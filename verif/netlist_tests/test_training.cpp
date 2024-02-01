// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_training.hpp"

using namespace training;

int main(int argc, char** argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_training since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    int return_code = 0;

    std::vector<std::string> input_args(argv, argv + argc);
    return_code = run(input_args);
    return return_code;

}
