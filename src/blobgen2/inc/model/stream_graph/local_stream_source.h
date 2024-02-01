// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_stream_source.h"

namespace pipegen2
{
    class LocalStreamSource : public BaseStreamSource
    {
    protected:
        virtual BaseStreamSource* clone_internal() const { return new LocalStreamSource(); }
    };
}