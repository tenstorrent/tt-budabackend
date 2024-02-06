// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "data_flow_calculator/data_flow_calculator.h"
#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipegen2_constants.h"

#include "mocks/model/rational_graph/rg_nodes_mocks.h"
#include "mocks/model/rational_graph/rg_pipes_mocks.h"

using namespace pipegen2;
using namespace std;

namespace
{
// Creates appropriate fork pipe and connects forked node on input and virtual nodes on output.
void create_fork_pipe_block(DataFlowType dataflow_type,
                            unique_ptr<RGBaseNode> forked_node,
                            vector<VirtualNode*> virt_nodes,
                            vector<unique_ptr<RGBaseNode>>& nodes,
                            vector<unique_ptr<RGBasePipe>>& pipes,
                            NodeId& node_id,
                            NodeId& pipe_id)
{
    unique_ptr<RGBasePipe> fork_pipe;
    if (dataflow_type == DataFlowType::Serial)
    {
        fork_pipe = make_unique<SerialForkPipeMock>(++pipe_id, virt_nodes.size(), 0);
    }
    else
    {
        fork_pipe = make_unique<ParallelForkPipeMock>(++pipe_id);
    }

    forked_node->add_output_pipe(fork_pipe.get());
    fork_pipe->add_input_node(forked_node.get());

    for (VirtualNode* virt_node : virt_nodes)
    {
        virt_node->set_input_pipe(fork_pipe.get());
        fork_pipe->add_output_node(virt_node);
    }

    nodes.push_back(move(forked_node));
    pipes.push_back(move(fork_pipe));
}

// Creates appropriate unicast pipe and connects input and output nodes.
void create_unicast_block(unique_ptr<RGBaseNode> input_node,
                          unique_ptr<RGBaseNode> output_node,
                          vector<unique_ptr<RGBaseNode>>& nodes,
                          vector<unique_ptr<RGBasePipe>>& pipes,
                          NodeId& node_id,
                          NodeId& pipe_id,
                          uint32_t repeat_input = 1)
{
    unique_ptr<RGBasePipe> unicast_pipe = make_unique<UnicastPipeMock>(++pipe_id);
    input_node->add_output_pipe(unicast_pipe.get());
    unicast_pipe->add_output_node(output_node.get());
    for (uint32_t i = 0; i < repeat_input; ++i)
    {
        unicast_pipe->add_input_node(input_node.get());
    }
    output_node->set_input_pipe(unicast_pipe.get());

    nodes.push_back(move(input_node));
    nodes.push_back(move(output_node));
    pipes.push_back(move(unicast_pipe));
}

// Checks that every receiver phase corresponds to contiguous subset of sender phases that sum up to the same number of
// messages.
void check_phase_alignment(const vector<PhaseInfo>& sender_phases, const vector<PhaseInfo>& receiver_phases,
                           const unsigned int max_num_tiles_per_phase)
{
    size_t sender_phase_index = 0;
    for (const PhaseInfo& receiver_phase : receiver_phases)
    {
        unsigned int sender_phase_sum = 0;
        while (sender_phase_index < sender_phases.size() &&
               sender_phase_sum + sender_phases[sender_phase_index].num_msgs <= receiver_phase.num_msgs)
        {
            sender_phase_sum += sender_phases[sender_phase_index].num_msgs;
            ASSERT_LE(sender_phase_sum, max_num_tiles_per_phase);
            ++sender_phase_index;
        }
        ASSERT_EQ(sender_phase_sum, receiver_phase.num_msgs);
        sender_phase_sum = 0;
    }
}
} // namespace

TEST(Pipegen2_DataFlowCalculator, ParallelWithSerialForkCombination)
{
    vector<unique_ptr<RGBaseNode>> nodes;
    vector<unique_ptr<RGBasePipe>> pipes;

    NodeId node_id = 100;
    NodeId pipe_id = 200;

    unique_ptr<RGBaseNode> root_packer_node = make_unique<PackerInputNodeMock>(
        ++node_id /* node_id */,
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        10 /* scatter_gather_num_tiles */
    );

    unique_ptr<VirtualNode> virt_node_1 = make_unique<VirtualNode>(++node_id);
    unique_ptr<VirtualNode> virt_node_2 = make_unique<VirtualNode>(++node_id);
    create_fork_pipe_block(
        DataFlowType::Parallel, move(root_packer_node), {virt_node_1.get(), virt_node_2.get()},
        nodes, pipes, node_id, pipe_id);

    unique_ptr<VirtualNode> virt_node_1_1 = make_unique<SerialForkNode>(++node_id, 0 /* scatter_index */);
    unique_ptr<VirtualNode> virt_node_1_2 = make_unique<SerialForkNode>(++node_id, 1 /* scatter_index */);
    create_fork_pipe_block(
        DataFlowType::Serial, move(virt_node_1), {virt_node_1_1.get(), virt_node_1_2.get()},
        nodes, pipes, node_id, pipe_id);

    unique_ptr<VirtualNode> virt_node_2_1 = make_unique<SerialForkNode>(++node_id, 0 /* scatter_index */);
    unique_ptr<VirtualNode> virt_node_2_2 = make_unique<SerialForkNode>(++node_id, 1 /* scatter_index */);
    create_fork_pipe_block(
        DataFlowType::Serial, move(virt_node_2), {virt_node_2_1.get(), virt_node_2_2.get()},
        nodes, pipes, node_id, pipe_id);

    unique_ptr<RGBaseNode> unpacker_node_1 = make_unique<UnpackerOutputNodeMock>(
        ++node_id,
        10 /* size_tiles */,
        5 /* transfer_granularity */
    );
    create_unicast_block(
        move(virt_node_1_1), move(unpacker_node_1), nodes, pipes, node_id, pipe_id, 3 /* repeat_input */);

    unique_ptr<RGBaseNode> unpacker_node_2 = make_unique<UnpackerOutputNodeMock>(
        ++node_id,
        10 /* size_tiles */,
        5 /* transfer_granularity */
    );
    create_unicast_block(
        move(virt_node_1_2), move(unpacker_node_2), nodes, pipes, node_id, pipe_id, 5 /* repeat_input */);

    unique_ptr<RGBaseNode> unpacker_node_3 = make_unique<UnpackerOutputNodeMock>(
        ++node_id,
        10 /* size_tiles */,
        5 /* transfer_granularity */
    );
    create_unicast_block(
        move(virt_node_2_1), move(unpacker_node_3), nodes, pipes, node_id, pipe_id, 2 /* repeat_input */);

    unique_ptr<RGBaseNode> unpacker_node_4 = make_unique<UnpackerOutputNodeMock>(
        ++node_id,
        10 /* size_tiles */,
        5 /* transfer_granularity */
    );
    create_unicast_block(
        move(virt_node_2_2), move(unpacker_node_4), nodes, pipes, node_id, pipe_id, 4 /* repeat_input */);

    RationalGraph rg(std::move(nodes), std::move(pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, constants::general_max_num_tiles_per_phase);
    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    for (const unique_ptr<RGBasePipe>& pipe : rg.get_pipes())
    {
        if (pipe->get_dataflow_type() == DataFlowType::Serial)
        {
            uint32_t prev_phase_offset = 0;
            for (size_t i = 0; i < pipe->get_output_nodes().size(); ++i)
            {
                const vector<PhaseInfo>& pipe_to_node_phases = data_flow_info.get_edge_phases(
                    pipe.get(), pipe->get_output_nodes()[i]);
                ASSERT_GT(pipe_to_node_phases.size(), 0);
                if (i == 0)
                {
                    prev_phase_offset = pipe_to_node_phases.back().phase_offset;
                    continue;
                }

                ASSERT_GT(pipe_to_node_phases[0].phase_offset, prev_phase_offset);
                prev_phase_offset = pipe_to_node_phases.back().phase_offset;
            }
        }
    }
    for (const unique_ptr<RGBaseNode>& node : rg.get_nodes())
    {
        if (node->get_output_pipes().size() == 0)
        {
            continue;
        }
        const vector<PhaseInfo>& node_to_pipe_phases = data_flow_info.get_edge_phases(
            node.get(), node->get_output_pipe());
        ASSERT_GT(node_to_pipe_phases.size(), 0);
        if (node->get_input_pipe())
        {
            const vector<PhaseInfo>& pipe_to_node_phases = data_flow_info.get_edge_phases(
                node->get_input_pipe(), node.get());
            ASSERT_GT(pipe_to_node_phases.size(), 0);
        }
    }
}

TEST(Pipegen2_DataFlowCalculator, SenderPhaseAccumulation)
{
    vector<unique_ptr<RGBaseNode>> nodes;
    vector<unique_ptr<RGBasePipe>> pipes;

    NodeId node_id = 100;
    NodeId pipe_id = 200;

    unique_ptr<RGBaseNode> root_packer_node = make_unique<PackerInputNodeMock>(
        ++node_id /* node_id */,
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */
    );

    unique_ptr<VirtualNode> virt_node_1 = make_unique<VirtualNode>(++node_id);

    create_fork_pipe_block(
        DataFlowType::Parallel, move(root_packer_node), {virt_node_1.get()},
        nodes, pipes, node_id, pipe_id);

    // Let transfer granularity be 5 tiles so that 2K is not a multiple of it.
    unique_ptr<UnpackerOutputNodeMock> unpacker_node = make_unique<UnpackerOutputNodeMock>(
        ++node_id,
        10 /* size_tiles */,
        5 /* transfer_granularity */
    );

    create_unicast_block(
        move(virt_node_1), move(unpacker_node), nodes, pipes, node_id, pipe_id, 1 /* repeat_input */);

    RGBaseNode* input_node = nodes[nodes.size() - 2].get();
    RGBaseNode* unpacker_node_ptr = nodes.back().get();
    RGBasePipe* unicast_pipe = pipes.back().get();

    const unsigned int max_num_tiles_per_phase = 64;

    // Now let's add bunch more input offsets to the unicast pipe so that we get bunch of tiles accumulated.
    for (uint32_t i = 1; i < 3 * max_num_tiles_per_phase; ++i)
    {
        // By limiting offset to max_num_tiles_per_phase / 2 we make sure sender can't accumulate indefinitely.
        unicast_pipe->add_input_node(input_node, i % (max_num_tiles_per_phase / 2));
    }

    RationalGraph rg(std::move(nodes), std::move(pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    const vector<PhaseInfo>& node_to_pipe_phases = data_flow_info.get_edge_phases(
        input_node, unicast_pipe);
    const vector<PhaseInfo>& pipe_to_node_phases = data_flow_info.get_edge_phases(
        unicast_pipe, unpacker_node_ptr);

    ASSERT_GT(node_to_pipe_phases.size(), pipe_to_node_phases.size());

    check_phase_alignment(node_to_pipe_phases, pipe_to_node_phases,
                          data_flow_info.get_max_num_tiles_per_phase(unpacker_node_ptr));
}