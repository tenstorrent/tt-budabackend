MEMORY
{
    LOCAL_DATA_MEM : ORIGIN = 0xFFB00000, LENGTH = 4K
    LOCAL_DATA_MEM_TRISC : ORIGIN = 0xFFB00000, LENGTH = 2K
    LOCAL_DATA_MEM_NOCRISC : ORIGIN = 0xFFB00000, LENGTH = 4K
    L1 : ORIGIN = 0x00000000, LENGTH = 512K
    BRISC_CODE : ORIGIN = 0x00000000, LENGTH = 8448
    ZEROS : ORIGIN = 0x00002100, LENGTH = 512
    ETH_L1 : ORIGIN = 0x00000000, LENGTH = 256K
    /* CKernel code region
     = CA::KERNEL_DEFAULT_SIZE_PER_CORE */
    TRISC0_CODE : ORIGIN = __trisc_base, LENGTH = DEFINED(__trisc0_size) ? DEFINED(__trisc_local_mem_size) ? __trisc0_size - __trisc_local_mem_size : __trisc0_size : 16K
    TRISC1_CODE : ORIGIN = DEFINED(__trisc0_size) ? __trisc_base + __trisc0_size : 0x0000AC00, LENGTH = DEFINED(__trisc1_size) ? DEFINED(__trisc_local_mem_size) ? __trisc1_size - __trisc_local_mem_size : __trisc1_size : 16K
    TRISC2_CODE : ORIGIN = DEFINED(__trisc1_size) ? __trisc_base + __trisc0_size + __trisc1_size : 0x0000EC00, LENGTH = DEFINED(__trisc2_size) ? DEFINED(__trisc_local_mem_size) ? __trisc2_size - __trisc_local_mem_size : __trisc2_size : 16K
    /* Ckernel migrated code region
     = CA::KERNEL_DEFAULT_SIZE_PER_CORE */
    TRISC0_OVRD_CODE : ORIGIN = 0x000F4000, LENGTH = 16K
    TRISC1_OVRD_CODE : ORIGIN = 0x000F8000, LENGTH = 16K
    TRISC2_OVRD_CODE : ORIGIN = 0x000FC000, LENGTH = 16K
    NOCRISC_CODE : ORIGIN = 0x5000, LENGTH = 32K
    NOCRISC_DATA : ORIGIN = 0xE000, LENGTH = 4K                 /* To check if this section is ever used */
    NOCRISC_L1_SCRATCH :  ORIGIN = 0xE200, LENGTH = 3584        /* LENGTH = 4K - 0x200 */
    ERISC_CODE : ORIGIN = 0x00000000, LENGTH = 16K
    ERISC_DATA : ORIGIN = 0x00004000, LENGTH = 4K
    ERISC_APP_DATA : ORIGIN = 0x0003F000, LENGTH = 4K
}
