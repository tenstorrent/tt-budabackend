// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef ETH_SS_H
#define ETH_SS_H

#include <stdint.h>

#include "eth_ss_regs.h"

uint64_t eth_mac_get_num_crc_err();



////


void eth_send_raw(uint32_t q_num, uint32_t src_addr, uint32_t num_bytes, uint64_t dest_mac_addr);

void eth_send_packet(uint32_t q_num, uint32_t src_word_addr, uint32_t dest_word_addr, uint32_t num_words);

////

void eth_ecc_en(uint32_t irqs_en, uint32_t paritys_en);

void eth_ecc_clr(uint32_t irqs_to_clr);

void eth_ecc_force(uint32_t irqs_to_force);

uint32_t eth_ecc_get_status();

uint32_t eth_ecc_get_count(uint32_t source_idx);

bool eth_no_ecc_errors_detected();

#endif

