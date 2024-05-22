// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ncrisc_creator.h"
#include "ncrisc_creator_wh.h"

namespace pipegen2
{

// Implements NCRISC config creation for Blackhole architecture.
class NcriscCreatorBH : public NcriscCreatorWH
{
public:
    NcriscCreatorBH() : NcriscCreatorWH() {}
};

}  // namespace pipegen2