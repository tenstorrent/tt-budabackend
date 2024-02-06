INVALID_ADDRESS = -1
INVALID_CHANNEL = -1

TILE_SIZE = {
    "Float32"   : 32 * 32 * 4 + 32,
    "Tf32"      : 32 * 32 * 4 + 32,
    "Float16"   : 32 * 32 * 2 + 32,
    "Bfp8"      : 32 * 32 + 64 + 32,
    "Bfp4"      : 512 + 64 + 32,
    "Bfp2"      : 256 + 64 + 32,
    "Float16_b" : 32 * 32 * 2 + 32,
    "Bfp8_b"    : 32 * 32 + 64 + 32,
    "Bfp4_b"    : 512 + 64 + 32,
    "Bfp2_b"    : 256 + 64 + 32,
    "Lf8"       : 32 * 32 + 32,
    "UInt16"    : 32 * 32 * 2 + 32,
    "Int8"      : 32 * 32 + 32
}

def get_dram_queue_buffer_size(mblock, ublock, t, data_format, entries):
    return entries * mblock[0] * mblock[1] * ublock[0] * ublock[1]* t * TILE_SIZE[data_format] * 2 + 32

class DramAllocation:
    def __init__(self, address, size) -> None:
        self.__address = address
        self.__size = size

    def get_address(self):
        return self.__address

    def get_size(self):
        return self.__size

class DramChannel:
    def __init__(self, size) -> None:
        self.__size = size
        self.__allocations = []

    def set_allocation(self, address, size):
        if (address >= self.__size or address + size > self.__size or size < 0 or address<0):
            return INVALID_ADDRESS

        for i in range(len(self.__allocations)):
            allocation = self.__allocations[i]
            if (allocation.get_address() > address):
                if (allocation.get_address() >= address + size):
                    self.__allocations.insert(i, DramAllocation(address, size))
                    return address
                else:
                    return INVALID_ADDRESS
            if (allocation.get_address() + allocation.get_size() > address):
                return INVALID_ADDRESS

        self.__allocations.append(DramAllocation(address, size))
        return address

    def allocate(self, size):
        if (size < 0 or size > self.__size):
            return INVALID_ADDRESS

        previous_empty = 0
        for i in range(len(self.__allocations)):
            allocation = self.__allocations[i]
            if (allocation.get_address() - previous_empty >= size):
                self.__allocations.insert(i, DramAllocation(previous_empty, size))
                return previous_empty
            previous_empty = allocation.get_address() + allocation.get_size()

        if (previous_empty + size <= self.__size):
            self.__allocations.append(DramAllocation(previous_empty, size))
            return previous_empty

        return INVALID_ADDRESS

    def get_allocations(self):
        return self.__allocations

class Dram:
    def __init__(self, channel_cnt, size) -> None:
        self.__dram_channels = [DramChannel(size) for i in range(channel_cnt)]
        self.__channel_cnt = channel_cnt

    def set_allocation(self, channel, address, size):
        return self.__dram_channels[channel].set_allocation(address, size)

    def allocate(self, size):
        for i in range(self.__channel_cnt):
            address = self.__dram_channels[i].allocate(size)
            if (address != INVALID_ADDRESS):
                return i, address
        return INVALID_CHANNEL, INVALID_ADDRESS

    def get_dram_channels(self):
        return self.__dram_channels

    def get_channel_cnt(self):
        return self.__channel_cnt

class DramFactory:
    def create_grayskull_dram():
        dram = Dram(8, 1024*1024*1024)
        for i in range(8):
            dram.set_allocation(i, 0, 256 * 1024 * 1024)
        return dram
