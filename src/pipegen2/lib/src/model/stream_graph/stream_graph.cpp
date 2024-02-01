// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/stream_graph.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamGraph>> StreamGraph::split_into_connected_components()
    {
        std::vector<std::unique_ptr<StreamGraph>> connected_components;

        std::unordered_set<std::size_t> visited_indices;
        std::unordered_map<const StreamNode*, std::size_t> stream_node_to_index;

        for (std::size_t i = 0; i < m_streams.size(); ++i)
        {
            stream_node_to_index[m_streams[i].get()] = i;
        }

        for (std::size_t i = 0; i < m_streams.size(); ++i)
        {
            if (visited_indices.find(i) != visited_indices.end())
            {
                continue;
            }

            std::vector<std::size_t> component_indices;
            find_connected_streams(i, stream_node_to_index, visited_indices, component_indices);

            // Sort the indices so that the streams are in the same order as they were in the original StreamGraph.
            std::sort(component_indices.begin(), component_indices.end());

            std::unique_ptr<StreamGraph> stream_graph_component = std::make_unique<StreamGraph>();
            for (std::size_t component_index : component_indices)
            {
                stream_graph_component->add_stream(std::move(m_streams[component_index]));
            }

            connected_components.emplace_back(std::move(stream_graph_component));
        }

        ASSERT(visited_indices.size() == m_streams.size(), "Not all streams were visited");

        m_streams.clear();

        return connected_components;
    }

    void StreamGraph::find_connected_streams(
        const std::size_t stream_node_index,
        const std::unordered_map<const StreamNode*, std::size_t>& stream_node_to_index,
        std::unordered_set<std::size_t>& visited_indices,
        std::vector<std::size_t>& component_indices)
    {
        if (visited_indices.find(stream_node_index) != visited_indices.end())
        {
            return;
        }

        visited_indices.insert(stream_node_index);
        component_indices.push_back(stream_node_index);

        for (const StreamNode* source_stream : m_streams[stream_node_index]->get_unique_sources())
        {
            auto source_stream_index_it = stream_node_to_index.find(source_stream);
            ASSERT(source_stream_index_it != stream_node_to_index.end(),
                   "Source stream not found in stream_node_to_index map");
            const std::size_t source_stream_index = source_stream_index_it->second;

            find_connected_streams(source_stream_index, stream_node_to_index, visited_indices, component_indices);
        }

        for (const StreamNode* destination_stream : m_streams[stream_node_index]->get_unique_destinations())
        {
            auto destination_stream_index_it = stream_node_to_index.find(destination_stream);
            ASSERT(destination_stream_index_it != stream_node_to_index.end(),
                   "Destination stream not found in stream_node_to_index map");
            const std::size_t destination_stream_index = destination_stream_index_it->second;

            find_connected_streams(destination_stream_index, stream_node_to_index, visited_indices, component_indices);
        }
    }

#ifdef DEBUG
    std::string StreamGraph::to_json() const
    {
        std::stringstream json_builder;

        // We need this map in order to visualize stream graphs before stream IDs are assigned.
        std::unordered_map<const StreamNode*, std::size_t> stream_to_graph_id;

        json_builder << "{ \"kind\": { \"graph\": true }, ";

        json_builder << "\"nodes\": [";
        for (std::size_t i = 0; i < m_streams.size(); ++i)
        {
            const StreamNode* stream = m_streams[i].get();
            StreamId stream_id = stream->get_stream_id();
            std::size_t stream_graph_id = i;
            stream_to_graph_id[stream] = stream_graph_id;
            std::string stream_type = stream_type_to_string(stream->get_stream_type());
            std::string stream_location = stream->get_physical_location().str();
            std::string stream_color = stream_type_to_color(stream->get_stream_type());
            json_builder << " { \"id\": \"" << stream_graph_id << "_" << stream_location
                         << "\", \"label\": \"" << stream_id << "\\n" << stream_type << "\\n" << stream_location
                         << "\", \"color\": \"" << stream_color
                         << "\", \"shape\": \"ellipse\" },";

            for (const NcriscConfig& ncrisc_config : stream->get_ncrisc_configs())
            {
                json_builder << " { \"id\": \"" << stream_graph_id << "_" << ncrisc_config.dram_buf_noc_addr
                             << "\", \"label\": \"NCRISC config\\nnoc_addr:" << ncrisc_config.dram_buf_noc_addr
                             << "\", \"color\": \"#FF00FF\", \"shape\": \"box\" },";
            }
        }
        // Erasing last ',' because that makes improper json.
        json_builder.seekp(-1, json_builder.cur);
        json_builder << " ], ";

        json_builder << "\"edges\": [";
        for (const std::unique_ptr<StreamNode>& stream: m_streams)
        {
            std::string stream_loc_id = std::to_string(stream_to_graph_id[stream.get()]) + "_" +
                                        stream->get_physical_location().str();
            for (const StreamNode* destination_stream : stream->get_unique_destinations())
            {
                std::string destination_stream_loc_id = std::to_string(stream_to_graph_id[destination_stream]) + "_" +
                                                        destination_stream->get_physical_location().str();
                json_builder << " { \"from\": \"" << stream_loc_id
                             << "\", \"to\": \"" << destination_stream_loc_id << "\" },";
            }

            for (const NcriscConfig& ncrisc_config : stream->get_ncrisc_configs())
            {
                std::string ncrisc_config_id = std::to_string(stream_to_graph_id[stream.get()]) + "_" +
                                               std::to_string(ncrisc_config.dram_buf_noc_addr);
                if (ncrisc_config.dram_input.value_or(false))
                {
                    json_builder << " { \"from\": \"" << ncrisc_config_id
                                 << "\", \"to\": \"" << stream_loc_id << "\" },";
                }
                else
                {
                    json_builder << " { \"from\": \"" << stream_loc_id
                                 << "\", \"to\": \"" << ncrisc_config_id << "\" },";
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