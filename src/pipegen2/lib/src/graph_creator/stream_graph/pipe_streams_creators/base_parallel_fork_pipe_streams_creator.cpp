// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/base_parallel_fork_pipe_streams_creator.h"

namespace pipegen2
{
    BaseParallelForkPipeStreamsCreator::~BaseParallelForkPipeStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> BaseParallelForkPipeStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;

        log_assert(pipe->get_inputs().size() == 1, "Expecting single input to the parallel fork pipe");
        const RGBaseNode* input_node = pipe->get_inputs()[0].get_node();

        const std::vector<const RGBaseNode*>& pipe_outputs = pipe->get_output_nodes();
        std::vector<StreamNode*> fork_streams;

        for (const RGBaseNode* pipe_output: pipe_outputs)
        {
            const RGBasePipe* output_pipe = pipe_output->get_output_pipe();
            created_streams.push_back(create_sending_stream(input_node, output_pipe, data_flow_info));
            fork_streams.push_back(created_streams.back().get());
        }

        for (std::size_t i = 0; i < pipe_outputs.size(); ++i)
        {
            StreamNode* created_fork_stream = fork_streams[i];

            const RGBaseNode* output_node = pipe_outputs[i];
            const VirtualNode* out_virt_node = dynamic_cast<const VirtualNode*>(output_node);
            log_assert(out_virt_node != nullptr, "Expecting virtual node at the parallel fork pipe output");

            created_fork_stream->get_base_config().set_fork_streams(fork_streams);
            created_fork_stream->get_base_config().set_num_fork_streams(fork_streams.size() - 1);

            // Blobgen requires that only the first packer stream in the fork group has output index set, so erase
            // this field from all the fork streams, apart the first one.
            if (i > 0)
            {
                created_fork_stream->get_base_config().opt_set_output_index(std::nullopt);
            }
        }

        return created_streams;
    }
}