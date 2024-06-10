from io import StringIO
import threading
from typing import Dict, List, Set
from xml.sax.saxutils import escape as xml_escape, unescape as xml_unescape

from tt_gdb_communication import GDB_ASCII_COLON, GDB_ASCII_COMMA, GDB_ASCII_SEMICOLON, ClientSocket, GdbInputStream, GdbMessageParser, GdbMessageWriter, ServerSocket
from tt_gdb_data import GdbProcess, GdbThreadId
from tt_gdb_file_server import GdbFileServer
from tt_debuda_context import Context
from tt_debug_risc import RiscLoc, get_risc_name
import tt_util as util

# Helper class returns currently debugging list of threads to gdb client in paged manner
class GdbThreadListPaged:
    def __init__(self, threads: Dict[int,GdbThreadId]):
        self.threads = [thread for thread in threads.values() if isinstance(thread, GdbThreadId)]
        self.returned = 0

    def next(self, writer: GdbMessageWriter, max_length: int):
        # Check if we reached end of list
        if self.returned == len(self.threads):
            writer.append(b"l")
            return

        # Format list of threads until we hit max message length or end of the list
        message = "m"
        index = self.returned
        while index < len(self.threads):
            thread = self.threads[index].to_gdb_string()
            if len(thread) + len(message) + 1 > max_length:
                break
            if index != self.returned:
                message += ","
            message += thread
            index += 1
        writer.append_string(message)
        self.returned = index

# Class that serves gdb client requests
# Gdb remote protocol documentation: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
class GdbServer(threading.Thread):
    def __init__(self, context: Context, server: ServerSocket):
        super().__init__(daemon=True) # Spawn as deamon, so we don't block exit
        self.context = context # debuda context
        self.server = server # server socket used for listening to incoming connections
        self.is_connected = False # flag that indicates if gdb client is connected
        self.is_non_stop = False # flag that indicates if we are in non-stop mode
        self.should_ack = True # flag that indicates if we should send ack after each message
        self.stop_event = threading.Event() # event that indicates that server should stop

        self.prepared_responses_for_paging: Dict[str,str] = {} # prepared responses for paged messages
        self.client_features: Dict[str,object] = {} # dictionary of supported gdb client features (key: feature, value: mostly True/False, but can be anything)
        self.paged_thread_list: GdbThreadListPaged = None # helper class that returns list of threads in paged manner
        self.file_server: GdbFileServer = GdbFileServer() # File server that serves gdb client file operations
        self.vCont_pending_statuses: List[str] = [] # List of status reports that should be returned to gdb client (it is stored in reversed order, so we can pop it from the end of the list)

        self.current_process: GdbProcess = None # currently debugging process (core/thread - all in one)
        self.debugging_threads: Dict[int,GdbThreadId] = {} # Dictionary of threads that are currently being debugged (key: pid)
        self.next_pid = 1
        self._last_available_processes: Dict[RiscLoc,GdbProcess] = {} # Dictionary of last executed available processes that can be debugged (key: pid)

    @property
    def available_processes(self):
        available_processes: Dict[int,GdbProcess] = {} # Dictionary of available processes that can be debugged (key: pid)
        last_available_processes: Dict[RiscLoc,GdbProcess] = {}
        for device in self.context.devices.values():
            for risc_debug in device.debuggable_cores:
                if not risc_debug.is_in_reset():
                    try:
                        elf_path = self.context.get_risc_elf_path(risc_debug.location.loc, risc_debug.location.risc_id)
                    except:
                        # If we are running without full functionality, we will not have elf files available
                        elf_path = None

                    # Check if process is in self._last_available_processes and reuse it if it is
                    # TODO: In ideal world, we would have "start time" for a core (time when core was taken out of reset) and use that as a key for reusing process id; for now, we can just check if elf path is the same
                    last_process = self._last_available_processes.get(risc_debug.location)
                    if last_process is None or last_process.elf_path != elf_path:
                        core_type = "worker" # TODO: once we add support for ETH cores, we should update this
                        pid = self.next_pid
                        self.next_pid += 1
                        virtual_core = pid # TODO: Maybe we should actually have some mapping from RiscLoc to virtual core
                        process = GdbProcess(pid, elf_path, risc_debug, virtual_core, core_type)
                    else:
                        process = last_process
                    available_processes[process.process_id] = process
                    last_available_processes[risc_debug.location] = process
        self._last_available_processes = last_available_processes
        return available_processes

    def __del__(self):
        self.stop()

    def stop(self, wait=True):
        if self.is_alive():
            self.stop_event.set()
            if wait:
                self.join()
        self.server.close()
        self.file_server.close_all()

    def run(self):
        while not self.stop_event.is_set():
            client = self.server.accept(0.5)
            if client:
                self.is_connected = True
                self.should_ack = True
                try:
                    self.process_client(client)
                except Exception as e:
                    # Just log exceptions and continue with next client
                    util.ERROR(f"Unhandled exception in GDB implementation: {e}")
                client.close()
                self.is_connected = False
                self.file_server.close_all()

    def process_client(self, client: ClientSocket):
        util.VERBOSE("GDB client connected")
        input_stream = GdbInputStream(client)
        writer = GdbMessageWriter(client)
        while not self.stop_event.is_set():
            parser = input_stream.read()
            if not parser:
                break
            try:
                should_ack = self.should_ack
                if self.process_message(parser, writer):
                    if should_ack:
                        client.write(b'+')
                    writer.send()
            except Exception as e:
                client.write(b'-')
                util.ERROR(f"GDB exception: {e}")
        util.VERBOSE("GDB client closed")

    def process_message(self, parser: GdbMessageParser, writer: GdbMessageWriter):
        if parser.is_ack_ok:
            # Ignore this message
            return False

        if parser.is_ack_error:
            util.ERROR("GDB client error")
            # Ignore this message
            return False

        try:
            util.VERBOSE(f"processing GDB message: {parser.data.decode()}")
        except:
            # We ignore error if we cannot decode message
            pass

        if parser.parse(b"!"): # Enable extended mode.
            writer.append(b"OK")
        elif parser.parse(b"?"): # This is sent when connection is first established to query the reason the target halted.
            # Check if we are debugging some thread
            if self.current_process is None:
                # Respond that process exited with exit status 0.
                # This means that we are not debugging anything at the moment and if gdb client is started in extended mode
                # it will be able to query which process it can attach to. Process = some riscv core on device
                writer.append(b"W00")
            else:
                # Return trap signal
                last_signal = 5
                writer.append(b"S")
                writer.append_hex(last_signal, 2)
        elif parser.parse(b"A"):
            # We don't support initialization of argv
            writer.append(b"E01")
        elif parser.parse(b"c"): # Continue at addr, which is the address to resume. If addr is omitted, resume at current address.
            # ‘c [addr]’
            address = parser.parse_hex()
            if address is not None:
                util.WARN(f"GDB: client want to continue at address: {address}, but we don't support that. We will continue at current address.")
            util.ERROR(f"GDB: client uses single threaded continue instead of multi-threaded version. Returning error to GDB client and waiting for 'vCont' message")
            writer.append(b"E01")
        elif parser.parse(b"C"): # Continue with signal sig (hex signal number). If ‘;addr’ is omitted, resume at same address.
            # ‘C sig[;addr]’
            # We don't support signaling
            writer.append(b"E01")
        elif parser.parse(b"D"): # Detach GDB from the remote system, or detach only a specific process.
            # ‘D’
            # ‘D;pid’
            if parser.parse(b";"):
                pid = parser.parse_hex()
                if pid in self.debugging_threads:
                    thread_id = self.debugging_threads.pop(pid)
                    process = self.available_processes.get(pid)
                    if process is not None:
                        # Remove all break points from the process
                        for bid in range(0, process.risc_debug.max_watchpoints):
                            process.risc_debug.disable_watchpoint(bid)

                        # Continue process if it is halted
                        if process.risc_debug.is_halted():
                            process.risc_debug.cont()
                    writer.append(b"OK")
                else:
                    writer.append(b"E01")
            else:
                # We should detach all processes that we are debugging
                for pid in self.debugging_threads.keys():
                    process = self.available_processes.get(pid)
                    if process is not None:
                        # Remove all break points from the process
                        for bid in range(0, process.risc_debug.max_watchpoints):
                            process.risc_debug.disable_watchpoint(bid)

                        # Continue process if it is halted
                        if process.risc_debug.is_halted():
                            process.risc_debug.cont()
                self.debugging_threads.clear()
                writer.append(b"OK")
        elif parser.parse(b"g"): # Read general registers.
            # ‘g’
            # Register definitions: https://github.com/riscvarchive/riscv-binutils-gdb/blob/5da071ef0965b8054310d8dde9975037b0467311/gdb/features/riscv/32bit-cpu.c
            # TODO: Use arc to read registers faster
            for j in range(0, GdbServer.REGISTER_COUNT):
                value = self.current_process.risc_debug.read_gpr(j)
                writer.append_register_hex(value)
        elif parser.parse(b"G"): # Write general registers.
            # ‘G XX...’
            # TODO: There are two optimizations: 1. Use arc to write registers faster 2. If read was done during halt, we know some registers and we can skip writing them if they didn't change
            for j in range(0, GdbServer.REGISTER_COUNT):
                if parser.parse(b"xxxxxxxx") or parser.parse(b"XXXXXXXX"):
                    # Skip this register
                    pass
                else:
                    value = parser.read_register_hex()
                    if value is None:
                        writer.append(b"E01")
                        return True
                    self.current_process.risc_debug.write_gpr(j, value)
            writer.append(b"OK")
        elif parser.parse(b"H"): # Set thread for subsequent operations (‘m’, ‘M’, ‘g’, ‘G’, et.al.).
            # ‘H op thread-id’
            op = parser.read_char()
            thread_id = parser.parse_thread_id()
            util.VERBOSE(f"GDB: Set thread {thread_id} and prepare for op '{chr(op)}'")
            if self.current_process is None:
                # Respond that we are not debugging anything at the moment
                writer.append(b"E01")
            else:
                # Verify that requested thread is in list of debugging threads
                if thread_id.process_id == -1:
                    # We are not changing current process
                    thread_id = self.current_process.thread_id
                else:
                    # Since our processes have only 1 thread, we will pick this up from the cache
                    t = self.debugging_threads.get(thread_id.process_id)
                    if t is None:
                        # Unknown process!!!
                        util.ERROR(f"GDB: Unknown process id in self.debugging_threads: {thread_id.process_id}")
                        writer.append(b"E01")
                        return
                    thread_id = t

                # Change current thread
                process = self.available_processes.get(thread_id.process_id)
                if process is None:
                    # Unknown process!!!
                    util.ERROR(f"GDB: Unknown process id in self.available_processes: {thread_id.process_id}")
                    writer.append(b"E01")
                else:
                    self.current_process = process
                    writer.append(b"OK")
        elif parser.parse(b"i"): # Step the remote target by a single clock cycle.
            # ‘i [addr[,nnn]]’
            # We don't support step by single clock cycle
            writer.append(b"E01")
        elif parser.parse(b"I"): # Signal, then cycle step.
            # ‘I’
            # We don't support step by single clock cycle
            writer.append(b"E01")
        elif parser.parse(b"k"): # Kill request.
            # TODO: ‘k’
            # TODO: We should raise exception and close client connection. Also, we should detach from all processes and let device continue working as it was before we connected.
            pass
        elif parser.parse(b"m"): # Read length addressable memory units starting at address addr.
            # ‘m addr,length’
            address = parser.parse_hex()
            parser.parse(b',')
            length = parser.parse_hex()

            if self.current_process is None:
                # Return error if we are not debugging any process
                writer.append(b"E02")
            elif length <= 0:
                writer.append(b"E01")
            else:
                # Read first 4 bytes of unaligned data
                first_offset = address % 4
                if first_offset != 0:
                    value = self.current_process.risc_debug.read_memory(address - first_offset)
                    buffer = value.to_bytes(4, byteorder='little')
                    used_bytes = min(4 - first_offset, length)
                    writer.append_hex(int.from_bytes(buffer[first_offset:first_offset + used_bytes], byteorder='little'), 2 * used_bytes)
                    length -= used_bytes
                    address += used_bytes

                # Read aligned data
                while length >= 4:
                    value = self.current_process.risc_debug.read_memory(address)
                    writer.append_register_hex(value)
                    length -= 4
                    address += 4

                # Read last 4 bytes of unaligned data
                if length > 0:
                    value = self.current_process.risc_debug.read_memory(address)
                    buffer = value.to_bytes(4, byteorder='little')
                    writer.append_hex(int.from_bytes(buffer[:length], byteorder='little'), 2 * length)
        elif parser.parse(b"M"): # Write length addressable memory units starting at address addr.
            # ‘M addr,length:XX…’
            address = parser.parse_hex()
            parser.parse(b',')
            length = parser.parse_hex()
            parser.parse(b':')
            data = parser.read_rest()
            if len(data) != 2 * length or length <= 0:
                # Return error if we didn't get all data
                writer.append(b"E01")
            elif self.current_process is None:
                # Return error if we are not debugging any process
                writer.append(b"E02")
            else:
                # Write first 4 bytes of unaligned data
                first_offset = address % 4
                if first_offset != 0:
                    # First read 4 bytes, since we don't want to override existing data
                    value = self.current_process.risc_debug.read_memory(address - first_offset)
                    used_bytes = min(4 - first_offset, length)

                    # Update number
                    new_value = value
                    for i in range(first_offset, first_offset + used_bytes):
                        mask = 0xFF << (8*i)
                        new_value = (new_value & mask) + parser.read_hex(2)

                    # Write bytes
                    self.current_process.risc_debug.write_memory(address - first_offset, new_value)
                    length -= used_bytes
                    address += used_bytes

                # Write aligned data
                while length >= 4:
                    value = parser.read_register_hex()
                    self.current_process.risc_debug.write_memory(address, value)
                    length -= 4
                    address += 4

                # Write last 4 bytes of unaligned data
                if length > 0:
                    # First read 4 bytes, since we don't want to override existing data
                    value = self.current_process.risc_debug.read_memory(address)

                    # Update number
                    new_value = value
                    for i in range(0, length):
                        mask = 0xFF << (8*i)
                        new_value = (new_value & mask) + parser.read_hex(2)

                    # Write bytes
                    self.current_process.risc_debug.write_memory(address, new_value)
                writer.append(b"OK")
        elif parser.parse(b"p"): # Read the value of register n; n is in hex.
            # ‘p n’
            register = parser.parse_hex()
            if register < 0 or register > GdbServer.REGISTER_COUNT:
                writer.append(b"E01")
            else:
                value = self.current_process.risc_debug.read_gpr(register)
                writer.append_hex(value, 8)
        elif parser.parse(b"P"): # Write register n… with value r….
            # ‘P n…=r…’
            register = parser.parse_hex()
            parser.parse(b'=')
            value = parser.read_hex(8)
            if register < 0 or register > GdbServer.REGISTER_COUNT:
                writer.append(b"E01")
            else:
                self.current_process.risc_debug.write_gpr(register, value)
                writer.append(b"OK")
        elif parser.parse(b"qSupported"): # Tell the remote stub about features supported by GDB, and query the stub for features it supports.
            # Read supported gdb client features
            if parser.parse(b":"):
                util.VERBOSE("GDB: client features:")
                while True:
                    feature = parser.read_until(GDB_ASCII_SEMICOLON)
                    if not feature:
                        break
                    feature = feature.decode()
                    if feature.endswith("+") or feature.endswith("-"):
                        value = feature.endswith("+")
                        feature = feature[:-1]
                        self.client_features[feature] = value
                        util.VERBOSE(f"     - {feature} = {value}")
                    else:
                        feature,value = feature.split('=')
                        self.client_features[feature] = value
                        util.VERBOSE(f"     - {feature} = {value}")

            # Return supported features
            writer.append(b"PacketSize=")
            writer.append_hex(writer.socket.packet_size)
            writer.append(b";multiprocess+;swbreak+;QStartNoAckMode+;qXfer:osdata:read+;qXfer:exec-file:read+;qXfer:features:read+")
        elif parser.parse(b"qTStatus"): # Ask the stub if there is a trace experiment running right now.
            # Since we don't support trace experiments, we should return empty packet
            pass
        elif parser.parse(b"qfThreadInfo"): # Obtain a list of all active thread IDs from the target (OS).
            self.paged_thread_list = GdbThreadListPaged(self.debugging_threads)
            self.paged_thread_list.next(writer, writer.socket.packet_size - 4)
        elif parser.parse(b"qsThreadInfo"): # Obtain a list of all active thread IDs from the target (OS).
            if self.paged_thread_list is None:
                # If we didn't initialize thread list, we should return empty list
                # To avoid implementing this feature on multiple places, we will initialize it with empty dictionary
                self.paged_thread_list = GdbThreadListPaged({})
            self.paged_thread_list.next(writer, writer.socket.packet_size - 4)
        elif parser.parse(b"qC"): # Return the current thread ID.
            # Return the current thread ID.
            if self.current_process is not None:
                writer.append(b"QC")
                writer.append_thread_id(self.current_process.thread_id)
            else:
                # We are not debugging anything at the moment...
                writer.append(b"E01")
        elif parser.parse(b"qAttached"): # Return an indication of whether the remote server attached to an existing process or created a new process.
            # ‘qAttached:pid’
            # Since we are not starting process, but only attaching to device that is already running some code, we return `1`
            writer.append(b"1")
        elif parser.parse(b"qXfer:"): # Read uninterpreted bytes from the target’s special data area identified by the keyword object.
            object = parser.read_until(GDB_ASCII_COLON).decode()
            operation = parser.read_until(GDB_ASCII_COLON)
            annex = parser.read_until(GDB_ASCII_COLON).decode()
            offset = parser.parse_hex()
            if not parser.parse(b','):
                util.ERROR(f"GDB: Something wrong with offset and length: {parser.data}")
                writer.append(b"E01")
                return True
            length = parser.parse_hex()
            paging_key = f"{object}_{annex}"
            if operation == b"read" and offset > 0:
                # We prepared string that should be returned with this command, just continue paging it...
                self.write_paged_message(self.prepared_responses_for_paging[paging_key], offset, length, writer)
            elif object == "osdata" and operation == b"read" and annex == "":
                # List available types for listing
                self.prepared_responses_for_paging[paging_key] = self.create_osdata_types_response()
                self.write_paged_message(self.prepared_responses_for_paging[paging_key], offset, length, writer)
            elif object == "osdata" and operation == b"read" and annex == "processes":
                # List available processes for debugging
                self.prepared_responses_for_paging[paging_key] = self.create_osdata_processes_response()
                self.write_paged_message(self.prepared_responses_for_paging[paging_key], offset, length, writer)
            elif object == "features" and operation == b"read" and annex == "target.xml":
                self.prepared_responses_for_paging[paging_key] = self.create_features_target_response()
                self.write_paged_message(self.prepared_responses_for_paging[paging_key], offset, length, writer)
            elif object == "exec-file" and operation == b"read":
                # Return path to elf file used to run on selected core. Annex represent process id (without p)
                pid = int(annex, 16)
                process = self.available_processes.get(pid)
                if process is None:
                    util.ERROR(f"GDB: bad process id: {pid} ({annex})")
                    writer.append(b"E01")
                else:
                    self.prepared_responses_for_paging[paging_key] = process.elf_path
                    self.write_paged_message(self.prepared_responses_for_paging[paging_key], offset, length, writer)
            else:
                # We don't support this message
                util.ERROR(f"GDB: unsupported message: {parser.data}")
                pass
        elif parser.parse(b"qSymbol"): # Notify the target that GDB is prepared to serve symbol lookup requests.
            # We don't need symbol lookup (for now)
            pass
        elif parser.parse(b"qOffsets"): # Get section offsets that the target used when relocating the downloaded image. 
            # Addresses are exactly the same as they are written in elf file, so we don't need to implement this message
            pass
        elif parser.parse(b"QStartNoAckMode"): # Request that the remote stub disable the normal ‘+’/‘-’ protocol acknowledgments
            # Enter start-no-ack mode.
            self.should_ack = False
            writer.append(b"OK")
        elif parser.parse(b"s"): # Single step, resuming at addr. If addr is omitted, resume at same address. 
            # ‘s [addr]’
            address = parser.parse_hex()
            if address is not None:
                util.WARN(f"GDB: client want to single step at address: {address}, but we don't support that. We will single step at current address.")
            util.ERROR(f"GDB: client uses single threaded single step instead of multi-threaded version. Returning error to GDB client and waiting for 'vCont' message")
            writer.append(b"E01")
        elif parser.parse(b"S"): # Step with signal.
            # ‘S sig[;addr]’
            # We don't support this message
            writer.append(b"E01")
        elif parser.parse(b"T"): # Find out if the thread thread-id is alive.
            # ‘T thread-id’
            thread_id = parser.parse_thread_id()
            if thread_id.process_id in self.available_processes:
                writer.append(b"OK")
            else:
                writer.append(b"E01")
        elif parser.parse(b"vMustReplyEmpty"): # The correct reply to an unknown ‘v’ packet is to return the empty string, however, some older versions of gdbserver would incorrectly return ‘OK’ for unknown ‘v’ packets.
            # We should reply with empty message
            pass
        elif parser.parse(b"vAttach"): # Attach to a new process with the specified process ID pid. The process ID is a hexadecimal integer identifying the process.
            # ‘vAttach;pid’
            if parser.parse(b";"):
                pid = parser.parse_hex()
                # Check if pid is in the list of available processes and repond with error if it is not
                process = self.available_processes.get(pid)
            else:
                process = None
            if process is None:
                writer.append(b"E01")
            elif self.debugging_threads.get(process.process_id) is not None:
                util.WARN(f"GDB: attaching to process that we are already debugging: {process.process_id}")
                writer.append(b"E01")
            else:
                try:
                    # We should halt selected core
                    process.risc_debug.enable_debug()
                    process.risc_debug.halt()
                    self.debugging_threads[process.process_id] = process.thread_id

                    # Respond with Stop Reply Packet
                    # Example on x64: T0006:008e3ced17560000;07:d0b6f7f6fe7f0000;10:9bf26da6a27f0000;thread:pd60.d60;core:5;
                    # We want to return "T00" - signal 0, user requested halt
                    writer.append(b"T00")
                    # TODO: Then we want to return maybe list of registers that we read?!? Basically, we can use this opportunity to return only needed registers to gdb so it won't issue 'g' command.
                    # And lastly, we want to return thread and core
                    writer.append(b";thread:")
                    writer.append_thread_id(process.thread_id)
                    writer.append(b";core:")
                    writer.append_hex(process.virtual_core_id)
                    writer.append(b";")
                    self.current_process = process
                except Exception as e:
                    util.ERROR(f"++ exception while halting: {e}")
                    writer.clear()
                    writer.append(b"E01")
        elif parser.parse(b"vCont?"): # Request a list of actions supported by the ‘vCont’ packet. 
            # We support continue, step and stop
            writer.append(b"vCont;c;C;s;S;t")
        elif parser.parse(b"vCont"): # Resume the inferior, specifying different actions for each thread.
            # ‘vCont[;action[:thread-id]]…’

            # Create dictionary per thread that will contain actions that should be executed on that thread
            thread_actions = {}
            for pid in self.debugging_threads.keys():
                thread_actions[pid] = None

            # First check if there is some thread that has pending status to report.
            if len(self.vCont_pending_statuses) > 0:
                # Last run should have created list of all status reports, but should return only one status report per run
                writer.append(self.vCont_pending_statuses.pop().encode())
                return True

            # Parse input actions
            while parser.parse(b';'):
                # Parse action
                action = parser.read_char()

                # Check if action is supported
                if action == 99: # 'c' - continue
                    thread_action = 'c'
                elif action == 67: # 'C' - continue with signal
                    thread_action = 'c'
                    signal = parser.parse_hex() # ignore signal
                elif action == 115: # 's' - step
                    thread_action = 's'
                elif action == 82: # 'S' - step with signal
                    thread_action = 's'
                    signal = parser.parse_hex() # ignore signal
                elif action == 116: # 't' - stop
                    if not self.is_non_stop:
                        util.WARN(f"GDB: client wanted to stop thread, but we are not in non-stop mode")
                        writer.append(b"E01")
                        return True
                    thread_action = 't'
                else:
                    # Unsupported action
                    util.WARN(f"GDB: unsupported continue action: {chr(action)}")
                    writer.append(b"E01")
                    return True

                # Parse thread id
                if parser.parse(b':'):
                    # Execute action on selected thread
                    thread_id = parser.parse_thread_id()
                    if thread_id is not None:
                        if thread_id.process_id == -1:
                            thread_id = None
                        elif thread_id.process_id not in self.debugging_threads:
                            util.WARN(f"GDB: we are not debugging process id: {thread_id.process_id}")
                            writer.append(b"E01")
                            return True
                else:
                    thread_id = None

                # Check if this action is for specific core or for all cores
                if thread_id:
                    # Check if we are already executing action on this thread
                    if thread_id.process_id in thread_actions and thread_actions[thread_id.process_id] is not None:
                        # We already executed action on this thread, so we can skip it
                        continue

                    # Save action for this thread
                    thread_actions[thread_id.process_id] = thread_action
                else:
                    # Save action on all threads that didn't have action associated and stop processing this message
                    for pid in thread_actions.keys():
                        if thread_actions[pid] is None:
                            thread_actions[pid] = thread_action
                    break

            # Check if all processes are alive and report Stop Reply Packet if some process is not alive
            available_processes = self.available_processes
            for pid in thread_actions.keys():
                # Check if process is still alive
                if pid not in available_processes:
                    # Reply that process exited
                    writer.append(b"W00;process:")
                    writer.append_hex(pid)
                    return True

            # Apply actions
            processes_to_watch: Set[GdbProcess] = set()
            for pid in thread_actions.keys():
                # NOTE: The server must ignore ‘c’, ‘C’, ‘s’, ‘S’, and ‘r’ actions for threads that are already running. Conversely, the server must ignore ‘t’ actions for threads that are already stopped.
                process = available_processes.get(pid)
                action = thread_actions[pid]
                if action == 'c': # Continue
                    # Continue only if we are not already running.
                    if process.risc_debug.is_halted():
                        process.risc_debug.cont(verify=False)
                        processes_to_watch.add(process)
                elif action == 's': # Step
                    # Step only if we are not already running.
                    if process.risc_debug.is_halted():
                        process.risc_debug.step()
                        processes_to_watch.add(process)
                elif action == 't': # Stop
                    # Stop only if we are already running
                    if not process.risc_debug.is_halted() and not process.risc_debug.is_in_reset():
                        process.risc_debug.halt()
                        # TODO: Write something to response?!? This is only in non-stop mode...
                elif not process.risc_debug.is_halted(): # Unchanged, but still running
                    # Process state should not be changed (action = unchanged), but it is already running, so it should be watched
                    processes_to_watch.add(process)

            # Watch processes that are running for changes and report (only in all-stop mode)
            if not self.is_non_stop:
                stop_watching = False
                client_socket = writer.socket
                while not stop_watching:
                    # Check if user wants to stop gdb
                    if self.stop_event.is_set():
                        stop_watching = True

                    # Check if client sent break to stop all processes
                    if client_socket.input_ready():
                        peek = client_socket.peek(1)
                        if peek == b'\x03':
                            # Client sent break
                            client_socket.read(1)
                            stop_watching = True

                    # Check processes that we are watching
                    for process in processes_to_watch:
                        # TODO: Check if same process is still alive (timestamp is increasing) -> Remove process from processes to watch so that we don't halt this core as gdb is not debugging this new process

                        # Check if core is not in reset
                        if process.risc_debug.is_in_reset():
                            # Report that process exited
                            self.vCont_pending_statuses.append(f"W00;process:{pid:x}")
                            stop_watching = True

                        # Check if core hit watchpoint
                        risc_debug_status = process.risc_debug.read_status()
                        if risc_debug_status.is_halted:
                            # Report that process is halted
                            if risc_debug_status.is_memory_watchpoint_hit:
                                watchpoints = process.risc_debug.read_watchpoints_state()
                                watchpoints_hit = risc_debug_status.watchpoints_hit
                                for i in range(0, process.risc_debug.max_watchpoints):
                                    if watchpoints_hit[i]:
                                        assert watchpoints[i].is_enabled

                                        # If it is memory watchpoint, check type of watchpoint (read, write, access) and get address
                                        if watchpoints[i].is_memory:
                                            if watchpoints[i].is_write:
                                                detected_signal = "watch:"
                                            elif watchpoints[i].is_read:
                                                detected_signal = "rwatch:"
                                            else:
                                                detected_signal = "awatch:"
                                            detected_signal = f"{detected_signal}{process.risc_debug.read_watchpoint_address(i):x};"
                                            break
                            elif risc_debug_status.is_pc_watchpoint_hit:
                                detected_signal = "swbreak:;" # This is also software breakpoint and not hardware breakpoint in gdb eyes
                            elif risc_debug_status.is_ebreak_hit:
                                detected_signal = "swbreak:;"
                            else:
                                detected_signal = ""
                            self.vCont_pending_statuses.append(f"T05{detected_signal}thread:p{process.thread_id.process_id:x}.{process.thread_id.thread_id:x};core:{process.virtual_core_id:x};")
                            stop_watching = True
                if len(self.vCont_pending_statuses) > 0:
                    self.vCont_pending_statuses.append("T05")
                else:
                    self.vCont_pending_statuses.append("S02")
                self.vCont_pending_statuses.reverse()
                writer.append(self.vCont_pending_statuses.pop().encode())

                # Once we are done with watching, we should stop all processes that we are debugging
                for process in processes_to_watch:
                    if not process.risc_debug.is_in_reset() and not process.risc_debug.is_halted():
                        process.risc_debug.halt()
            else:
                writer.append(b"OK")
        elif parser.parse(b"vFile:setfs:"): # Select the filesystem on which vFile operations with filename arguments will operate.
            # ‘vFile:setfs: pid’
            # Select the filesystem on which vFile operations with filename arguments will operate.
            # If pid is nonzero, select the filesystem as seen by process pid. If pid is zero, select the filesystem as seen by the remote stub.
            # Return 0 on success, or -1 if an error occurs.
            pid = parser.parse_hex()
            if pid != 0 and self.available_processes.get(pid) is None:
                writer.append(b"F-1")
            else:
                # We don't have different file system per process, so we just respond with OK
                writer.append(b"F0")
        elif parser.parse(b"vFile:open:"): # Open a file at filename and return a file descriptor for it, or return -1 if an error occurs.
            # ‘vFile:open: filename, flags, mode’
            filename = parser.read_until(GDB_ASCII_COMMA).decode()
            filename = bytes.fromhex(filename).decode()
            flags = parser.parse_hex()
            parser.parse(b',')
            mode = parser.parse_hex()
            result = self.file_server.open(filename, flags, mode)
            writer.append(b"F")
            if type(result) is str:
                writer.append_string(result)
            else:
                writer.append_hex(result)
        elif parser.parse(b"vFile:close:"): # Close the open file corresponding to fd and return 0, or -1 if an error occurs. 
            # ‘vFile:close: fd’
            fd = parser.parse_hex()
            if self.file_server.close(fd):
                writer.append(b"F0")
            else:
                writer.append(b"F-1")
        elif parser.parse(b"vFile:pread:"): # Read data from the open file corresponding to fd.
            # ‘vFile:pread: fd, count, offset’
            fd = parser.parse_hex()
            parser.parse(b',')
            count = parser.parse_hex()
            parser.parse(b',')
            offset = parser.parse_hex()
            result = self.file_server.pread(fd, count, offset)
            if type(result) is str:
                writer.append(b"F")
                writer.append_string(result)
            else:
                # We expect result to be bytes
                writer.append(b"F")
                writer.append_hex(len(result))
                writer.append(b";")
                writer.append(result)
        elif parser.parse(b"vFile:pwrite:"): # Write data (a binary buffer) to the open file corresponding to fd.
            # ‘vFile:pwrite: fd, offset, data’
            fd = parser.parse_hex()
            parser.parse(b',')
            offset = parser.parse_hex()
            parser.parse(b',')
            data = parser.read_rest()
            writer.append(b"F-1")
            if type(result) is str:
                writer.append(b"F")
                writer.append_string(result)
            else:
                # We expect result to be int, number of bytes written
                writer.append(b"F")
                writer.append_hex(result)
        elif parser.parse(b"X"): # Write data to memory, where the data is transmitted in binary.
            # ‘X addr,length:XX…’
            try:
                address = parser.parse_hex()
                parser.parse(b',')
                length = parser.parse_hex()
                parser.parse(b':')
                data = parser.read_rest()

                # Check if we are debugging something
                if len(data) != length or length <= 0:
                    # Return error if we didn't get all data
                    writer.append(b"E01")
                elif self.current_process is None:
                    writer.append(b"E02")
                else:
                    data_index = 0

                    # Write first 4 bytes of unaligned data
                    first_offset = address % 4
                    if first_offset != 0:
                        # First read 4 bytes, since we don't want to override existing data
                        value = self.current_process.risc_debug.read_memory(address - first_offset)
                        used_bytes = min(4 - first_offset, length)

                        # Update number
                        new_value = value
                        for i in range(first_offset, first_offset + used_bytes):
                            mask = 0xFF << (8*i)
                            new_value = (new_value & mask) + data[data_index]
                            data_index += 1

                        # Write bytes
                        self.current_process.risc_debug.write_memory(address - first_offset, new_value)
                        length -= used_bytes
                        address += used_bytes

                    # Write aligned data
                    while length >= 4:
                        value = int.from_bytes(data[data_index:data_index+4], byteorder='little')
                        data_index += 4
                        self.current_process.risc_debug.write_memory(address, value)
                        length -= 4
                        address += 4

                    # Write last 4 bytes of unaligned data
                    if length > 0:
                        # First read 4 bytes, since we don't want to override existing data
                        value = self.current_process.risc_debug.read_memory(address)

                        # Update number
                        new_value = value
                        for i in range(0, length):
                            mask = 0xFF << (8*i)
                            new_value = (new_value & mask) + data[data_index]
                            data_index += 1

                        # Write bytes
                        self.current_process.risc_debug.write_memory(address, new_value)
                    writer.append(b"OK")
            except:
                writer.append(b"E03")
        elif parser.parse(b"z0,"): # Remove a software breakpoint at address of type kind.
            # ‘z0,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex() # We ignore kind for now
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and not watchpoints[i].is_memory:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        self.current_process.risc_debug.disable_watchpoint(i)
                        writer.append(b"OK")
                        return True
        elif parser.parse(b"z1,"): # Remove a hardware breakpoint at address.
            # ‘z1,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex() # We ignore kind for now
            # TODO: We don't support hardware breakpoints
        elif parser.parse(b"z2,"): # Remove a write watchpoint at address.
            # ‘z2,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_write:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        self.current_process.risc_debug.disable_watchpoint(i)
                        writer.append(b"OK")
                        return True
        elif parser.parse(b"z3,"): # Remove a read watchpoint at address.
            # ‘z3,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_read:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        self.current_process.risc_debug.disable_watchpoint(i)
                        writer.append(b"OK")
                        return True
        elif parser.parse(b"z4,"): # Remove an access watchpoint at address.
            # ‘z4,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_access:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        self.current_process.risc_debug.disable_watchpoint(i)
                        writer.append(b"OK")
                        return True
        elif parser.parse(b"Z0,"): # Insert a software breakpoint at address of type kind.
            # ‘Z0,addr,kind[;cond_list…][;cmds:persist,cmd_list…]’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()
            # TODO: Add support for conditional break point
            # TODO: Add support for optional command list that should be executed when breakpoint is hit

            # Check if we already have watchpoint at this address
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and not watchpoints[i].is_memory:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        writer.append(b"OK")
                        return True

            # Find empty slot to add watchpoint
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if not watchpoints[i].is_enabled:
                    self.current_process.risc_debug.set_watchpoint_on_pc_address(i, addr)
                    writer.append(b"OK")
                    return True

            # No more free slots for setting breakpoint
            util.WARN(f"GDB: No more free slots for setting breakpoint...")
            writer.append(b"E01")
        elif parser.parse(b"Z1,"): # Insert a hardware breakpoint at address of type kind.
            # ‘Z1,addr,kind[;cond_list…][;cmds:persist,cmd_list…]’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()
            # TODO: We don't support hardware breakpoints
        elif parser.parse(b"Z2,"): # Insert a write watchpoint at address. The number of bytes to watch is specified by kind.
            # ‘Z2,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()

            # TODO: we are not using range for memory watchpoints, but we should at least inform user about that

            # Check if we already have watchpoint at this address
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_write:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        writer.append(b"OK")
                        return True

            # Find empty slot to add watchpoint
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if not watchpoints[i].is_enabled:
                    self.current_process.risc_debug.set_watchpoint_on_memory_write(i, addr)
                    writer.append(b"OK")
                    return True

            # No more free slots for setting breakpoint
            util.WARN(f"GDB: No more free slots for setting breakpoint...")
            writer.append(b"E01")
        elif parser.parse(b"Z3,"): # Insert a read watchpoint at address. The number of bytes to watch is specified by kind.
            # ‘Z3,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()

            # TODO: we are not using range for memory watchpoints, but we should at least inform user about that

            # Check if we already have watchpoint at this address
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_read:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        writer.append(b"OK")
                        return True

            # Find empty slot to add watchpoint
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if not watchpoints[i].is_enabled:
                    self.current_process.risc_debug.set_watchpoint_on_memory_read(i, addr)
                    writer.append(b"OK")
                    return True

            # No more free slots for setting breakpoint
            util.WARN(f"GDB: No more free slots for setting breakpoint...")
            writer.append(b"E01")
        elif parser.parse(b"Z4,"): # Insert an access watchpoint at address. The number of bytes to watch is specified by kind.
            # ‘Z4,addr,kind’
            addr = parser.parse_hex()
            parser.parse(b',')
            kind = parser.parse_hex()

            # TODO: we are not using range for memory watchpoints, but we should at least inform user about that

            # Check if we already have watchpoint at this address
            watchpoints = self.current_process.risc_debug.read_watchpoints_state()
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if watchpoints[i].is_enabled and watchpoints[i].is_memory and watchpoints[i].is_access:
                    watchpoint_address = self.current_process.risc_debug.read_watchpoint_address(i)
                    if watchpoint_address == addr:
                        writer.append(b"OK")
                        return True

            # Find empty slot to add watchpoint
            for i in range(0, self.current_process.risc_debug.max_watchpoints):
                if not watchpoints[i].is_enabled:
                    self.current_process.risc_debug.set_watchpoint_on_memory_access(i, addr)
                    writer.append(b"OK")
                    return True

            # No more free slots for setting breakpoint
            util.WARN(f"GDB: No more free slots for setting breakpoint...")
            writer.append(b"E01")
        else:
            # Return unsupported message
            util.WARN(f"GDB: unsupported message: {parser.data}")
            pass
        return True

    def write_paged_message(self, message: str, offset: int, length: int, writer: GdbMessageWriter):
        if offset >= len(message):
            writer.append(b"l")
        elif offset + length >= len(message):
            writer.append(b"l")
            writer.append_string(message[offset:])
        else:
            writer.append(b"m")
            writer.append_string(message[offset:offset+length])

    def create_osdata_types_response(self):
        # Currently we only support processes
        return GdbServer.serialize_to_xml("osdata", "types", [
            {'Type': 'processes', 'Description': 'Listing of all processes'}
        ])

    def create_osdata_processes_response(self):

        processes = []
        for pid, process in self.available_processes.items():
            processes.append({
                'pid': pid,
                'user': self.context.short_name,
                'cores': process.virtual_core_id,
                'device': process.risc_debug.location.loc._device._id,
                'core_type': process.core_type,
                'core_location': process.risc_debug.location.loc.to_user_str(),
                'risc': get_risc_name(process.risc_debug.location.risc_id), # TODO: Once we add support for ETH cores, we should update this -> method should be part of device class and it also needs coordinate as well
                'command': process.elf_path,
            })

        return GdbServer.serialize_to_xml("osdata", "processes", processes)

    @staticmethod
    def serialize_to_xml(name: str, type: str, data: List[Dict[str,str]]):
        sb = StringIO()
        sb.write(f'<{name} type="{xml_escape(type)}">')
        for item in data:
            sb.write('<item>')
            for key, value in item.items():
                sb.write(f'<column name="{xml_escape(key)}">{xml_escape(str(value))}</column>')
            sb.write('</item>')
        sb.write(f'</{name}>')
        return sb.getvalue()

    # Number of registers is defined in create_features_target_response
    REGISTER_COUNT = 33

    def create_features_target_response(self):
        return """<?xml version="1.0"?>
<!DOCTYPE feature SYSTEM "gdb-target.dtd">
<target>
    <architecture>riscv:rv32</architecture>
    <feature name="org.gnu.gdb.riscv.cpu">
        <reg name="zero" bitsize="32" type="int" regnum="0"/>
        <reg name="ra" bitsize="32" type="code_ptr"/>
        <reg name="sp" bitsize="32" type="data_ptr"/>
        <reg name="gp" bitsize="32" type="data_ptr"/>
        <reg name="tp" bitsize="32" type="data_ptr"/>
        <reg name="t0" bitsize="32" type="int"/>
        <reg name="t1" bitsize="32" type="int"/>
        <reg name="t2" bitsize="32" type="int"/>
        <reg name="fp" bitsize="32" type="data_ptr"/>
        <reg name="s1" bitsize="32" type="int"/>
        <reg name="a0" bitsize="32" type="int"/>
        <reg name="a1" bitsize="32" type="int"/>
        <reg name="a2" bitsize="32" type="int"/>
        <reg name="a3" bitsize="32" type="int"/>
        <reg name="a4" bitsize="32" type="int"/>
        <reg name="a5" bitsize="32" type="int"/>
        <reg name="a6" bitsize="32" type="int"/>
        <reg name="a7" bitsize="32" type="int"/>
        <reg name="s2" bitsize="32" type="int"/>
        <reg name="s3" bitsize="32" type="int"/>
        <reg name="s4" bitsize="32" type="int"/>
        <reg name="s5" bitsize="32" type="int"/>
        <reg name="s6" bitsize="32" type="int"/>
        <reg name="s7" bitsize="32" type="int"/>
        <reg name="s8" bitsize="32" type="int"/>
        <reg name="s9" bitsize="32" type="int"/>
        <reg name="s10" bitsize="32" type="int"/>
        <reg name="s11" bitsize="32" type="int"/>
        <reg name="t3" bitsize="32" type="int"/>
        <reg name="t4" bitsize="32" type="int"/>
        <reg name="t5" bitsize="32" type="int"/>
        <reg name="t6" bitsize="32" type="int"/>
        <reg name="pc" bitsize="32" type="code_ptr"/>
    </feature>
</target>
"""
