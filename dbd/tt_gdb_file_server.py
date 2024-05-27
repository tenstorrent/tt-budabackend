import os
from typing import Set

class GdbFileServer:
    def __init__(self):
        self.opened_files: Set[int] = set()

    def __del__(self):
        self.close_all()

    def close_all(self):
        # Close all opened files
        for fd in self.opened_files:
            try:
                os.close(fd)
            except:
                # Ignore exceptions
                pass
        self.opened_files.clear()

    def open(self, filename: str, flags: int, mode: int):
        try:
            fd = os.open(filename, flags, mode)
            self.opened_files.add(fd)
            return fd
        except OSError as e:
            return f"-1,{e.errno}"

    def close(self, fd: int):
        if fd in self.opened_files:
            try:
                os.close(fd)
                self.opened_files.remove(fd)
                return True
            except:
                pass
        return False

    def pread(self, fd: int, count: int, offset: int):
        if fd in self.opened_files:
            try:
                os.lseek(fd, offset, os.SEEK_SET)
                return os.read(fd, count)
            except OSError as e:
                return f"-1,{e.errno}"
        else:
            return "-1"

    def pwrite(self, fd: int, offset: int, data: bytes):
        if fd in self.opened_files:
            try:
                os.lseek(fd, offset, os.SEEK_SET)
                return os.write(fd, data)
            except OSError as e:
                return f"-1,{e.errno}"
        else:
            return "-1"
