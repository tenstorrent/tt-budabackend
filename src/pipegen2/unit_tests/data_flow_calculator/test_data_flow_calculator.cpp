// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/data_flow_calculator.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipegen2_constants.h"

#include "mocks/model/rational_graph/rg_nodes_mocks.h"
#include "mocks/model/rational_graph/rg_pipes_mocks.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Tests for function: DataFlowCalculator::get_data_flow_info
**********************************************************************************************************************/

class Pipegen2_DataFlowCalculator_GetDataFlowInfo : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
        m_pipe_id = 200;
    }

    RGBaseNode* create_rg_node(std::unique_ptr<RGBaseNode>&& rg_node)
    {
        m_rg_nodes.push_back(std::move(rg_node));

        return m_rg_nodes.back().get();
    }

    RGBaseNode* create_packer_node(
        unsigned int size_tiles,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input,
        unsigned int num_scatter_chunks,
        unsigned int scatter_gather_num_tiles)
    {
        return create_rg_node(std::make_unique<PackerInputNodeMock>(
            ++m_node_id, size_tiles, num_epoch_tiles, tiles_per_input, num_scatter_chunks, scatter_gather_num_tiles));
    }

    RGBaseNode* create_virtual_node() { return create_rg_node(std::make_unique<VirtualNode>(++m_node_id)); }

    RGBaseNode* create_serial_fork_node(unsigned int scatter_index)
    {
        return create_rg_node(std::make_unique<SerialForkNode>(++m_node_id, scatter_index));
    }

    RGBaseNode* create_unpacker_node(unsigned int size_tiles, unsigned int transfer_granularity)
    {
        return create_rg_node(std::make_unique<UnpackerOutputNodeMock>(++m_node_id, size_tiles, transfer_granularity));
    }

    RGBasePipe* create_rg_pipe(std::unique_ptr<RGBasePipe>&& rg_pipe)
    {
        m_rg_pipes.push_back(std::move(rg_pipe));

        return m_rg_pipes.back().get();
    }

    // Creates appropriate fork pipe and connects forked node on input and virtual nodes on output.
    RGBasePipe* create_fork_pipe_block(
        DataFlowType dataflow_type, RGBaseNode* forked_node, std::vector<RGBaseNode*> virt_nodes)
    {
        std::unique_ptr<RGBasePipe> fork_pipe;
        if (dataflow_type == DataFlowType::Serial)
        {
            fork_pipe = std::make_unique<SerialForkPipeMock>(++m_pipe_id, virt_nodes.size(), 0);
        }
        else
        {
            fork_pipe = std::make_unique<ParallelForkPipeMock>(++m_pipe_id);
        }

        forked_node->add_output_pipe(fork_pipe.get());
        fork_pipe->add_input_node(forked_node);

        for (RGBaseNode* virt_node : virt_nodes)
        {
            virt_node->set_input_pipe(fork_pipe.get());
            fork_pipe->add_output_node(virt_node);
        }

        return create_rg_pipe(std::move(fork_pipe));
    }

    // Creates appropriate unicast pipe and connects input and output nodes.
    RGBasePipe* create_unicast_block(RGBaseNode* input_node, RGBaseNode* output_node, unsigned int repeat_input = 1)
    {
        std::unique_ptr<RGBasePipe> unicast_pipe = std::make_unique<UnicastPipeMock>(++m_pipe_id);

        input_node->add_output_pipe(unicast_pipe.get());
        unicast_pipe->add_output_node(output_node);
        for (unsigned int i = 0; i < repeat_input; ++i)
        {
            unicast_pipe->add_input_node(input_node);
        }
        output_node->set_input_pipe(unicast_pipe.get());

        return create_rg_pipe(std::move(unicast_pipe));
    }

    NodeId m_node_id;
    NodeId m_pipe_id;

    std::vector<std::unique_ptr<RGBaseNode>> m_rg_nodes;
    std::vector<std::unique_ptr<RGBasePipe>> m_rg_pipes;
};

TEST_F(Pipegen2_DataFlowCalculator_GetDataFlowInfo, ParallelWithSerialForkCombination)
{
    RGBaseNode* root_packer_node = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        10 /* scatter_gather_num_tiles */);

    RGBaseNode* virt_node_1 = create_virtual_node();
    RGBaseNode* virt_node_2 = create_virtual_node();
    create_fork_pipe_block(DataFlowType::Parallel, root_packer_node, {virt_node_1, virt_node_2});

    RGBaseNode* virt_node_1_1 = create_serial_fork_node(0 /* scatter_index */);
    RGBaseNode* virt_node_1_2 = create_serial_fork_node(1 /* scatter_index */);
    create_fork_pipe_block(DataFlowType::Serial, virt_node_1, {virt_node_1_1, virt_node_1_2});

    RGBaseNode* virt_node_2_1 = create_serial_fork_node(0 /* scatter_index */);
    RGBaseNode* virt_node_2_2 = create_serial_fork_node(1 /* scatter_index */);
    create_fork_pipe_block(DataFlowType::Serial, virt_node_2, {virt_node_2_1, virt_node_2_2});

    RGBaseNode* unpacker_node_1 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    create_unicast_block(virt_node_1_1, unpacker_node_1, 3 /* repeat_input */);

    RGBaseNode* unpacker_node_2 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    create_unicast_block(virt_node_1_2, unpacker_node_2, 5 /* repeat_input */);

    RGBaseNode* unpacker_node_3 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    create_unicast_block(virt_node_2_1, unpacker_node_3, 2 /* repeat_input */);

    RGBaseNode* unpacker_node_4 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    create_unicast_block(virt_node_2_2, unpacker_node_4, 4 /* repeat_input */);

    RationalGraph rg(std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, constants::general_max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    for (const std::unique_ptr<RGBasePipe>& pipe : rg.get_pipes())
    {
        if (pipe->get_dataflow_type() == DataFlowType::Serial)
        {
            unsigned int prev_phase_offset = 0;
            for (size_t i = 0; i < pipe->get_output_nodes().size(); ++i)
            {
                const std::vector<PhaseInfo>& pipe_to_node_phases =
                    data_flow_info.get_edge_phases(pipe.get(), pipe->get_output_nodes()[i]);
                EXPECT_GT(pipe_to_node_phases.size(), 0);

                if (i == 0)
                {
                    prev_phase_offset = pipe_to_node_phases.back().phase_offset;
                    continue;
                }

                EXPECT_GT(pipe_to_node_phases[0].phase_offset, prev_phase_offset);
                prev_phase_offset = pipe_to_node_phases.back().phase_offset;
            }
        }
    }

    for (const std::unique_ptr<RGBaseNode>& node : rg.get_nodes())
    {
        if (node->get_output_pipes().size() == 0)
        {
            continue;
        }

        const std::vector<PhaseInfo>& node_to_pipe_phases =
            data_flow_info.get_edge_phases(node.get(), node->get_output_pipe());
        EXPECT_GT(node_to_pipe_phases.size(), 0);

        if (node->get_input_pipe())
        {
            const std::vector<PhaseInfo>& pipe_to_node_phases =
                data_flow_info.get_edge_phases(node->get_input_pipe(), node.get());
            EXPECT_GT(pipe_to_node_phases.size(), 0);
        }
    }
}

TEST_F(Pipegen2_DataFlowCalculator_GetDataFlowInfo, SenderPhaseAccumulation)
{
    RGBaseNode* root_packer_node = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */);

    RGBaseNode* unicast_pipe_input_virt_node = create_virtual_node();
    create_fork_pipe_block(DataFlowType::Parallel, root_packer_node, {unicast_pipe_input_virt_node});

    // Let transfer granularity be 5 tiles so that 2K is not a multiple of it.
    RGBaseNode* unpacker_node = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    RGBasePipe* unicast_pipe = create_unicast_block(unicast_pipe_input_virt_node, unpacker_node, 1 /* repeat_input */);

    const unsigned int max_num_tiles_per_phase = 64;

    // Now let's add bunch more input offsets to the unicast pipe so that we get bunch of tiles accumulated.
    for (unsigned int i = 1; i < 3 * max_num_tiles_per_phase; ++i)
    {
        // By limiting offset to max_num_tiles_per_phase / 2 we make sure sender can't accumulate indefinitely.
        unicast_pipe->add_input_node(unicast_pipe_input_virt_node, i % (max_num_tiles_per_phase / 2));
    }

    RationalGraph rg(std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    const std::vector<PhaseInfo>& node_to_pipe_phases =
        data_flow_info.get_edge_phases(unicast_pipe_input_virt_node, unicast_pipe);

    const std::vector<PhaseInfo>& pipe_to_node_phases = data_flow_info.get_edge_phases(unicast_pipe, unpacker_node);

    EXPECT_GT(node_to_pipe_phases.size(), pipe_to_node_phases.size());

    check_phase_alignment(
        node_to_pipe_phases, pipe_to_node_phases, data_flow_info.get_max_num_tiles_per_phase(unpacker_node));
}

TEST_F(Pipegen2_DataFlowCalculator_GetDataFlowInfo, ThrowsOnAccessingNonExistentEdgePhases)
{
    RGBaseNode* root_packer_node = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */);

    RGBaseNode* virt_node = create_virtual_node();
    create_fork_pipe_block(DataFlowType::Parallel, root_packer_node, {virt_node});

    RGBaseNode* unpacker_node = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    RGBasePipe* unicast_pipe = create_unicast_block(virt_node, unpacker_node, 1 /* repeat_input */);

    RationalGraph rg(std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, constants::general_max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    // Non-existent node -> pipe edge.
    verify_log_assert(
        [&]() { data_flow_info.get_edge_phases(unpacker_node, unicast_pipe); },
        "^Expecting to find edge phases from node [0-9]+ to pipe [0-9]+.*");

    // Non-existent pipe -> node edge.
    verify_log_assert(
        [&]() { data_flow_info.get_edge_phases(unicast_pipe, virt_node); },
        "^Expecting to find edge phases from pipe [0-9]+ to node [0-9]+.*");
}

TEST_F(Pipegen2_DataFlowCalculator_GetDataFlowInfo, IsolatedDataFlowGraphGetsSkipped)
{
    RGBaseNode* root_packer_node = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */);

    RationalGraph rg(std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, constants::general_max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    // Verify that the node got skipped, i.e. it won't have associated data flow info.
    verify_log_assert(
        [&]() { data_flow_info.get_max_num_tiles_per_phase(root_packer_node); },
        "^Expecting to find data flow info for node [0-9]+.*");

    verify_log_assert(
        [&]() { data_flow_info.get_tiles_to_send(root_packer_node); },
        "^Expecting to find data flow info for node [0-9]+.*");
}

TEST_F(Pipegen2_DataFlowCalculator_GetDataFlowInfo, MultipleDisconnectedDataFlowGraphs)
{
    RGBaseNode* root_packer_node_1 = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */);
    RGBaseNode* virt_node_1 = create_virtual_node();
    create_fork_pipe_block(DataFlowType::Parallel, root_packer_node_1, {virt_node_1});
    RGBaseNode* unpacker_node_1 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    RGBasePipe* unicast_pipe_1 = create_unicast_block(virt_node_1, unpacker_node_1, 1 /* repeat_input */);

    RGBaseNode* root_packer_node_2 = create_packer_node(
        2 /* size_tiles */,
        40 /* num_epoch_tiles */,
        1 /* tiles_per_input */,
        20 /* num_scatter_chunks */,
        1 /* scatter_gather_num_tiles */);
    RGBaseNode* virt_node_2 = create_virtual_node();
    create_fork_pipe_block(DataFlowType::Parallel, root_packer_node_2, {virt_node_2});
    RGBaseNode* unpacker_node_2 = create_unpacker_node(10 /* size_tiles */, 5 /* transfer_granularity */);
    RGBasePipe* unicast_pipe_2 = create_unicast_block(virt_node_2, unpacker_node_2, 1 /* repeat_input */);

    RationalGraph rg(std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    DataFlowCalculator dfc(&rg, constants::general_max_num_tiles_per_phase);

    DataFlowInfo data_flow_info = dfc.get_data_flow_info();

    // Verify that the data flow info objects holds information about both data flow graphs.
    EXPECT_GT(data_flow_info.get_max_num_tiles_per_phase(unpacker_node_1), 0);
    EXPECT_GT(data_flow_info.get_tiles_to_send(virt_node_1), 0);
    EXPECT_GT(data_flow_info.get_num_iterations_in_epoch(unicast_pipe_1), 0);
    EXPECT_GT(data_flow_info.get_subtree_divisor(unicast_pipe_1), 0);

    EXPECT_GT(data_flow_info.get_max_num_tiles_per_phase(unpacker_node_2), 0);
    EXPECT_GT(data_flow_info.get_tiles_to_send(virt_node_2), 0);
    EXPECT_GT(data_flow_info.get_num_iterations_in_epoch(unicast_pipe_2), 0);
    EXPECT_GT(data_flow_info.get_subtree_divisor(unicast_pipe_2), 0);
}