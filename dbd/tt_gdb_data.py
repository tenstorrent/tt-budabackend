from dataclasses import dataclass
from functools import cached_property
from tt_debug_risc import RiscDebug

@dataclass
class GdbThreadId:
    process_id: int
    thread_id: int

    def to_gdb_string(self):
        return f"p{self.process_id}.{self.thread_id}"

@dataclass
class GdbProcess:
    process_id: int
    elf_path: str
    risc_debug: RiscDebug
    virtual_core_id: int
    core_type: str

    @cached_property
    def thread_id(self):
        return GdbThreadId(self.process_id, self.virtual_core_id)

    def __eq__(self, other):
        if not isinstance(other, GdbProcess):
            return NotImplemented
        return (self.process_id, self.elf_path) == (other.process_id, other.elf_path)

    def __hash__(self):
        return hash((self.process_id, self.elf_path))
