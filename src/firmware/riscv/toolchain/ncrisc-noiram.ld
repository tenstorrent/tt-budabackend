INCLUDE "tensix-memory-noiram.ld"

REGION_ALIAS("REGION_CODE", NOCRISC_CODE)
REGION_ALIAS("REGION_L1_CODE", NOCRISC_CODE) /* all NCRISC code is in L1 when noiram */
REGION_ALIAS("REGION_DATA", LOCAL_DATA_MEM_NOCRISC)
REGION_ALIAS("REGION_L1_DATA", NOCRISC_L1_SCRATCH)
REGION_ALIAS("REGION_RODATA", LOCAL_DATA_MEM_NOCRISC)
REGION_ALIAS("REGION_LDM", LOCAL_DATA_MEM_NOCRISC)

__firmware_start = ORIGIN(NOCRISC_CODE);
__firmware_stack_size = 976;

INCLUDE "ncrisc-sections.ld"
INCLUDE "tensix-address.ld"

