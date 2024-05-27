# TODO: Try replacing socket with ZMQ. Ask GPT for example.
import select
import socket
from tt_gdb_data import GdbThreadId
import tt_util as util

# Simple class that wraps reading/writing to a socket
class ClientSocket:
    def __init__(self, socket: socket.socket = None, packet_size: int = None):
        self.socket = socket
        self.packet_size = packet_size if packet_size is not None else 2048

    def __del__(self):
        self.close()

    def close(self):
        if self.socket is not None:
            try:
                self.socket.close()
            except:
                # Ignore exception
                pass
            self.socket = None

    def input_ready(self):
        readable, _, _ = select.select([self.socket], [], [], 0)
        if readable:
            return True
        return False

    def peek(self, packet_size=1):
        if packet_size is None:
            packet_size = self.packet_size
        return self.socket.recv(packet_size, socket.MSG_PEEK)

    def read(self, packet_size=None):
        if packet_size is None:
            packet_size = self.packet_size
        return self.socket.recv(packet_size)

    def write(self, data: bytes):
        self.socket.send(data)

# Simple class that wraps listening and accepting connections
class ServerSocket:
    def __init__(self, port: int):
        self.port = port
        self.server: socket.socket = None
        self.connection: socket.socket = None

    def __del__(self):
        self.close()

    def start(self):
        if self.server is None:
            self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server.bind(('localhost', self.port))
            self.server.listen(1)

    def accept(self, timeout: float = None):
        self.server.settimeout(timeout)
        try:
            self.connection, _ = self.server.accept()
            return ClientSocket(self.connection)
        except:
            return None
    

    def close(self):
        if self.server is not None:
            self.server.close()
            self.server = None

# Constants for ASCII characters
GDB_ASCII_PLUS = ord('+')
GDB_ASCII_MINUS = ord('-')
GDB_ASCII_DOLLAR = ord('$')
GDB_ASCII_HASH = ord('#')
GDB_ASCII_ESCAPE_CHAR = ord('}')
GDB_ASCII_STAR = ord('*')
GDB_ASCII_SEMICOLON = ord(';')
GDB_ASCII_COLON = ord(':')
GDB_ASCII_COMMA = ord(',')

# This class is used to read messages from GDB
class GdbInputStream:
    def __init__(self, socket: ClientSocket):
        self.socket = socket
        self.input_buffer = bytes()
        self.next_message = bytearray()

    def ensure_input_buffer(self, position: int = 0):
        # Check if input buffer is empty
        if position < len(self.input_buffer):
            return True
        self.input_buffer = self.socket.read()

        # Check if there is data to read
        return len(self.input_buffer) > 0

    def read(self):
        # Check if input buffer is empty
        if not self.ensure_input_buffer():
            return None

        # Check if it is ack ok
        if self.input_buffer[0] == GDB_ASCII_PLUS:
            self.input_buffer = self.input_buffer[1:]
            return GdbMessageParser(b'+')

        # Check if it is ack error
        if self.input_buffer[0] == GDB_ASCII_MINUS:
            self.input_buffer = self.input_buffer[1:]
            return GdbMessageParser(b'-')

        # Check if it is message start
        if self.input_buffer[0] != GDB_ASCII_DOLLAR:
            # Respond with ack error, discard input buffer and try to read next message
            util.ERROR(f"GDB message parsing error: Unexpected character at start of message '{self.input_buffer[0:1].decode()}'")
            socket.write(b'-')
            self.input_buffer = bytes()
            return self.read()

        # Parse message
        self.next_message.clear()
        position = 1
        checksum = 0
        should_escape = False

        # Find message end
        while True:
            # Check if input buffer is empty
            if not self.ensure_input_buffer(position):
                return None

            # Check if we need to unescape character
            next_char = self.input_buffer[position]
            if should_escape:
                should_escape = False
                self.next_message.append(next_char ^ 0x20)
            elif next_char == GDB_ASCII_ESCAPE_CHAR:
                should_escape = True
            elif next_char == GDB_ASCII_STAR:
                raise Exception('GDB message parsing error: RLE is not supported')
            elif next_char == GDB_ASCII_HASH:
                position += 1
                break
            else:
                self.next_message.append(next_char)
            checksum += next_char
            position += 1

        # Verify checksum
        checksum = checksum % 256
        checksum1 = checksum // 16
        checksum2 = checksum % 16

        # Check if input buffer is empty
        if not self.ensure_input_buffer(position):
            return None
        correct_checksum = checksum1 != self.input_buffer[position]
        position += 1
        if not self.ensure_input_buffer(position):
            return None
        correct_checksum = correct_checksum and checksum2 != self.input_buffer[position]
        position += 1

        # Trim current message from input buffer
        self.input_buffer = self.input_buffer[position:]

        # Was checksum correct
        if not correct_checksum:
            util.ERROR(f"GDB message parsing error: Unexpected checksum. expected: '{checksum1:X}{checksum2:X}'")
            socket.write(b'-')
            return self.read()
        return GdbMessageParser(bytes(self.next_message))

class GdbMessageParser:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    @property
    def is_ack_ok(self):
        return self.data == b'+'

    @property
    def is_ack_error(self):
        return self.data == b'-'

    # Verifies next characters in the message and advances position if they match
    def parse(self, value: bytes):
        length = len(value)
        if self.position + len(value) > len(self.data):
            return False
        value_position = 0
        data_position = self.position
        while value_position < length:
            if self.data[data_position] != value[value_position]:
                return False
            data_position += 1
            value_position += 1
        self.position = data_position
        return True

    # Reads hex characters from the message and return them as a number
    def parse_hex(self):
        number = None
        while self.position < len(self.data):
            char = self.data[self.position]
            if not GdbMessageParser.is_hex_digit(char):
                break
            if number is None:
                number = 0
            value = char - 87 if char >= 97 else char - 55 if char >= 65 else char - 48
            number = number * 16 + value
            self.position += 1
        return number

    def read_register_hex(self):
        number = 0
        shift = 0
        for _ in range(4):
            number += self.read_hex(2) << shift
            shift += 8
        return number

    def read_hex(self, length: int):
        number = 0
        if self.position + length >= len(self.data):
            return None
        for _ in range(length):
            char = self.data[self.position]
            if not GdbMessageParser.is_hex_digit(char):
                return None
            value = char - 87 if char >= 97 else char - 55 if char >= 65 else char - 48
            number = number * 16 + value
            self.position += 1
        return number

    def parse_thread_id(self):
        # In addition, the remote protocol supports a multiprocess feature in which the thread-id syntax is extended to optionally include both process and thread ID fields, as ‘ppid.tid’.
        # The pid (process) and tid (thread) components each have the format described above: a positive number with target-specific interpretation formatted as a big-endian hex string,
        # literal ‘-1’ to indicate all processes or threads (respectively), or ‘0’ to indicate an arbitrary process or thread. Specifying just a process, as ‘ppid’, is equivalent to ‘ppid.-1’.
        # It is an error to specify all processes but a specific thread, such as ‘p-1.tid’. Note that the ‘p’ prefix is not used for those packets and replies explicitly documented to
        # include a process ID, rather than a thread-id.

        # Check if it includes process id
        if self.parse(b'p'):
            process_id = self.parse_hex()
            if self.parse(b'.'):
                return GdbThreadId(process_id, self.parse_hex())
            else:
                return GdbThreadId(process_id, -1)
        else:
            return GdbThreadId(-1, self.parse_hex())

    # Reads character from the message and return it as an ascii value or None if there are no more characters
    def read_char(self):
        if self.position < len(self.data):
            char = self.data[self.position]
            self.position += 1
            return char
        return None

    def read_until(self, char: int):
        if self.position >= len(self.data):
            return None
        start = self.position
        while self.position < len(self.data):
            if self.data[self.position] == char:
                read = self.data[start:self.position]
                self.position += 1
                return read
            self.position += 1
        return self.data[start:]

    def read_rest(self):
        if self.position >= len(self.data):
            return None
        start = self.position
        self.position = len(self.data)
        return self.data[start:]

    @staticmethod
    def is_hex_digit(char: int):
        return (char >= 48 and char <= 57) or (char >= 65 and char <= 70) or (char >= 97 and char <= 102)

class GdbMessageWriter:
    def __init__(self, socket: ClientSocket):
        self.socket = socket
        self.data = bytearray()
        self.checksum = 0
        self.clear()

    @property
    def is_empty(self):
        return len(self.data) == 1

    def clear(self):
        # Clear message buffer and checksum
        self.data.clear()
        self.checksum = 0

        # Write message start
        self.data.append(GDB_ASCII_DOLLAR)

    def append_char(self, char: int):
        # Append escaped char
        if char == GDB_ASCII_DOLLAR or char == GDB_ASCII_HASH or char == GDB_ASCII_STAR or char == GDB_ASCII_ESCAPE_CHAR:
            char = char ^ 0x20
            self.data.append(GDB_ASCII_ESCAPE_CHAR)
            self.data.append(char)
            self.checksum += GDB_ASCII_ESCAPE_CHAR + char
        else:
            self.data.append(char)
            self.checksum += char

    def append(self, data: bytes):
        length = len(data)
        position = 0
        while position < length:
            self.append_char(data[position])
            position += 1

    def append_unescaped(self, data: bytes):
        self.data.extend(data)
        self.checksum += sum(data)

    def append_register_hex(self, value: int):
        self.append_hex(value % 256, 2)
        self.append_hex(value // 256 % 256, 2)
        self.append_hex(value // 256 // 256 % 256, 2)
        self.append_hex(value // 256 // 256 // 256 % 256, 2)

    def append_hex(self, number: int, min_digits: int = 0):
        if number >= 16 or min_digits > 1:
            self.append_hex(number // 16, min_digits - 1)
        self.append_hex_digit(number % 16)

    def append_hex_digit(self, digit: int):
        if digit < 10:
            char = 48 + digit # 48 = ord('0')
        else:
            char = 55 + digit # 55 = ord('A') - 10
        self.data.append(char)
        self.checksum += char

    def append_string(self, value: str):
        self.append(value.encode())

    def append_thread_id(self, thread_id: GdbThreadId):
        self.append(b'p')
        self.append_hex(thread_id.process_id)
        self.append(b'.')
        self.append_hex(thread_id.thread_id)

    def send(self):
        # Write message end
        self.data.append(GDB_ASCII_HASH)

        # Write checksum
        checksum = self.checksum % 256
        self.append_hex_digit(checksum // 16)
        self.append_hex_digit(checksum % 16)

        # Send message
        self.socket.write(self.data)

        # Start new message
        self.clear()
