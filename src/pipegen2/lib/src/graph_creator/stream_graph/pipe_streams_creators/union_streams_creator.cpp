// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/union_streams_creator.h"

#include "utils/logger.hpp"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> UnionStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // Won't need to create new streams for the Union pipe.
        return {};
    }

    void UnionStreamsCreator::assign_streams_to_pipe_outputs(
        const RGBasePipe* pipe,
        const std::vector<std::unique_ptr<StreamNode>>& created_streams)
    {
        StreamNode* union_stream = unify_input_streams(pipe);
        forward_union_stream_to_virtual_output(pipe, union_stream);
    }

    StreamNode* UnionStreamsCreator::unify_input_streams(const RGBasePipe* pipe)
    {
        const std::vector<const RGBaseNode*> input_nodes = pipe->get_unique_input_nodes();

        // Unifying all inputs streams into the first stream.
        StreamNode* union_stream = get_input_stream_node(input_nodes[0]);

        for (std::size_t i = 1; i < input_nodes.size(); ++i)
        {
            StreamNode* input_stream = get_input_stream_node(input_nodes[i]);
            log_assert(input_stream->get_stream_type() == StreamType::Relay,
                       "Only relay streams are currently supported at the Union pipe input");

            unify_input_stream_with_union_stream(input_stream, union_stream);
        }

        union_stream->get_base_config().set_is_union_stream(true);
        union_stream->sort_phases();

        return union_stream;
    }

    void UnionStreamsCreator::forward_union_stream_to_virtual_output(const RGBasePipe* pipe, StreamNode* union_stream)
    {
        const VirtualNode* out_virtual_node = dynamic_cast<const VirtualNode*>(pipe->get_output_node());
        if (out_virtual_node)
        {
            assign_stream_node_to_virtual_node(out_virtual_node, union_stream);
        }
    }

    void UnionStreamsCreator::unify_input_stream_with_union_stream(StreamNode* input_stream, StreamNode* union_stream)
    {
        update_stream_sources_dest_field(input_stream, union_stream);

        for (PhaseConfig& phase_config: input_stream->get_phase_configs())
        {
            union_stream->add_phase_config(phase_config.phase_id, std::move(phase_config.config));
        }

        input_stream->soft_delete();
    }

    void UnionStreamsCreator::update_stream_sources_dest_field(StreamNode* input_stream, StreamNode* union_stream)
    {
        std::vector<StreamNode*> input_stream_sources = input_stream->get_unique_sources();
        for (StreamNode* source_stream : input_stream_sources)
        {
            for (PhaseConfig& phase_config: source_stream->get_phase_configs())
            {
                if (phase_config.config.get_dest().has_value())
                {
                    std::vector<StreamNode*> updated_dests = phase_config.config.get_dest().value();
                    std::replace(updated_dests.begin(), updated_dests.end(), input_stream, union_stream);
                    phase_config.config.set_dest(updated_dests);
                }
            }
        }
    }
}