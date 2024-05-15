// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// pipegen_yaml_loader.hpp
#pragma once

#include <memory>
#include <vector>

#include "chip.hpp"
#include "pipe.hpp"

namespace analyzer {
    struct EpochPipes {
        std::string graph_name;
        std::vector<std::shared_ptr<Pipe>> pipes;
        std::vector<std::shared_ptr<EthernetPipe>> ethernet_pipes;
    };
    // load epoch pipes
    EpochPipes load_pipegen_yaml(Chip& c, std::string filename);
}