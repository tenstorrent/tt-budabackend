// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/subgraph_leaf_groups_finder.h"

#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "model/data_flow/subgraph_leaf_groups.h"

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::Contains;
using ::testing::Key;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAreArray;

/**********************************************************************************************************************
    Tests for function: SubgraphLeafGroupsFinder::find_leaf_groups
**********************************************************************************************************************/

class Pipegen2_SubgraphLeafGroupsFinder_FindLeafGroups : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        DataFlowType data_flow_type,
        unsigned int num_root_to_leaf_paths,
        const std::optional<unsigned int>& root_to_leaf_path_index,
        unsigned int num_unique_df_paths)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_dataflow_type()).WillByDefault(Return(data_flow_type));
        ON_CALL(*df_node_mock, get_root_to_leaf_path_index()).WillByDefault(Return(root_to_leaf_path_index));
        ON_CALL(*df_node_mock, get_num_root_to_leaf_paths_in_subgraph()).WillByDefault(Return(num_root_to_leaf_paths));

        df_node_mock->set_number_of_unique_df_paths(num_unique_df_paths);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_packer_df_node_mock()
    {
        return make_df_node_mock(
            DataFlowType::Serial,
            1 /* num_root_to_leaf_paths */,
            std::nullopt /* root_to_leaf_path_index */,
            1 /* num_unique_df_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_parallel_fork_df_node_mock()
    {
        return make_df_node_mock(
            DataFlowType::Parallel,
            1 /* num_root_to_leaf_paths */,
            std::nullopt /* root_to_leaf_path_index */,
            1 /* num_unique_df_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_serial_fork_df_node_mock(
        unsigned int num_root_to_leaf_paths, unsigned int root_to_leaf_path_index)
    {
        return make_df_node_mock(
            DataFlowType::Serial,
            num_root_to_leaf_paths,
            std::make_optional(root_to_leaf_path_index),
            1 /* num_unique_df_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_virtual_df_node_mock(unsigned int num_unique_df_paths = 1)
    {
        return make_df_node_mock(
            DataFlowType::Serial,
            1 /* num_root_to_leaf_paths */,
            std::nullopt /* root_to_leaf_path_index */,
            num_unique_df_paths);
    }

    std::unique_ptr<DataFlowNodeMock> make_unpacker_df_node_mock(unsigned int num_unique_df_paths = 1)
    {
        return make_df_node_mock(
            DataFlowType::ParallelCopy,
            1 /* num_root_to_leaf_paths */,
            std::nullopt /* root_to_leaf_path_index */,
            num_unique_df_paths);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_SubgraphLeafGroupsFinder_FindLeafGroups, CompoundDataFlowGraph)
{
    // Note: To better understand this test case, one can visit `Leaf node clustering` page in pipegen2 design graffle.
    const unsigned int num_root_to_leaf_paths_a = 3;
    const unsigned int num_root_to_leaf_paths_b = 2;

    std::unique_ptr<DataFlowNodeMock> packer_node_1 = make_packer_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> packer_node_2 = make_packer_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> packer_node_3 = make_packer_df_node_mock();

    // Packer node is always hidden behind parallel fork node. In this example, packer nodes 1 and 3 do not fork, and
    // packer 2 forks to two destinations. The first number in the name indicates the input packer node and the second
    // number indicates the fork path index.

    // This parallel fork will start subgraph ID of 0 which will get propagated to the leaf nodes.
    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_1_1 = make_parallel_fork_df_node_mock();

    // This parallel fork will start subgraph ID of 1, however it won't get propagated to the leaf nodes, as it connects
    // to the same gather as the previous parallel fork, which will already propagate its subgraph id.
    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_2_1 = make_parallel_fork_df_node_mock();

    // Similarly, starts subgraph ID of 2, propagates to leaf nodes.
    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_2_2 = make_parallel_fork_df_node_mock();

    // Starts subgraphs ID of 3 but doesn't propagate it to the leaf nodes, as it connects to the same union as the
    // previous parallel fork.
    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_3_1 = make_parallel_fork_df_node_mock();

    // First packer root node has a serial fork to two destinations on its only fork path. Naming pattern for the serial
    // fork nodes is serial_fork_node_<packer_id>_<fork_id>_<serial_fork_destination_id>.
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_1_1_1 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_a, 0 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_1_1_2 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_a, 2 /* root_to_leaf_path_index */);

    // Second packer has two serial fork destinations on its only fork branch.
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_2_1_1 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_a, 0 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_2_1_2 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_a, 2 /* root_to_leaf_path_index */);

    // Second packer has one serial fork destination on its second fork branch. This is done to emulate the padding
    // buffers.
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_2_2_1 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_b, 0 /* root_to_leaf_path_index */);

    // Third packer has one serial fork destination on its only fork branch.
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_3_1_1 =
        make_serial_fork_df_node_mock(num_root_to_leaf_paths_b, 1 /* root_to_leaf_path_index */);

    // Gather nodes ensure that different packer fork branches end up in the same subgraph. Despise these gather nodes
    // are in the same subgraph, each of them will open a different leaf group (which _1 and _2 suffixes indicate).
    std::unique_ptr<DataFlowNodeMock> gather_node_a_0 = make_virtual_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node_a_2 = make_virtual_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> gather_node_b_0 = make_virtual_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node_b_1 = make_virtual_df_node_mock();

    // Union node is in different subgraph since it is not connected to these gather nodes.
    std::unique_ptr<DataFlowNodeMock> union_node = make_virtual_df_node_mock(2 /* num_unique_df_paths */);

    // The naming here follows a convention unpacker_node_<subgraph>_<leaf_group>_<node_in_the_leaf_group>.
    std::unique_ptr<DataFlowNodeMock> unpacker_node_a_0_1 = make_unpacker_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> unpacker_node_a_2_1 = make_unpacker_df_node_mock();

    // Not here that these unpackers are multicast destinations so they all have the same subgraph and the same leaf
    // group (_b_1_) but they vary in the position in that leaf group.
    std::unique_ptr<DataFlowNodeMock> unpacker_node_b_0_1 = make_unpacker_df_node_mock(2 /* num_unique_df_paths */);
    std::unique_ptr<DataFlowNodeMock> unpacker_node_b_0_2 = make_unpacker_df_node_mock(2 /* num_unique_df_paths */);
    std::unique_ptr<DataFlowNodeMock> unpacker_node_b_0_3 = make_unpacker_df_node_mock(2 /* num_unique_df_paths */);

    std::vector<DataFlowNode*> root_nodes{packer_node_1.get(), packer_node_2.get(), packer_node_3.get()};
    connect_data_flow_graph({
        make_df_edge(packer_node_1, parallel_fork_node_1_1),

        make_df_edge(packer_node_2, parallel_fork_node_2_1),
        make_df_edge(packer_node_2, parallel_fork_node_2_2),

        make_df_edge(packer_node_3, parallel_fork_node_3_1),

        make_df_edge(parallel_fork_node_1_1, serial_fork_node_1_1_1),
        make_df_edge(parallel_fork_node_1_1, serial_fork_node_1_1_2),

        make_df_edge(parallel_fork_node_2_1, serial_fork_node_2_1_1),
        make_df_edge(parallel_fork_node_2_1, serial_fork_node_2_1_2),

        make_df_edge(parallel_fork_node_2_2, serial_fork_node_2_2_1),

        make_df_edge(parallel_fork_node_3_1, serial_fork_node_3_1_1),

        make_df_edge(serial_fork_node_1_1_1, gather_node_a_0),
        make_df_edge(serial_fork_node_2_1_1, gather_node_a_0),

        make_df_edge(serial_fork_node_1_1_2, gather_node_a_2),
        make_df_edge(serial_fork_node_2_1_2, gather_node_a_2),

        make_df_edge(serial_fork_node_2_2_1, gather_node_b_0),
        make_df_edge(serial_fork_node_3_1_1, gather_node_b_1),

        make_df_edge(gather_node_a_0, unpacker_node_a_0_1),

        make_df_edge(gather_node_a_2, unpacker_node_a_2_1),

        make_df_edge(gather_node_b_0, union_node),
        make_df_edge(gather_node_b_1, union_node),

        make_df_edge(union_node, unpacker_node_b_0_1),
        make_df_edge(union_node, unpacker_node_b_0_2),
        make_df_edge(union_node, unpacker_node_b_0_3),
    });

    SubgraphLeafGroupsFinder subgraph_leaf_group_finder;
    std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups = subgraph_leaf_group_finder.find_leaf_groups(root_nodes);

    const std::unordered_map<unsigned int, LeafGroupOrder>& subgraph_leaves =
        subgraph_leaf_groups->get_subgraph_leaves();

    EXPECT_EQ(subgraph_leaves.size(), 4 /* Number of distinct parallel fork paths in the data flow graph */);

    // Verify subgraph A (with subgraph ID of 0).
    EXPECT_THAT(subgraph_leaves, Contains(Key(0)));
    const LeafGroupOrder& subgraph_a = subgraph_leaves.at(0);
    EXPECT_EQ(subgraph_a.get_leaf_groups().size(), num_root_to_leaf_paths_a);

    // Note that these indexes 0 and 2 correspond to the `root_to_leaf_path_index` of the appropriate serial fork
    // node which feeds the corresponding gather node which feeds the leaf node.
    EXPECT_THAT(
        subgraph_a.get_leaf_groups().at(0).get_leaf_nodes(), UnorderedElementsAreArray({unpacker_node_a_0_1.get()}));
    EXPECT_THAT(
        subgraph_a.get_leaf_groups().at(2).get_leaf_nodes(), UnorderedElementsAreArray({unpacker_node_a_2_1.get()}));

    // Since no serial fork is registered for the `root_to_leaf_path_index` 1, that leaf group is empty.
    EXPECT_TRUE(subgraph_a.get_leaf_groups().at(1).empty());

    // // Verify subgraph B (with subgraph ID of 2).
    EXPECT_THAT(subgraph_leaves, Contains(Key(2)));
    const LeafGroupOrder& subgraph_b = subgraph_leaves.at(2);
    EXPECT_EQ(subgraph_b.get_leaf_groups().size(), num_root_to_leaf_paths_b);

    // Both leaf groups will have the same leaf nodes because they are fed by the union node.
    std::unordered_set<DataFlowNode*> expected_leaf_nodes{
        unpacker_node_b_0_1.get(), unpacker_node_b_0_2.get(), unpacker_node_b_0_3.get()};
    EXPECT_THAT(subgraph_b.get_leaf_groups().at(0).get_leaf_nodes(), UnorderedElementsAreArray(expected_leaf_nodes));
    EXPECT_THAT(subgraph_b.get_leaf_groups().at(1).get_leaf_nodes(), UnorderedElementsAreArray(expected_leaf_nodes));

    // Empty leaf groups can be the result of gather node which gather multiple fork paths, because every fork
    // path will try to increase the subgrpah leaf ID, however only the ID of the first fork path will be propagated
    // to the leaf nodes, while all the other ones will remain empty (as the gather node will alredy be processed
    // by the first fork path, so there is no need to process it again).
    verify_empty_subgraph_leaf_group(subgraph_leaves, 1);
    verify_empty_subgraph_leaf_group(subgraph_leaves, 3);
}