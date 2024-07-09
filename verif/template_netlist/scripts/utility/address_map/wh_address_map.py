from dataclasses import dataclass

from address_map import AddressMap
from memory_region import MemoryRegion


@dataclass(frozen=True)
class WHAddressMap(AddressMap):
    TRISC0 = 0
    TRISC1 = 1
    TRISC2 = 2
    NCRISC = 3
    BRISC = 4
    FIRMWARE_SIZE = 20 * 1024
    L1_BARRIER_SIZE = 0x20
    BRISC_FIRMWARE_SIZE = 7 * 1024 + 512 + 768
    ZEROS_SIZE = 512
    NCRISC_FIRMWARE_SIZE = 32 * 1024
    TRISC0_SIZE = 20 * 1024
    TRISC1_SIZE = 16 * 1024
    TRISC2_SIZE = 20 * 1024
    TRISC_LOCAL_MEM_SIZE = 4 * 1024
    NCRISC_LOCAL_MEM_SIZE = 4 * 1024
    NCRISC_L1_SCRATCH_SIZE = 4 * 1024  # TODO this is not good, in tensix-memory.ld this is 4K - 0x200
    NCRISC_L1_CODE_SIZE = 16 * 1024
    NCRISC_IRAM_CODE_SIZE = 16 * 1024
    NCRISC_DATA_SIZE = 4 * 1024
    EPOCH_RUNTIME_CONFIG_SIZE = 128
    OVERLAY_BLOB_SIZE = (64 * 1024) - EPOCH_RUNTIME_CONFIG_SIZE
    TILE_HEADER_BUF_SIZE = 32 * 1024
    NCRISC_L1_EPOCH_Q_SIZE = 32
    FW_L1_BLOCK_SIZE = (
        FIRMWARE_SIZE
        + NCRISC_FIRMWARE_SIZE
        + TRISC0_SIZE
        + TRISC1_SIZE
        + TRISC2_SIZE
        + OVERLAY_BLOB_SIZE
        + EPOCH_RUNTIME_CONFIG_SIZE
        + TILE_HEADER_BUF_SIZE
    )
    FIRMWARE_BASE = 0
    L1_BARRIER_BASE = 0x16DFC0
    ZEROS_BASE = FIRMWARE_BASE + BRISC_FIRMWARE_SIZE
    NCRISC_FIRMWARE_BASE = FIRMWARE_BASE + FIRMWARE_SIZE
    NCRISC_L1_CODE_BASE = NCRISC_FIRMWARE_BASE + NCRISC_IRAM_CODE_SIZE
    NCRISC_LOCAL_MEM_BASE = NCRISC_FIRMWARE_BASE + NCRISC_FIRMWARE_SIZE - NCRISC_LOCAL_MEM_SIZE
    NCRISC_L1_SCRATCH_BASE = NCRISC_FIRMWARE_BASE + 0x200
    NCRISC_L1_CONTEXT_BASE = NCRISC_FIRMWARE_BASE + 0x20
    NCRISC_L1_DRAM_POLLING_CTRL_BASE = NCRISC_FIRMWARE_BASE + 0x40
    NCRISC_PERF_QUEUE_HEADER_SIZE = 8 * 8
    NCRISC_PERF_QUEUE_HEADER_ADDR = NCRISC_FIRMWARE_BASE + NCRISC_L1_SCRATCH_SIZE
    NCRISC_L1_PERF_BUF_BASE = NCRISC_PERF_QUEUE_HEADER_ADDR + NCRISC_PERF_QUEUE_HEADER_SIZE
    NCRISC_PERF_BUF_SIZE_LEVEL_0 = 640
    NCRISC_PERF_BUF_SIZE_LEVEL_1 = 4 * 1024 - NCRISC_PERF_QUEUE_HEADER_SIZE
    NCRISC_L1_EPOCH_Q_BASE = NCRISC_L1_PERF_BUF_BASE + NCRISC_PERF_BUF_SIZE_LEVEL_1
    TRISC_BASE = NCRISC_FIRMWARE_BASE + NCRISC_FIRMWARE_SIZE
    TRISC0_BASE = NCRISC_FIRMWARE_BASE + NCRISC_FIRMWARE_SIZE
    TRISC0_LOCAL_MEM_BASE = TRISC0_BASE + TRISC0_SIZE - TRISC_LOCAL_MEM_SIZE
    TRISC1_BASE = TRISC0_BASE + TRISC0_SIZE
    TRISC1_LOCAL_MEM_BASE = TRISC1_BASE + TRISC1_SIZE - TRISC_LOCAL_MEM_SIZE
    TRISC2_BASE = TRISC1_BASE + TRISC1_SIZE
    TRISC2_LOCAL_MEM_BASE = TRISC2_BASE + TRISC2_SIZE - TRISC_LOCAL_MEM_SIZE
    EPOCH_RUNTIME_CONFIG_BASE = TRISC2_BASE + TRISC2_SIZE + TILE_HEADER_BUF_SIZE
    OVERLAY_BLOB_BASE = EPOCH_RUNTIME_CONFIG_BASE + EPOCH_RUNTIME_CONFIG_SIZE
    DATA_BUFFER_SPACE_BASE = EPOCH_RUNTIME_CONFIG_BASE + EPOCH_RUNTIME_CONFIG_SIZE + OVERLAY_BLOB_SIZE
    TRISC_L1_MAILBOX_OFFSET = 4
    BRISC_L1_MAILBOX_OFFSET = 4
    NRISC_L1_MAILBOX_OFFSET = 4
    TRISC0_MAILBOX_BASE = TRISC0_BASE + TRISC_L1_MAILBOX_OFFSET
    TRISC1_MAILBOX_BASE = TRISC1_BASE + TRISC_L1_MAILBOX_OFFSET
    TRISC2_MAILBOX_BASE = TRISC2_BASE + TRISC_L1_MAILBOX_OFFSET
    FW_MAILBOX_BASE = 32
    DEBUG_MAILBOX_BUF_BASE = 112
    FW_MAILBOX_BUF_SIZE = 64
    DEBUG_MAILBOX_BUF_SIZE = 64
    TRISC_TT_LOG_MAILBOX_OFFSET = 28
    TRISC_TT_LOG_MAILBOX_SIZE = 64
    TRISC0_TT_LOG_MAILBOX_BASE = TRISC0_MAILBOX_BASE + TRISC_TT_LOG_MAILBOX_OFFSET
    TRISC1_TT_LOG_MAILBOX_BASE = TRISC1_MAILBOX_BASE + TRISC_TT_LOG_MAILBOX_OFFSET
    TRISC2_TT_LOG_MAILBOX_BASE = TRISC2_MAILBOX_BASE + TRISC_TT_LOG_MAILBOX_OFFSET
    DEBUG_BUFFER_SIZE = 2 * 1024
    TRISC0_DEBUG_BUFFER_BASE = TRISC0_LOCAL_MEM_BASE + DEBUG_BUFFER_SIZE
    TRISC1_DEBUG_BUFFER_BASE = TRISC1_LOCAL_MEM_BASE + DEBUG_BUFFER_SIZE
    TRISC2_DEBUG_BUFFER_BASE = TRISC2_LOCAL_MEM_BASE + DEBUG_BUFFER_SIZE
    MAX_SIZE = 1499136
    MAX_L1_LOADING_SIZE = 1 * 1024 * 1024
    RISC_LOCAL_MEM_BASE = 0xFFB00000
    NCRISC_IRAM_MEM_BASE = 0xFFC00000
    NCRISC_HAS_IRAM = 1
    PERF_BUF_SIZE = FIRMWARE_SIZE - BRISC_FIRMWARE_SIZE - ZEROS_SIZE
    PERF_TOTAL_SETUP_BUFFER_SIZE = 64
    PERF_QUEUE_HEADER_SIZE = 16
    PERF_RISC_MAILBOX_SIZE = 8
    PERF_RESET_PTR_MAILBOX_SIZE = 4
    PERF_ANALYZER_COMMAND_START_PTR_SIZE = 8
    PERF_ANALYZER_COMMAND_START_VAL_SIZE = 4
    PERF_UNUSED_SIZE = 24
    MATH_PERF_BUF_SIZE = 64
    BRISC_PERF_BUF_SIZE = 640
    UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 = 640
    UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 = (
        (12 * 1024 - 768) // 2
        - MATH_PERF_BUF_SIZE // 2
        - (PERF_TOTAL_SETUP_BUFFER_SIZE) // 2
        - BRISC_PERF_BUF_SIZE // 2
    )
    PERF_QUEUE_HEADER_ADDR = FIRMWARE_BASE + BRISC_FIRMWARE_SIZE + ZEROS_SIZE
    PERF_RISC_MAILBOX_ADDR = PERF_QUEUE_HEADER_ADDR + PERF_QUEUE_HEADER_SIZE
    PERF_RESET_PTR_MAILBOX_ADDR = PERF_RISC_MAILBOX_ADDR + PERF_RISC_MAILBOX_SIZE
    PERF_ANALYZER_COMMAND_START_PTR_ADDR = PERF_RESET_PTR_MAILBOX_ADDR + PERF_RESET_PTR_MAILBOX_SIZE
    PERF_ANALYZER_COMMAND_START_VAL_ADDR = PERF_ANALYZER_COMMAND_START_PTR_ADDR + PERF_ANALYZER_COMMAND_START_PTR_SIZE
    BRISC_PERF_BUF_BASE_ADDR = (
        PERF_ANALYZER_COMMAND_START_VAL_SIZE + PERF_UNUSED_SIZE + PERF_ANALYZER_COMMAND_START_VAL_ADDR
    )
    MATH_PERF_BUF_BASE_ADDR = BRISC_PERF_BUF_BASE_ADDR + BRISC_PERF_BUF_SIZE
    UNPACK_PACK_PERF_BUF_BASE_ADDR = MATH_PERF_BUF_BASE_ADDR + MATH_PERF_BUF_SIZE
    PERF_NUM_THREADS = 5
    PERF_QUEUE_PTRS = PERF_QUEUE_HEADER_ADDR
    PERF_THREAD_HEADER = PERF_QUEUE_PTRS + 8
    PERF_WR_PTR_COPY = PERF_QUEUE_PTRS + 12
    WALL_CLOCK_L = 0xFFB121F0
    WALL_CLOCK_H = 0xFFB121F8

    def get_alignment(self):
        return 32

    def get_figname(self):
        return "wh_l1_address_map.png"

    def get_regions(self):
        # Declare regions of interest to plot. Comment out some of them to get a clear view of smaller regions.
        regions = [
            MemoryRegion("FIRMWARE", self.FIRMWARE_BASE, self.FIRMWARE_SIZE),
            MemoryRegion("BRISC_FIRMWARE", self.FIRMWARE_BASE, self.BRISC_FIRMWARE_SIZE),
            MemoryRegion("ZEROS", self.ZEROS_BASE, self.ZEROS_SIZE),
            MemoryRegion("NCRISC_FIRMWARE", self.NCRISC_FIRMWARE_BASE, self.NCRISC_FIRMWARE_SIZE),
            MemoryRegion("NCRISC_L1_CODE", self.NCRISC_L1_CODE_BASE, self.NCRISC_L1_CODE_SIZE),
            MemoryRegion("NCRISC_LOCAL_MEM", self.NCRISC_LOCAL_MEM_BASE, self.NCRISC_LOCAL_MEM_SIZE),
            MemoryRegion("NCRISC_L1_SCRATCH", self.NCRISC_L1_SCRATCH_BASE, self.NCRISC_L1_SCRATCH_SIZE - 0x200),
            MemoryRegion(
                "NCRISC_PERF_QUEUE_HEADER",
                self.NCRISC_PERF_QUEUE_HEADER_ADDR,
                self.NCRISC_PERF_QUEUE_HEADER_SIZE,
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            MemoryRegion(
                "NCRISC_L1_PERF_BUF",
                self.NCRISC_L1_PERF_BUF_BASE,
                max(self.NCRISC_PERF_BUF_SIZE_LEVEL_0, self.NCRISC_PERF_BUF_SIZE_LEVEL_1),
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            MemoryRegion("TRISC", self.TRISC_BASE, self.TRISC0_SIZE + self.TRISC1_SIZE + self.TRISC2_SIZE),
            MemoryRegion("TRISC0", self.TRISC0_BASE, self.TRISC0_SIZE, start_alignment=self.get_alignment()),
            MemoryRegion("TRISC1", self.TRISC1_BASE, self.TRISC1_SIZE, start_alignment=self.get_alignment()),
            MemoryRegion("TRISC2", self.TRISC2_BASE, self.TRISC2_SIZE, start_alignment=self.get_alignment()),
            MemoryRegion(
                "TILE_HEADER_BUFFER",
                self.TRISC2_BASE + self.TRISC2_SIZE,
                self.TILE_HEADER_BUF_SIZE,
                start_alignment=self.get_alignment(),
            ),
            MemoryRegion("EPOCH_RUNTIME_CONFIG", self.EPOCH_RUNTIME_CONFIG_BASE, self.EPOCH_RUNTIME_CONFIG_SIZE),
            MemoryRegion("OVERLAY_BLOB", self.OVERLAY_BLOB_BASE, self.OVERLAY_BLOB_SIZE),
            MemoryRegion(
                "DATA_BUFFERS", self.DATA_BUFFER_SPACE_BASE, 20480
            ),  # NOTE end is actually self.MAX_SIZE but it is too large to plot
            MemoryRegion(
                "EXTRA_OVERLAY_BLOB", self.DATA_BUFFER_SPACE_BASE, 10240
            ),  # NOTE extra overlay blob is allocated dynamically when needed, here it is drawn just for illustration
            MemoryRegion("PERF_BUF", self.PERF_QUEUE_HEADER_ADDR, self.PERF_BUF_SIZE),
            MemoryRegion(
                "PERF_QUEUE_HEADER",
                self.PERF_QUEUE_HEADER_ADDR,
                self.PERF_QUEUE_HEADER_SIZE,
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            MemoryRegion(
                "BRISC_PERF_BUF",
                self.BRISC_PERF_BUF_BASE_ADDR,
                self.BRISC_PERF_BUF_SIZE,
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            MemoryRegion(
                "MATH_PERF_BUF",
                self.MATH_PERF_BUF_BASE_ADDR,
                self.MATH_PERF_BUF_SIZE,
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            MemoryRegion(
                "UNPACK_PACK_PERF_BUF",
                self.UNPACK_PACK_PERF_BUF_BASE_ADDR,
                max(self.UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0, self.UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1),
                start_alignment=self.get_alignment(),
                size_alignment=2 * self.get_alignment(),
            ),
            # from tensix-memory.ld
            MemoryRegion("NOCRISC_DATA", 0x5000, size=4 * 1024),
            # MemoryRegion("NOCRISC_CODE", 0xFFC00000, size=16 * 1024),  # this is in NCRISC L0
            # MemoryRegion("NOCRISC_L1_CODE", 0x9000, size=16 * 1024),  # duplicate of NCRISC_L1_CODE
            # MemoryRegion("NOCRISC_L1_SCRATCH", 0x5200, size=3584), # duplicate of NCRISC_L1_SCRATCH
        ]

        if self.NCRISC_HAS_IRAM:
            regions.append(
                MemoryRegion("NCRISC_IRAM_CODE", self.NCRISC_FIRMWARE_BASE, self.NCRISC_IRAM_CODE_SIZE),
            )

        self.find_overlaps(regions)
        return self.sort(regions)
