// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "base_stream_destination.h"

namespace pipegen2
{
    class LocalStreamDestination : public BaseStreamDestination
    {
    public:
        std::optional<bool> is_local_receiver_tile_clearing() const { return m_local_receiver_tile_clearing; }

        void set_local_receiver_tile_clearing(std::optional<bool> value) { m_local_receiver_tile_clearing = value; }
        void set_local_receiver_tile_clearing(bool value) { m_local_receiver_tile_clearing = value; }

    protected:
        virtual BaseStreamDestination* clone_internal() const;

    private:
        // TODO: fill in
        std::optional<bool> m_local_receiver_tile_clearing;
    };
}