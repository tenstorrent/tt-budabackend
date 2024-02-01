// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_stream_destination.h"

namespace pipegen2
{
    class RemoteStreamDestination : public BaseStreamDestination
    {
    protected:
        virtual BaseStreamDestination* clone_internal() const { return new RemoteStreamDestination(); }
    };
}