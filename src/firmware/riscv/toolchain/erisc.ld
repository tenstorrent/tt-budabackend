INCLUDE "erisc-memory.ld"

REGION_ALIAS("REGION_INIT_CODE", ERISC_INIT_CODE)
REGION_ALIAS("REGION_CODE", ERISC_CODE)
REGION_ALIAS("REGION_DATA", ERISC_DATA)
REGION_ALIAS("REGION_RODATA", ERISC_DATA)
REGION_ALIAS("REGION_LDM", LOCAL_DATA_MEM)
REGION_ALIAS("REGION_APP_CODE", ERISC_APP_CODE)

__firmware_start = ORIGIN(ERISC_INIT_CODE);
__firmware_stack_size = 1536;
__firmware_global_pointer = ORIGIN(ERISC_DATA) + 2048;

INCLUDE "erisc-sections.ld"
INCLUDE "tensix-address.ld"


