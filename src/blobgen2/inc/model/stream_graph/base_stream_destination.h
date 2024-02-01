// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace pipegen2
{
    class BaseStreamDestination
    {
    public:
        virtual ~BaseStreamDestination() = default;

        BaseStreamDestination* clone() const;

    protected:
        // Each derived destination should implement this to clone its additional parameters.
        virtual BaseStreamDestination* clone_internal() const = 0;
    };
}