// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef ETH_SS_REGS_H
#define ETH_SS_REGS_H

///////////////
// ETH Params

#define NUM_ECC_SOURCES (5 + 4*3 + 2)
#define NUM_ETH_QUEUES  2


//////////////////
// RISC debug regs
#define ETH_RISC_REGS_START 0xFFB10000

#define ETH_RISC_RESET            0x21B0
#define ETH_RISC_WALL_CLOCK_0     0x21F0
#define ETH_RISC_WALL_CLOCK_1     0x21F4
#define ETH_RISC_WALL_CLOCK_1_AT  0x21F8

///////////////
// Synopsys MAC
#define ETH_MAC_REGS_START 0xFFBA0000
#define ETH_MAC_REGS_SIZE  0x10000


#define ETH_MAC_MTL_OPERATION_MODE 0x1000

///////////////
// Synopsys PCS
#define ETH_PCS_REGS_START 0xFFBB0000
#define ETH_PCS_REGS_SIZE  0x10000

#define ETH_PCS_IND_REG 0xFF

//////////////////////////////
// TX queue 0/1 controllers
#define ETH_TXQ0_REGS_START 0xFFB90000
#define ETH_TXQ_REGS_SIZE      0x1000
#define ETH_TXQ_REGS_SIZE_BIT  12

// TXQ_CTRL[0]: set to enable packet resend mode (must be set on both sides)
// TXQ_CTRL[1]: reserved, should be 0
// TXQ_CTRL[2]: 0 = use Length field, 1 = use Type field (from ETH_TXQ_ETH_TYPE)
// TXQ_CTRL[3]: set to disable drop notification, timeout-only for resend
#define ETH_TXQ_CTRL                     0x0
#define ETH_TXQ_CTRL_KEEPALIVE  (0x1 << 0)
#define ETH_TXQ_CTRL_USE_TYPE   (0x1 << 2)
#define ETH_TXQ_CTRL_DIS_DROP   (0x1 << 3)

// TXQ_CMD should be written as one-hot. 
// TXQ_CMD[0]: issue raw transfer (no resend)
// TXQ_CMD[1]: issue packet transfer (with resend)
// TXQ_CMD[2]: issue remote reg write (TXQ0 only)
// TXQ_CMD[3]: MAC queue flush
// TXQ_CMD read returns 1 if command ongoing, 0 if ready for next command.
#define ETH_TXQ_CMD                      0x4 
#define ETH_TXQ_CMD_START_RAW   (0x1 << 0)
#define ETH_TXQ_CMD_START_DATA  (0x1 << 1)
#define ETH_TXQ_CMD_START_REG   (0x1 << 2)
#define ETH_TXQ_CMD_FLUSH       (0x1 << 3)

#define ETH_TXQ_STATUS                   0x8   // IMPROVE: document (misc. internal bits for debug)
#define ETH_TXQ_MAX_PKT_SIZE_BYTES       0xC   // Max ethernet payload size (default = 1500 bytes)
#define ETH_TXQ_BURST_LEN                0x10  // Value to drive on ati_q#_pbl output (default = 8)
#define ETH_TXQ_TRANSFER_START_ADDR      0x14  // Start source address (byte address, should be 16-byte aligned for packet transfers)
#define ETH_TXQ_TRANSFER_SIZE_BYTES      0x18  // Transfer size in bytes (should be multiple of 16 for packet transfers)
#define ETH_TXQ_DEST_ADDR                0x1C  // Remote destination address for (packet/register transfer only, should be 16-byte aligned)
#define ETH_TXQ_CTRL_WORD                0x20  // Reserved
#define ETH_TXQ_TRANSFER_CNT             0x30  // Number of issued transfers
#define ETH_TXQ_PKT_START_CNT            0x34  // Number of issued packets
#define ETH_TXQ_PKT_END_CNT              0x3C  // Number of sent packets
#define ETH_TXQ_WORD_CNT                 0x40  // Number of send 16-byte words
#define ETH_TXQ_REMOTE_REG_DATA          0x44  // Write data for remote register write
#define ETH_TXQ_REMOTE_SEQ_TIMEOUT       0x48  // Timeout for resend if no sequence number acks received
#define ETH_TXQ_LOCAL_SEQ_UPDATE_TIMEOUT 0x4C  // Timeout for sending sequence number update packet if no other traffic issued
#define ETH_TXQ_DEST_MAC_ADDR_HI         0x50  // Destination MAC address [47:32]
#define ETH_TXQ_DEST_MAC_ADDR_LO         0x54  // Destination MAC address [31:0]
#define ETH_TXQ_SRC_MAC_ADDR_HI          0x58  // Source MAC address [47:32]
#define ETH_TXQ_SRC_MAC_ADDR_LO          0x5C  // Source MAC address [31:0]
#define ETH_TXQ_ETH_TYPE                 0x60  // Type field for outgoing packets (used if TXQ_CTRL[2]=1) 
#define ETH_TXQ_MIN_PACKET_SIZE_WORDS    0x64  // Minimal packet size (in 16-byte words); padding added (and dropped at destination) for smaller packets
#define ETH_TXQ_RESEND_CNT               0x68  // Number of resend start events
#define ETH_TXQ_DATA_PACKET_ACCEPT_AHEAD 0x6C  // Number of packets to accept before previous ones sent

//////////////////////////////
// RX queue 0/1 controllers
#define ETH_RXQ0_REGS_START 0xFFB92000
#define ETH_RXQ_REGS_SIZE      0x1000
#define ETH_RXQ_REGS_SIZE_BIT  12

// RXQ_CTRL[0]: reserved, should be 0
// RXQ_CTRL[1]: 0 = raw data receiving mode, 1 = packet receiving mode
// RXQ_CTRL[2]: set to enable buffer pointer wrapping for raw data mode
// RXQ_CTRL[3]: set to force back-pressure on MAC incoming data (should be 0 in normal operation)
#define ETH_RXQ_CTRL                    0x0
#define ETH_RXQ_CTRL_PACKET_MODE     (0x1 << 1)
#define ETH_RXQ_CTRL_BUF_WRAP        (0x1 << 2)
#define ETH_RXQ_CTRL_FORCE_BPRESSURE (0x1 << 3)

#define ETH_RXQ_STATUS                  0x4   // IMPROVE: document (misc. internal bits for debug)
#define ETH_RXQ_BUF_PTR                 0x8   // Current byte buffer pointer (auto-incrementing) for raw data mode (If polling for data receive, also check ETH_RXQ_OUTSTANDING_WR_CNT to ensure the data is commited to L1)
#define ETH_RXQ_BUF_START_WORD_ADDR     0xC   // Word (16B, aligned) start address of receive buffer for raw data mode
#define ETH_RXQ_BUF_SIZE_WORDS          0x10  // Size (16B words) of receive buffer for raw data mode
#define ETH_RXQ_WORD_CNT                0x14  // Number of received 16B words (with frame overheads) (Wait 20 cycles after reading this value to ensure the data is commited to L1)
#define ETH_RXQ_BURST_LEN               0x1C  // Value to drive on ari_q#_pbl output (default 0)
#define ETH_RXQ_PKT_FLUSH               0x20  // Write 1 to flush MAC receive queue
#define ETH_RXQ_PKT_START_CNT           0x24  // Number of Ethernet frame start received
#define ETH_RXQ_PKT_END_CNT             0x28  // Number of Ethernet frame end received
#define ETH_RXQ_RXSTATUS                0x30  // 0x30-0x3C = 128-bit rxstatus value received for the last Ethernet frame
#define ETH_RXQ_LOCAL_RX_SEQ_NUM        0x40  // Current receive sequence number (i.e., last sequence number received in order)
#define ETH_RXQ_REMOTE_RX_SEQ_NUM       0x44  // Current remote sequence number (i.e., last sequence number acked by the remote side)
#define ETH_RXQ_TILE_HEADER_FORMAT_REG  0x48  // Tile header format for overlay snoop: [13:7] = bit width of tile size field (default = 16), [6:0] = byte offset of tile size field (default = 0)
#define ETH_RXQ_PACKET_DROP_CNT         0x4C  // Number of dropped packets
#define ETH_RXQ_OUTSTANDING_WR_CNT      0x50  // Number of issued L1 writes that have not been acked yet. Should be checked together with ETH_RXQ_BUF_PTR to make sure that data at the current pointer have been committed. 

//////////////////////////
// Misc. control registers
#define ETH_CTRL_REGS_START 0xFFB94000
#define ETH_CTRL_REGS_SIZE      0x1000

#define ETH_CTRL_ETH_RESET  0x0   // Ethernet block resets (all 0, i.e. asserted by default)
#define   ETH_SOFT_RESETN            (0x1 << 0)
#define   ETH_MAC_SOFT_PWR_ON_RESETN (0x1 << 1) 
#define   ETH_PCS_SOFT_PWR_ON_RESETN (0x1 << 2)
#define   ETH_MAC_SOFT_RESETN        (0x1 << 3) 
#define   ETH_PHY_SOFT_RESETN        (0x1 << 4) 

// IMPROVE: document status/ctrl registers (misc. internal bits for debug and overrides for constant signals, should not be used in normal operation)
#define ETH_CTRL_MAC_STATUS 0x4
#define ETH_CTRL_MAC_CTRL   0x8
#define ETH_CTRL_PCS_STATUS 0xC
#define ETH_CTRL_PCS_CTRL   0x10
#define ETH_CTRL_PHY_STATUS 0x14
#define ETH_CTRL_PHY_CTRL   0x18
#define ETH_CTRL_ECC_PAR_EN 0x1C
#define ETH_CTRL_ECC_INT_EN 0x20
#define ETH_CTRL_ECC_CLR    0x24
#define ETH_CTRL_ECC_FORCE  0x28
#define ETH_CTRL_ECC_STATUS 0x2C
#define ETH_CTRL_ECC_CNT    0x30 // Has NUM_ECC_SOURCES 32-bit registers
#define ETH_CTRL_MISC_CTRL  (0x30 + NUM_ECC_SOURCES*4)
#define ETH_CTRL_TEST_SUM   ((0x30 + NUM_ECC_SOURCES*4) + 0x4) // Reset at 0, auto-increments by each value written (test-only)
#define ETH_CTRL_SCRATCH0   ((0x30 + NUM_ECC_SOURCES*4) + 0x8) // Scratch/spare
#define ETH_CTRL_SCRATCH1   ((0x30 + NUM_ECC_SOURCES*4) + 0xC) // Scratch/spare
#define ETH_CTRL_SCRATCH2   ((0x30 + NUM_ECC_SOURCES*4) + 0x10) // Scratch/spare
#define ETH_CTRL_SCRATCH3   ((0x30 + NUM_ECC_SOURCES*4) + 0x14) // Scratch/spare
#define ETH_CORE_CTRL       ((0x30 + NUM_ECC_SOURCES*4) + 0x18) // {ECC_SCRUBBER_Enable, ECC_SCRUBBER_Scrub_On_Error, ECC_SCRUBBER_Scrub_On_Error_Immediately, ECC_SCRUBBER_Delay[10:0]}
#define ETH_CORE_IRAM_LOAD  ((0x30 + NUM_ECC_SOURCES*4) + 0x1C) // Write to start NCRISC IRAM load. Write value: word address for the start of binary in L1. Read value: bit 0 = status (1=ongoing, 0=complete), bits [17:1] = currend read address.
////


#endif

