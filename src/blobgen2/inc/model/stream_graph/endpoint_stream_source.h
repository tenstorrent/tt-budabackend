// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "base_stream_source.h"

namespace pipegen2
{
    class EndpointStreamSource : public BaseStreamSource
    {
    public:
        std::optional<bool> is_ptrs_not_zero() const { return m_ptrs_not_zero; }

        void set_ptrs_not_zero(std::optional<bool> value) { m_ptrs_not_zero = value; }
        void set_ptrs_not_zero(bool value) { m_ptrs_not_zero = value; }

    protected:
        virtual BaseStreamSource* clone_internal() const;

    private:
        // TODO: fill in
        std::optional<bool> m_ptrs_not_zero;
    };
}