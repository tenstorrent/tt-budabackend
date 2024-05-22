// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ncrisc_creator.h"

namespace pipegen2
{
// Implements NCRISC config creation for Grayskull architecture.
class NcriscCreatorWH : public NcriscCreator
{
public:
    NcriscCreatorWH() : NcriscCreator() {}
};
}  // namespace pipegen2