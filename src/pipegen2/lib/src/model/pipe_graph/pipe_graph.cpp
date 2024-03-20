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

    bool PipeGraph::is_unmapped_location(const tt_cxy_pair& location)
    {
        return location.x == constants::unmapped_logical_location &&
               location.y == constants::unmapped_logical_location;
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

#ifdef TT_DEBUG
    std::string PipeGraph::to_json() const
    {
        std::stringstream json_builder;

        json_builder << "{ \"kind\": { \"graph\": true }, ";

        json_builder << "\"nodes\": [";
        for (const std::unique_ptr<PGBuffer>& buffer : m_buffers)
        {
            std::string node_type = buffer->type_to_string();
            tt_cxy_pair node_location = buffer->get_logical_location();
            std::string node_color = buffer->node_type_to_color();
            std::string location_str = is_unmapped_location(node_location) ?
                                       "(chip=" + std::to_string(node_location.chip) + ", no worker core)" :
                                       node_location.str();
            json_builder << " { \"id\": \"" << buffer->get_id()
                         << "\", \"label\": \"" << buffer->get_id() << "\\n" << node_type << "\\n" 
                         << location_str
                         << "\", \"color\": \"" << node_color
                         << "\", \"shape\": \"ellipse\" },";
        }
        for (const std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            json_builder << " { \"id\": \"" <<  pipe->get_id()
                         << "\", \"label\": \" Pipe " << pipe->get_id() << "\\n"
                         << "\", \"color\": \"#00BFFF\", \"shape\": \"box\" },";
        }
        // Erasing last ',' because that makes improper json.
        json_builder.seekp(-1, json_builder.cur);
        json_builder << " ], ";

        json_builder << "\"edges\": [";
        for (const std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            for (const PGBuffer* input_buffer : pipe->get_unique_input_buffers())
            {
                json_builder << " { \"from\": \"" << input_buffer->get_id()
                             << "\", \"to\": \"" << pipe->get_id()
                             << "\" },";
            }
            for (std::size_t scatter_index = 0; scatter_index < pipe->get_output_buffers().size(); scatter_index++)
            {
                for (PGBuffer* output_buffer : pipe->get_output_buffers()[scatter_index])
                {
                    json_builder << " { \"from\": \"" << pipe->get_id()
                             << "\", \"to\": \"" << output_buffer->get_id()
                             << "\", \"label\": \" scatter_index=" << scatter_index
                             << "\" },";
                }
            }
        }
        for (const std::unique_ptr<PGPipe>& pipe : m_pipes)
        {
            const std::vector<NodeId>& output_padding_buffer_ids = pipe->get_output_padding_buffers_ids();
            for (std::size_t scatter_index = 0; scatter_index < output_padding_buffer_ids.size(); 
                 scatter_index += pipe->get_consumer_repeat())
            {
                if (output_padding_buffer_ids[scatter_index] != c_no_output_padding_buffer_id)
                {
                    json_builder << " { \"from\": \"" << output_padding_buffer_ids[scatter_index]
                             << "\", \"to\": \"" << pipe->get_id()
                             << "\", \"label\": \" scatter_index=" << scatter_index
                             << "\", \"style\": \"dotted"
                             << "\" },";
                }
            }
        }
        // Erasing last ',' because that makes improper json.
        json_builder.seekp(-1, json_builder.cur);
        json_builder << " ] }";

        return json_builder.str();
    }
#endif

}