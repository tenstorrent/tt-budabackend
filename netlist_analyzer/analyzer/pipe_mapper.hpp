// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "chip.hpp"
#include "pipe.hpp"

namespace analyzer {

void mapPipe(Chip & chip, Pipe* p);
void mapPipe(Chip & chip, std::shared_ptr<Pipe> p);
void mapGenericPipe(Chip & chip, Pipe* p);
void mapEthernetPipe(Chip & chip, int chip_id, EthernetPipe * p);

}