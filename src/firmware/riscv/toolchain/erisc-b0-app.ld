INCLUDE "erisc-b0-memory.ld"

REGION_ALIAS("REGION_APP_IRAM_CODE", ERISC_APP_IRAM_CODE)
REGION_ALIAS("REGION_APP_CODE", ERISC_APP_CODE)
REGION_ALIAS("REGION_APP_DATA", ERISC_APP_DATA)
REGION_ALIAS("REGION_LDM", LOCAL_DATA_MEM)

__firmware_start = ORIGIN(ERISC_CODE);
__firmware_stack_size = 1024;
__firmware_global_pointer = ORIGIN(ERISC_DATA) + 2048;


INCLUDE "erisc-b0-app-sections.ld"
INCLUDE "tensix-address.ld"


