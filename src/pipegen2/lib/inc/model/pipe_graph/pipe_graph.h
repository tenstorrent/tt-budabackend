// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "model/typedefs.h"
#include "pg_buffer.h"
#include "pg_pipe.h"

namespace pipegen2
{
    class PipeGraph
    {
    public:

        // Value indicating that for a given scatter index, pipe should read from regular inputs instead of the scatter
        // padding buffers.
        static constexpr unsigned int c_no_output_padding_buffer_id = 0;

        void add_buffer(std::unique_ptr<PGBuffer> buffer) { m_buffers.push_back(std::move(buffer)); }

        void add_pipe(std::unique_ptr<PGPipe> pipe) { m_pipes.push_back(std::move(pipe)); }

        const std::vector<std::unique_ptr<PGBuffer>>& get_buffers() const { return m_buffers; }

        std::vector<std::unique_ptr<PGBuffer>>& get_buffers() { return m_buffers; }

        void remove_buffers(const std::unordered_set<PGBuffer*>& buffers_to_remove);

        void add_pipes(std::vector<std::unique_ptr<PGPipe>>& pipes_to_add);

        const std::vector<std::unique_ptr<PGPipe>>& get_pipes() const { return m_pipes; }

        std::vector<std::unique_ptr<PGPipe>>& get_pipes() { return m_pipes; }

        void remove_pipes(const std::unordered_set<PGPipe*>& pipes_to_remove);

        std::vector<NodeId> get_all_node_ids() const;

        std::vector<ChipId> get_all_chip_ids() const;

        // Returns buffer which shares L1 space with buffer with given `id` if such exists, otherwise nullptr.
        PGBuffer* get_shared_output_buffer(NodeId id) const;

        // Checks if core location is irregular, i.e. unmapped.
        static bool is_unmapped_location(const tt_cxy_pair& location);

#ifdef TT_DEBUG
        std::string to_json() const;
#endif

    private:
        std::vector<std::unique_ptr<PGBuffer>> m_buffers;

        std::vector<std::unique_ptr<PGPipe>> m_pipes;
    };
}