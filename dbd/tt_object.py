# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import tt_util as util
from sortedcontainers import SortedSet


# Parent class for all objects (ops, queues, buffers, streams...)
class TTObject:
    def __str__(self):
        return f"{self.id()}:{type(self).__name__}"

    def __repr__(self):
        return self.__str__()

    def id(self):
        return self._id  # Assume children have the _id

    def __lt__(self, other):
        return self.id() < other.id()


# This class maps the object id to the object itself
class TTObjectIDDict(dict):
    def __str__(self):
        s = f"{type(self).__name__}:len={len(self)}"
        for k, v in self.items():
            s += f"\n  {k}: {v}"
        return s

    def __repr__(self):
        return self.__str__()

    def first(self):
        if len(self):
            return next(iter(self.values()))
        else:
            return None

    def keep(self, lam):
        for k in list(self.values()):
            if not lam(k):
                del self[k.id()]

    def remove(self, lam):
        self.keep(lambda x: not lam(x))

    def copy(self):
        return TTObjectIDDict(self)

    def add(self, o):
        assert isinstance(o, TTObject)
        self[o.id()] = o

    def intersection(self, other):
        return TTObjectIDDict({k: self[k] for k in self if k in other})

    def union(self, other):
        return TTObjectIDDict({**self, **other})

    def issubset(self, other):
        return all(k in other for k in self)

    def find_id(self, id):
        return self[id] if id in self else None


# Just an array of numbers that knows how to print itself
class DataArray(TTObject):
    def __str__(self):
        return f"{self._id}\n" + util.array_to_str(
            self.data, cell_formatter=self.cell_formatter
        )

    def __repr__(self):
        return f"{self._id} len:{len(self.data)} ({len(self.data)*self.bytes_per_entry} bytes)"

    def __init__(self, id, bytes_per_entry=4, cell_formatter=None):
        self._id = id
        self.data = []
        self.bytes_per_entry = bytes_per_entry
        if not cell_formatter:
            self.cell_formatter = util.CELLFMT.hex(self.bytes_per_entry)
        else:
            self.cell_formatter = cell_formatter

    def to_bytes_per_entry(self, bytes_per_entry):
        dest = bytes()
        for v in self.data:
            dest += int.to_bytes(v, length=self.bytes_per_entry, byteorder="little")
        self.data = []
        for i in range(0, len(dest), bytes_per_entry):
            self.data.append(
                int.from_bytes(dest[i : i + bytes_per_entry], byteorder="little")
            )
        self.bytes_per_entry = bytes_per_entry
        self.cell_formatter = util.CELLFMT.hex(self.bytes_per_entry)

    def from_bytes (self, bytes):
        """
        Initiliaze the data array from a byte array
        """
        self.data = []
        for i in range(0, len(bytes), self.bytes_per_entry):
            self.data.append(
                int.from_bytes(bytes[i : i + self.bytes_per_entry], byteorder="little")
            )
        return self

    def bytes (self):
        """
        Return the data array as a byte array
        """
        dest = bytes()
        for v in self.data:
            dest += int.to_bytes(v, length=self.bytes_per_entry, byteorder="little")
        return dest

    # subsript operator
    def __getitem__(self, key):
        return self.data[key]
