// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/pipe_graph/pipe_graph.h"

#include <unordered_set>

#include "pipegen2_constants.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<NodeId> PipeGraph::get_all_node_ids() const
    {
        std::vector<NodeId> all_node_ids;

        for (const std::unique_ptr<PGBuffer>& buffer : m_buffers)
        {
            all_node_ids.push_back(buffer->get_id());
        }
        for (const std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            all_node_ids.push_back(pipe->get_id());
        }

        return all_node_ids;
    }

    std::vector<ChipId> PipeGraph::get_all_chip_ids() const
    {
        std::unordered_set<ChipId> all_chip_ids;

        for (const std::unique_ptr<PGBuffer>& buffer : m_buffers)
        {
            all_chip_ids.insert(buffer->get_logical_location().chip);
        }
        for (const std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            for (const tt_cxy_pair& mcast_logical_location : pipe->get_mcast_core_logical_locations())
            {
                // Skip dummy pipes with unmapped logical locations.
                if (!PipeGraph::is_unmapped_location(mcast_logical_location))
                {
                    all_chip_ids.insert(mcast_logical_location.chip);
                }
            }
        }

        return std::vector(all_chip_ids.begin(), all_chip_ids.end());
    }

    PGBuffer* PipeGraph::get_shared_output_buffer(NodeId intermed_id) const
    {
        for (const std::unique_ptr<PGBuffer>& buffer : m_buffers)
        {
            if (buffer.get()->get_shared_space_buffer_id() == intermed_id)
            {
                return buffer.get();
            }
        }

        return nullptr;
    }

    bool PipeGraph::is_unmapped_location(const tt_cxy_pair& logical_location)
    {
        return logical_location.x == constants::unmapped_logical_location &&
               logical_location.y == constants::unmapped_logical_location;
    }

    void PipeGraph::remove_buffers(const std::unordered_set<PGBuffer*>& buffers_to_remove)
    {
        std::vector<std::unique_ptr<PGBuffer>> remaining_buffers;

        for (std::unique_ptr<PGBuffer>& buffer : m_buffers)
        {
            // If buffer is not marked for removal, put it in the new list.
            if (buffers_to_remove.count(buffer.get()) == 0)
            {
                remaining_buffers.push_back(std::move(buffer));
            }
        }

        // Move back all buffers that were not marked for removal to the member vector. This will delete all elements
        // that were left in the member vector previously.
        m_buffers = std::move(remaining_buffers);
    }

    void PipeGraph::remove_pipes(const std::unordered_set<PGPipe*>& pipes_to_remove)
    {
        std::vector<std::unique_ptr<PGPipe>> remaining_pipes;

        for (std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            // If pipe is not marked for removal, put it in the new list.
            if (pipes_to_remove.count(pipe.get()) == 0)
            {
                remaining_pipes.push_back(std::move(pipe));
            }
        }

        // Move back all pipes that were not marked for removal to the member vector. This will delete all elements
        // that were left in the member vector previously.
        m_pipes = std::move(remaining_pipes);
    }
}