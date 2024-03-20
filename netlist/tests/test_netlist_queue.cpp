// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>

#include "netlist/netlist.hpp"
int main() {
    tt_io_entry inputs = new tt_tensor();
    netlist_queue test_queue_1(1, "test_1");
    log_info(tt::LogTest, "Testing entries {} ", 1);
    for (int i = 0; i < 5; i++) {
        inputs->randomize();
        log_info(tt::LogTest, "Pushing input {}", i);
        test_queue_1.push(inputs);
        log_info(tt::LogTest, "Peeking input {}", i);
        test_queue_1.set_local_rd_ptr(i % test_queue_1.entries);
        test_queue_1.peek();
        log_info(tt::LogTest, "Popping input {}", i);
        test_queue_1.pop();
    }
    netlist_queue test_queue_2(2, "test_2");
    log_info(tt::LogTest, "Testing entries {} ", 2);
    for (int i = 0; i < 5; i++) {
        inputs->randomize();
        log_info(tt::LogTest, "Pushing input {}", i);
        test_queue_2.push(inputs);
        log_info(tt::LogTest, "Peeking input {}", i);
        test_queue_2.set_local_rd_ptr(i % test_queue_2.entries);
        test_queue_2.peek();
        log_info(tt::LogTest, "Popping input {}", i);
        test_queue_2.pop();
    }
    netlist_queue test_queue_3(2, "test_3");
    log_info(tt::LogTest, "Testing entries {} ", 2);
    for (int n = 0; n < 5; n++) {
        log_info(tt::LogTest, "Looping -- Loop index{}", n);
        for (int i = 0; i < 2; i++) {
            inputs->randomize();
            log_info(tt::LogTest, "Pushing input {}", i);
            test_queue_3.push(inputs);
        }
        for (int i = 0; i < 2; i++) {
            log_info(tt::LogTest, "Peeking input {}", i);
            test_queue_3.set_local_rd_ptr(i % test_queue_3.entries);
            test_queue_3.peek();
            log_info(tt::LogTest, "Popping input {}", i);
            test_queue_3.pop();
        }
    }
}
