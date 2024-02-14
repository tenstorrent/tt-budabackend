
#include "ckernel.h"
#include "ckernel_addr_map.h"
#include "ckernel_pcbuf.h"
#include "fw_debug.h"
#include "ckernel_main.h"
#include "ckernel_globals.h"
#include <l1_address_map.h>
#include <tensix.h>
#ifdef PERF_DUMP
#include "ckernel_perf_unpack_pack.h"
#include "ckernel_perf_math.h"
#endif

namespace ckernel
{

enum class ttRiscCores : std::uint32_t { Unpack = 0, Math = 1, Pack = 2, Brisc = 3, Nrisc = 4};

volatile uint tt_reg_ptr *reg_base = reinterpret_cast<volatile uint tt_reg_ptr *>(0xFFB10000);
volatile uint tt_reg_ptr *pc_buf_base = reinterpret_cast<volatile uint tt_reg_ptr *>(PC_BUF_BASE);
volatile uint tt_reg_ptr *regfile = reinterpret_cast<volatile uint tt_reg_ptr *>(REGFILE_BASE);
volatile uint tt_reg_ptr *instrn_buffer = reinterpret_cast<volatile uint tt_reg_ptr *>(INSTRN_BUF_BASE);
volatile uint tt_reg_ptr *mailbox_base[4] = {
    reinterpret_cast<volatile uint tt_reg_ptr *>(TENSIX_MAILBOX0_BASE), reinterpret_cast<volatile uint tt_reg_ptr *>(TENSIX_MAILBOX1_BASE),
    reinterpret_cast<volatile uint tt_reg_ptr *>(TENSIX_MAILBOX2_BASE), reinterpret_cast<volatile uint tt_reg_ptr *>(TENSIX_MAILBOX3_BASE)
};
volatile uint tt_reg_ptr *dbg_event_scratch = nullptr;

uint32_t cfg_state_id __attribute__((section(".bss"))) = 0;  // Flip between 0 and 1 to keep state between kernel calls
uint32_t dest_offset_id __attribute__((section(".bss"))) = 0; // Flip between 0 and 1 to keep dest pointer between kernel calls

uint32_t dbg_event_index __attribute__((section(".bss"))) = 0;
uint32_t dbg_event_end __attribute__((section(".bss"))) = 0;
volatile uint16_t tt_reg_ptr * debug_mailbox_base = nullptr;
uint8_t mailbox_index = 0;
const uint8_t mailbox_end = 32;
volatile uint8_t tt_l1_ptr * debug_buffer = nullptr;
volatile uint8_t tt_l1_ptr * debug_buffer_start = nullptr;
uint8_t thread_id __attribute__((section(".bss"))) = 0;

#ifdef PERF_DUMP
uint32_t perf_index __attribute__((section(".bss"))) = 0;
uint32_t perf_end __attribute__((section(".bss"))) = 0;
volatile uint32_t tt_l1_ptr *perf_buf_base[2];
uint8_t perf_buf_base_id __attribute__((section(".bss"))) = 0;
bool record_perf_events __attribute__((section(".bss"))) = 0;
uint32_t perf_events_target_idx __attribute__((section(".bss"))) = 0;
uint16_t current_outer_loop_iter __attribute__((section(".bss"))) = 0;
int32_t dram_dump_req_local;
bool first_unpack_recorded __attribute__((section(".bss"))) = 0;
volatile uint tt_l1_ptr *ncrisc_ack_addr = nullptr;
uint32_t header;
#if OVERLAY_DECOUPLE == 1
uint8_t overlay_output_decouple_mask = 0;
inline void update_overlay_decoupling_mailbox() {
    overlay_output_decouple_mask = PERF_RISC_MAILBOX_OUTPUT_DECOUPLE_MASK_PTR[0] & 0xff;
    if (thread_id == 0 || thread_id == 1) {
        while(semaphore_read(semaphore::UNPACK_MATH_DONE) == 0) {}
    }
}
inline void reset_unpack_pack_sync() {
    if (thread_id == 2) {
        semaphore_get(semaphore::UNPACK_MATH_DONE);
    }
}
#endif
#endif

volatile uint tt_l1_ptr * trisc_l1_mailbox = reinterpret_cast<volatile uint tt_l1_ptr *>(MAILBOX_ADDR);

inline bool ready_for_next_epoch() {         // place this through compiler into a section that is not going to overwritten
    return true;
    // mailbox_write(ttRiscCores::Nrisc);              // signal done epoch to NCRisc
    // mailbox_read(ttRiscCores::Nrisc);               // This is blocking read, until NCrisc signals epoch is ready
}

inline void set_thread_id_parameter() {
    if ((uint)__firmware_start == (uint)l1_mem::address_map::TRISC0_BASE) {
        thread_id = 0;
    } else if ((uint) __firmware_start == (uint)l1_mem::address_map::TRISC1_BASE) {
        thread_id = 1;
    } else {
        thread_id = 2;
    }
}

inline void allocate_debug_mailbox_buffer() {
   std::int32_t debug_mailbox_addr;
   if ((uint32_t)__firmware_start == (uint32_t)l1_mem::address_map::TRISC0_BASE) {
      debug_mailbox_addr = l1_mem::address_map::DEBUG_MAILBOX_BUF_BASE + 0*l1_mem::address_map::DEBUG_MAILBOX_BUF_SIZE;
   } else if ((uint32_t) __firmware_start == (uint32_t)l1_mem::address_map::TRISC1_BASE) {
      debug_mailbox_addr = l1_mem::address_map::DEBUG_MAILBOX_BUF_BASE + 1*l1_mem::address_map::DEBUG_MAILBOX_BUF_SIZE;
   } else {
      debug_mailbox_addr = l1_mem::address_map::DEBUG_MAILBOX_BUF_BASE + 2*l1_mem::address_map::DEBUG_MAILBOX_BUF_SIZE;
   }
   debug_mailbox_base = reinterpret_cast<volatile uint16_t tt_l1_ptr *>(debug_mailbox_addr);
   clear_mailbox_values();

}

inline void allocate_debug_buffer() {
   std::int32_t debug_buffer_addr;
   if ((uint32_t)__firmware_start == (uint32_t)l1_mem::address_map::TRISC0_BASE) {
      debug_buffer_addr = l1_mem::address_map::TRISC0_DEBUG_BUFFER_BASE;
   } else if ((uint32_t) __firmware_start == (uint32_t)l1_mem::address_map::TRISC1_BASE) {
      debug_buffer_addr = l1_mem::address_map::TRISC1_DEBUG_BUFFER_BASE;
   } else {
      debug_buffer_addr = l1_mem::address_map::TRISC2_DEBUG_BUFFER_BASE;
   }
   debug_buffer = reinterpret_cast<volatile uint8_t tt_l1_ptr *>(debug_buffer_addr);
   debug_buffer[l1_mem::address_map::DEBUG_BUFFER_SIZE-1]=0x0;
   debug_buffer_start = debug_buffer;
}

__attribute__((noinline)) void debug_dump(const uint8_t *data, uint32_t byte_size) {
  for (uint32_t i = 0; i < byte_size; i++) {
    if ((((uint32_t) debug_buffer)&(l1_mem::address_map::DEBUG_BUFFER_SIZE-1)) == 
         l1_mem::address_map::DEBUG_BUFFER_SIZE-1) {
       *(debug_buffer) = 0xff; //overflow detected
    } else {
       *debug_buffer = data[i];
       debug_buffer++;
    }
  }  
}

__attribute__((noinline)) void debug_dump_seek(uint8_t offset) {
  debug_buffer = reinterpret_cast<volatile uint8_t *>(debug_buffer_start + offset);
}

} // namespace ckernel

void local_mem_copy() {
   volatile uint tt_l1_ptr *l1_local_mem_start_addr;
   volatile uint *local_mem_start_addr = (volatile uint*) LOCAL_MEM_BASE_ADDR;

   if ((uint)__firmware_start == (uint)l1_mem::address_map::TRISC0_BASE) {
      l1_local_mem_start_addr = (volatile uint tt_l1_ptr *)l1_mem::address_map::TRISC0_LOCAL_MEM_BASE;
   } else if ((uint) __firmware_start == (uint)l1_mem::address_map::TRISC1_BASE) {
      l1_local_mem_start_addr = (volatile uint tt_l1_ptr *)l1_mem::address_map::TRISC1_LOCAL_MEM_BASE;
   } else {
      l1_local_mem_start_addr = (volatile uint tt_l1_ptr *)l1_mem::address_map::TRISC2_LOCAL_MEM_BASE;
   }
   uint word_size = ((uint)__local_mem_rodata_end_addr - (uint)__local_mem_rodata_start_addr)>>2;

   if (word_size>0) {
      for (uint n=0;n<word_size;n++) {
         local_mem_start_addr[n] = l1_local_mem_start_addr[n];
      }
   }
}

using namespace ckernel;

int main(int argc, char *argv[])
{
    FWEVENT("Launching production env kernels");

    // Initialize GPRs to all 0s
    for (int i = 0; i < 64; i++)
        regfile[i] = 0;

    // Init L1 buffer with 1.0f (used for reduce max)
    union {
        float f;
        uint32_t u;
    } f2u = {.f = 1.0f};

    // Save a little code space.  GCC fails to remove the loop variable so loop with a ptr
#pragma GCC unroll 0
    for (volatile uint32_t tt_l1_ptr *ptr = l1_buffer; ptr < &l1_buffer[16]; *ptr++ = f2u.u) // Load const into L1 buffer

    reset_cfg_state_id();
    
    trisc_l1_mailbox_write(RESET_VAL);

    if ((uint)l1_mem::address_map::RISC_LOCAL_MEM_BASE == 
            ((uint)__local_mem_rodata_end_addr&0xfff00000))
    {
       local_mem_copy();
    }  

    allocate_debug_mailbox_buffer();
    allocate_debug_buffer();


#ifdef PERF_DUMP
    set_thread_id_parameter();
    allocate_perf_buffer();
    setup_fpu_perf_cnt();
    record_dummy_math_event();
#if OVERLAY_DECOUPLE == 1
    update_overlay_decoupling_mailbox();
#endif
#endif
  
    //while (ready_for_next_epoch())
    {
        run_kernel();
    }

    // Signal completion
    tensix_sync();
#ifdef PERF_DUMP
#if OVERLAY_DECOUPLE == 1
    reset_unpack_pack_sync();
#endif
    record_perf_dump_end_and_check_overflow();
    // There has to be a tensix_sync() before this last pass.
    last_trisc_perf_dump_to_dram();
    tensix_sync();
#endif

    trisc_l1_mailbox_write(KERNEL_COMPLETE);

}
