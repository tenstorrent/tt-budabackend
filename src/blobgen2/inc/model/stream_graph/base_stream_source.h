// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace pipegen2
{
    class BaseStreamSource
    {
    public:
        virtual ~BaseStreamSource() = default;

        BaseStreamSource* clone() const;

    protected:
        // Each derived source should implement this to clone its additional parameters.
        virtual BaseStreamSource* clone_internal() const = 0;
    };
}