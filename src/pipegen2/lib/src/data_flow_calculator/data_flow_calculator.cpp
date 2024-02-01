// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/data_flow_calculator.h"

#include <memory>
#include <numeric>
#include <vector>
#include <unordered_set>

#include "data_flow_calculator/data_flow_calculator_internal.h"
#include "data_flow_calculator/subgraph_leaf_groups_finder.h"
#include "model/data_flow/subgraph_leaf_groups.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    using namespace data_flow_internal;

    DataFlowCalculator::DataFlowCalculator(const RationalGraph* rg, unsigned int max_num_tiles_per_phase) :
        m_rational_graph(rg),
        m_max_num_tiles_per_phase(max_num_tiles_per_phase)
    {
        for (const std::unique_ptr<RGBaseNode>& rg_node : rg->get_nodes())
        {
            m_rg_node_2_df_node.emplace(rg_node.get(), std::make_unique<DataFlowNode>(rg_node.get()));
        }

        for (const std::unique_ptr<RGBasePipe>& rg_pipe : rg->get_pipes())
        {
            std::unordered_set<DataFlowNode*> seen_source_nodes;
            for (const PipeInput& pipe_input : rg_pipe->get_inputs())
            {
                const RGBaseNode* rg_node = pipe_input.get_node();
                DataFlowNode* source_node = m_rg_node_2_df_node.at(rg_node).get();

                for (const RGBaseNode* pipe_out_node : rg_pipe->get_output_nodes())
                {
                    DataFlowNode* dest_node = m_rg_node_2_df_node.at(pipe_out_node).get();
                    dest_node->add_input(source_node, pipe_input.get_offset());

                    // Prevent creating multiple source -> dest edges in the data flow graph if the source is appearing
                    // multiple times in the pipe's input list.
                    if (!seen_source_nodes.count(source_node))
                    {
                        source_node->add_dest(dest_node);
                        dest_node->add_source(source_node);
                    }
                }
                seen_source_nodes.insert(source_node);
            }
        }

        split_into_connected_components();
    }

    DataFlowInfo DataFlowCalculator::get_data_flow_info()
    {
        // All data flow calculations conducted on separate DataFlowGraph will be summed up in `result`.
        DataFlowInfo result;

        for (std::unique_ptr<DataFlowGraph>& df_graph : m_connected_components)
        {
            // Skipping single node (isolated) graphs.
            if (df_graph->get_nodes().size() == 1)
            {
                continue;
            }
            result += get_data_flow_info(df_graph.get());
        }

        return result;
    }

    DataFlowInfo DataFlowCalculator::get_data_flow_info(const DataFlowGraph* df_graph)
    {
        std::vector<DataFlowNode*> root_nodes = find_root_nodes(df_graph);
        log_assert(!root_nodes.empty(), "Expecting to find at least one root node in a subgraph");

        find_direct_paths(root_nodes);

        std::vector<DataFlowNode*> leaf_nodes = find_leaf_nodes(df_graph);
        log_assert(!leaf_nodes.empty(), "Expecting to find at least one leaf node in a subgraph");

        calculate_num_iterations(leaf_nodes, root_nodes);

        calculate_tiles_to_send(leaf_nodes);

        calculate_number_of_paths_through_node(leaf_nodes);

        calculate_subtree_common_divisor(root_nodes);

        calculate_max_tiles_per_phase(root_nodes);

        std::unique_ptr<SubgraphLeafGroups> leaf_group_cluster = find_leaf_groups(root_nodes);

        calculate_phases(leaf_group_cluster.get());

        return create_data_flow_info(df_graph);
    }

    void DataFlowCalculator::find_direct_paths(const std::vector<DataFlowNode*> root_nodes)
    {
        for (DataFlowNode* df_node : root_nodes)
        {
            find_direct_paths(df_node, true /* is_upstream_direct */);
        }
    }

    bool DataFlowCalculator::find_direct_paths(DataFlowNode* df_node, bool is_upstream_direct)
    {
        if (!df_node->is_on_direct_path())
        {
            return false;
        }

        bool is_direct_path = is_upstream_direct;
        if (df_node->is_scatter())
        {
            is_direct_path = false;
        }

        if (!df_node->is_root_node() && df_node->get_total_num_inputs() > 1)
        {
            is_direct_path = false;
        }
        for (DataFlowNode* dest_node : df_node->get_destinations())
        {
            bool is_downstream_direct_path = find_direct_paths(dest_node, is_direct_path);
            is_direct_path = is_direct_path && is_downstream_direct_path;
        }

        df_node->set_is_on_direct_path(is_direct_path);

        return df_node->is_on_direct_path();
    }

    void DataFlowCalculator::calculate_num_iterations(const std::vector<DataFlowNode*>& leaf_nodes,
                                                      const std::vector<DataFlowNode*>& root_nodes)
    {
        std::unordered_set<DataFlowNode*> visited_nodes;

        bool only_direct_paths = leaf_nodes[0]->is_on_direct_path();
        bool single_iteration = true;
        for (const DataFlowNode* leaf_node : leaf_nodes)
        {
            only_direct_paths &= leaf_node->is_on_direct_path();
            single_iteration &= leaf_node->can_do_epoch_in_single_iteration();
        }

        // If all leaf nodes are on direct path, then we can calculate the number of iterations on a single leaf and use
        // it for all leaf nodes. Otherwise, we need to calculate the number of iterations for each node separately.
        if (only_direct_paths || single_iteration)
        {
            int num_iterations = single_iteration ? 1 : calculate_num_iterations(leaf_nodes[0]);
            auto set_num_iterations = [num_iterations](DataFlowNode* node)
            {
                node->set_num_iterations(num_iterations);
            };
            visit_nodes_in_topological_order(root_nodes, set_num_iterations, visited_nodes);
        }
        else
        {
            // Calculate the number of iterations first for root nodes. Then every node will calculate its number of
            // iterations based on its input node and its own repeat and serialization factors.
            auto set_num_iterations = [this](DataFlowNode* node)
            {
                if (node->is_intermediate())
                {
                    node->set_num_iterations(1);
                    return;
                }

                if (node->is_root_node())
                {
                    node->set_num_iterations(node->get_num_epoch_tiles() / node->get_scatter_gather_num_tiles());
                    return;
                }

                const DataFlowNode* input_node = node->get_first_input_node();
                const uint32_t input_num_iterations = input_node->get_num_iterations();

                node->set_num_iterations(
                    input_num_iterations * node->get_repeat_factor() / node->get_serialization_factor());
            };
            visit_nodes_in_topological_order(root_nodes, set_num_iterations, visited_nodes);
        }
    }

    void DataFlowCalculator::calculate_tiles_to_send(const std::vector<DataFlowNode*>& leaf_nodes)
    {
        std::unordered_set<DataFlowNode*> visited_nodes;
        for (DataFlowNode* leaf_node : leaf_nodes)
        {
            calculate_tiles_to_send(leaf_node, visited_nodes);
        }
    }

    void DataFlowCalculator::calculate_number_of_paths_through_node(const std::vector<DataFlowNode*>& leaf_nodes)
    {
        std::unordered_set<DataFlowNode*> visited_nodes;
        for (DataFlowNode* leaf_node : leaf_nodes)
        {
            calculate_number_of_paths_through_node(leaf_node, visited_nodes);
        }
    }

    void DataFlowCalculator::calculate_subtree_common_divisor(const std::vector<DataFlowNode*>& root_nodes)
    {
        std::unordered_set<DataFlowNode*> visited_nodes;
        for (DataFlowNode* root_node : root_nodes)
        {
            calculate_subtree_common_divisor(root_node, visited_nodes);
        }
    }

    void DataFlowCalculator::calculate_max_tiles_per_phase(const std::vector<DataFlowNode*>& root_nodes)
    {
        std::unordered_set<DataFlowNode*> visited_nodes;
        for (DataFlowNode* root_node : root_nodes)
        {
            calculate_max_tiles_per_phase(root_node, visited_nodes);
        }
    }

    std::unique_ptr<SubgraphLeafGroups> DataFlowCalculator::find_leaf_groups(
        const std::vector<DataFlowNode*>& root_nodes)
    {
        SubgraphLeafGroupsFinder subgraph_leaf_groups_finder;
        return subgraph_leaf_groups_finder.find_leaf_groups(root_nodes);
    }

    void DataFlowCalculator::calculate_phases(const SubgraphLeafGroups* subgraph_leaf_groups)
    {
        for (const auto& [subgraph_id, subgraph_leaf_cluster] : subgraph_leaf_groups->get_subgraph_leaves())
        {
            uint32_t start_phase_offset = 0;
            uint32_t phase_offset = 0;
            for (const auto& leaf_group : subgraph_leaf_cluster.get_leaf_groups())
            {
                if (leaf_group.empty())
                {
                    continue;
                }

                // All leaves in a single leaf group will have the exact same phases, so we only compute the phases
                // for the first leaf, and copy computed phases to other leaf nodes at the end.
                DataFlowNode* first_leaf_node = *(leaf_group.get_leaf_nodes().begin());

                start_phase_offset += calculate_phases(
                    first_leaf_node, 0 /* data_offset */, nullptr /* dest */, start_phase_offset);;
            }
        }

        copy_calculated_phases_per_leaf_group(subgraph_leaf_groups);
    }

    void DataFlowCalculator::copy_calculated_phases_per_leaf_group(const SubgraphLeafGroups* subgraph_leaf_groups)
    {
        for (const auto& [subgraph_id, subgraph_leaf_cluster] : subgraph_leaf_groups->get_subgraph_leaves())
        {
            for (const auto& leaf_group : subgraph_leaf_cluster.get_leaf_groups())
            {
                if (leaf_group.empty())
                {
                    continue;
                }

                copy_calculated_phases_per_leaf_group(leaf_group);
            }
        }
    }

    void DataFlowCalculator::copy_calculated_phases_per_leaf_group(const LeafGroup& leaf_group)
    {
        const std::unordered_set<DataFlowNode*>& leaf_nodes = leaf_group.get_leaf_nodes();

        auto leaf_node_it = leaf_nodes.begin();
        DataFlowNode* first_leaf_node = *leaf_node_it;

        const std::vector<DataFlowNode*>& source_nodes = first_leaf_node->get_sources();

        ++leaf_node_it;
        for (; leaf_node_it != leaf_nodes.end(); ++leaf_node_it)
        {
            std::map<unsigned int, std::vector<PhaseInfo>> receiving_phases_per_input_group =
                first_leaf_node->get_receiving_phases_per_input_group();

            DataFlowNode* leaf_node = *leaf_node_it;
            leaf_node->set_receiving_phases_per_input_group(std::move(receiving_phases_per_input_group));

            for (DataFlowNode* source_node : source_nodes)
            {
                std::vector<PhaseInfo> sending_phases = source_node->get_sending_phases(first_leaf_node);
                source_node->set_sending_phases_to_dest(leaf_node, std::move(sending_phases));
            }
        }
    }

    DataFlowInfo DataFlowCalculator::create_data_flow_info(const DataFlowGraph* data_flow_graph)
    {
        DataFlowInfo result;
        for (DataFlowNode* df_node : data_flow_graph->get_nodes())
        {
            for (DataFlowNode* dest : df_node->get_destinations())
            {
                result.set_edge_phases(df_node->get_rg_node(), dest->get_rg_pipe(), df_node->get_sending_phases(dest));
            }
            if (!df_node->is_root_node())
            {
                result.set_edge_phases(df_node->get_rg_pipe(),
                                       df_node->get_rg_node(),
                                       df_node->get_receiving_phases_for_all_input_groups());
                result.set_pipe_num_iterations(df_node->get_rg_pipe(), df_node->get_num_iterations());
                result.set_pipe_subtree_divisor(df_node->get_rg_pipe(), df_node->get_subtree_common_divisor());
            }
            result.set_node_max_num_tiles_per_phase(df_node->get_rg_node(), df_node->get_max_num_tiles_per_phase());
            result.set_node_msgs_to_send(df_node->get_rg_node(), df_node->get_tiles_to_send());
        }

        return result;
    }

    // TODO: make use of this function in find_direct_paths()
    bool DataFlowCalculator::is_end_of_direct_edge(DataFlowNode* node)
    {
        if (node->is_root_node())
        {
            return false;
        }
        if (node->get_total_num_inputs() > 1)
        {
            return false;
        }
        const DataFlowNode* parent_node = node->get_first_input_node();
        if (parent_node->is_scatter())
        {
            return false;
        }
        return true;
    }

    uint32_t DataFlowCalculator::calculate_num_iterations(DataFlowNode* leaf_node)
    {
        if (leaf_node->can_do_epoch_in_single_iteration())
        {
            return 1;
        }

        // leaf_node is on direct path. Traverse up to the only root node to get num of epoch tiles it is sending.
        DataFlowNode* root = leaf_node;
        while (!root->is_root_node())
        {
            root = root->get_first_input_node();
        }

        const uint32_t epoch_tiles = root->get_num_epoch_tiles();

        // If receiver can handle epoch_tiles so that they fit in less or equal than c_max_num_phases_per_iteration
        // phases, it can do it in one iteration as well.
        // Note that sending nodes can send chunks of maximum m_max_num_tiles_per_phase, and that division tells us how
        // many phases are actually needed to transfer epoch_tiles provided we can handle it in one iteration.
        // TODO: In case when num_epoch_tiles % m_max_num_tiles_per_phase > 0 we would technically need
        // (epoch_tiles / m_max_num_tiles_per_phase + 1) phases, so instead of int div, ceil int div should be used, but
        // legacy code doesn't check for that.
        uint32_t num_phases_required = epoch_tiles / m_max_num_tiles_per_phase;
        if (num_phases_required <= c_max_num_phases_per_iteration)
        {
            return 1;
        }

        // If none of the above works out, we have to figure out how many iterations we will need in order for this
        // leaf_node to receive epoch_tiles. Ideal num of iterations can be calculated if we assume nodes are sending
        // max num of allowed tiles per phase and are utilizing max allowed num of phases per iteration.
        // TODO: same here: ceil division should be used, but legacy code uses / instead, so it is left as is.
        uint32_t ideal_num_iterations = epoch_tiles / (c_max_num_phases_per_iteration * m_max_num_tiles_per_phase);
        uint32_t clearing_size = get_direct_path_clearing_granularity(root, leaf_node);

        // Ideally, we can do it in ideal_num_iterations or below.
        for (uint32_t num_iterations = ideal_num_iterations; num_iterations > 1; --num_iterations)
        {
            if (number_of_iterations_satisfies_divisibility_constraints(num_iterations, epoch_tiles, clearing_size))
            {
                return num_iterations;
            }
        }

        // If none of those satisfy divisibility constraints, we search for first num_iterations higher than
        // ideal_num_iterations that satisfies those constraints. We want minimal number because iterations are
        // requiring firmware control, therefore they are expensive.
        for (uint32_t num_iterations = ideal_num_iterations + 1; num_iterations < epoch_tiles; ++num_iterations)
        {
            if (number_of_iterations_satisfies_divisibility_constraints(num_iterations, epoch_tiles, clearing_size))
            {
                return num_iterations;
            }
        }

        // In worst case, epoch_tiles are sent one by one.
        return epoch_tiles;
    }

    bool DataFlowCalculator::number_of_iterations_satisfies_divisibility_constraints(
        uint32_t num_iterations, uint32_t num_epoch_tiles, uint32_t tile_clear_granularity)
    {
        return num_epoch_tiles % num_iterations == 0 &&
               (num_epoch_tiles / num_iterations) % tile_clear_granularity == 0;
    }

    uint32_t DataFlowCalculator::get_direct_path_clearing_granularity(const DataFlowNode* root_node,
                                                                      const DataFlowNode* leaf_node)
    {
        // If the direct path ends in DRAM, we need to ensure that number of messages per phase is divisible by the
        // dram write granularity (which is the scatter gather num tiles of the source node), otherwise NCRISC will
        // hang.
        // In other cases, we need to ensure that the number of messages is divisible by the consume granularity of the
        // leaf node.
        return leaf_node->is_dram_or_pcie_output() ? root_node->get_scatter_gather_num_tiles() :
                                                     leaf_node->get_consume_granularity();
    }

    void DataFlowCalculator::calculate_tiles_to_send(DataFlowNode* df_node,
                                                     std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        if (df_node->is_root_node())
        {
            df_node->set_tiles_to_send(df_node->get_num_epoch_tiles() / df_node->get_num_iterations());
            visited_nodes.insert(df_node);
            return;
        }

        int tiles_to_send = 0;
        for (const DataFlowNodeInput& input : df_node->get_all_inputs())
        {
            DataFlowNode* upstream_node = input.get_node();
            if (!visited_nodes.count(upstream_node))
            {
                calculate_tiles_to_send(upstream_node, visited_nodes);
            }
            tiles_to_send += upstream_node->get_tiles_to_send();
        }

        visited_nodes.insert(df_node);
        df_node->set_tiles_to_send(tiles_to_send / df_node->get_input_group_count());
    }

    void DataFlowCalculator::calculate_number_of_paths_through_node(DataFlowNode* df_node,
                                                                    std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        if (df_node->is_root_node())
        {
            visited_nodes.insert(df_node);
            df_node->set_number_of_unique_df_paths(1);
            return;
        }

        unsigned int max_number_of_unique_df_paths_through_node = df_node->get_input_group_count();
        for (DataFlowNode* upstream_node : df_node->get_sources())
        {
            if (!visited_nodes.count(upstream_node))
            {
                calculate_number_of_paths_through_node(upstream_node, visited_nodes);
            }
            max_number_of_unique_df_paths_through_node = std::max(max_number_of_unique_df_paths_through_node,
                                                                  upstream_node->get_number_of_unique_df_paths());
        }

        visited_nodes.insert(df_node);
        df_node->set_number_of_unique_df_paths(max_number_of_unique_df_paths_through_node);
    }

    void DataFlowCalculator::calculate_subtree_common_divisor(DataFlowNode* df_node,
                                                              std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        if (df_node->is_leaf_node())
        {
            df_node->set_subtree_common_divisor(df_node->get_consume_granularity());
            visited_nodes.insert(df_node);
            return;
        }

        uint32_t common_divisor = 1;
        for (const auto dest : df_node->get_destinations())
        {
            if (!visited_nodes.count(dest))
            {
                calculate_subtree_common_divisor(dest, visited_nodes);
            }
            common_divisor = std::lcm(common_divisor, dest->get_subtree_common_divisor());
        }

        visited_nodes.insert(df_node);
        df_node->set_subtree_common_divisor(common_divisor);
    }

    void DataFlowCalculator::calculate_max_tiles_per_phase(DataFlowNode* df_node,
                                                           std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        ASSERT(df_node->is_root_node(), "Expecting root node argument to start with max_tiles_per_phase calculation");

        uint32_t num_tiles_per_input = df_node->get_num_tiles_per_input();

        auto set_max_num_tiles_per_phase = [this, &num_tiles_per_input](DataFlowNode* df_node)
        {
            if (is_end_of_direct_edge(df_node))
            {
                const DataFlowNode* parent_node = df_node->get_first_input_node();
                df_node->set_max_num_tiles_per_phase(parent_node->get_max_num_tiles_per_phase());
                return;
            }

            // Starting value for root nodes not reading from DRAM or PCIe is their size in tiles.
            uint32_t common_divisor = df_node->is_root_node() && !df_node->is_dram_or_pcie_input() ?
                                      df_node->get_size_tiles() : 1;

            // If more than m_max_num_tiles_per_phase tiles flows through this node, we have to ensure that we send at
            // the granularity of the subtree common divisor.
            if (df_node->get_tiles_to_send() > m_max_num_tiles_per_phase)
            {
                common_divisor = std::lcm(common_divisor, df_node->get_subtree_common_divisor());
            }

            if (num_tiles_per_input <= m_max_num_tiles_per_phase)
            {
                uint32_t lcm_with_tiles_per_input = std::lcm(common_divisor, num_tiles_per_input);
                if (lcm_with_tiles_per_input > m_max_num_tiles_per_phase)
                {
                    log_warning(tt::LogPipegen2, "Pipegen cannot ensure divisibility with "
                            "num_tiles_per_input - therefore fork-join paths may hang");
                }
                else
                {
                    common_divisor = lcm_with_tiles_per_input;
                }
            }

            df_node->set_max_num_tiles_per_phase((m_max_num_tiles_per_phase / common_divisor) * common_divisor);
        };

        visit_nodes_in_topological_order({df_node}, set_max_num_tiles_per_phase, visited_nodes);
    }

    int DataFlowCalculator::calculate_phases(DataFlowNode* df_node,
                                             const uint32_t data_offset,
                                             DataFlowNode* dest,
                                             const uint32_t start_phase_offset)
    {
        if (df_node->is_root_node())
        {
            return calculate_phases_for_root(df_node, data_offset, dest, start_phase_offset);
        }

        if (!df_node->has_processed_all_inputs())
        {
            // Save the calculated offset so we don't go through its inputs again.
            df_node->set_inputs_phase_offset(calculate_receiving_phases(df_node, data_offset, start_phase_offset));
        }

        if (dest)
        {
            calculate_sending_phases(df_node, data_offset, dest, start_phase_offset);
        }

        if (!df_node->has_processed_all_inputs())
        {
            df_node->move_to_next_input_group();
        }

        return df_node->get_inputs_phase_offset();
    }

    int DataFlowCalculator::calculate_phases_for_root(DataFlowNode* df_node,
                                                      uint32_t data_offset,
                                                      DataFlowNode* dest,
                                                      uint32_t start_phase_offset)
    {
        ASSERT(dest, "Expecting destination node to be set for root node");
        int tiles_remaining_to_send = df_node->get_tiles_to_send();
        uint32_t num_msgs_in_cur_phase = tiles_remaining_to_send > df_node->get_max_num_tiles_per_phase() ?
                                         df_node->get_max_num_tiles_per_phase() : tiles_remaining_to_send;
        uint32_t sending_phase_offset = start_phase_offset;
        while (tiles_remaining_to_send)
        {
            df_node->add_sending_phase(dest, sending_phase_offset, data_offset, num_msgs_in_cur_phase);
            tiles_remaining_to_send -= num_msgs_in_cur_phase;
            ++sending_phase_offset;
            num_msgs_in_cur_phase = tiles_remaining_to_send > df_node->get_max_num_tiles_per_phase() ?
                                    df_node->get_max_num_tiles_per_phase() : tiles_remaining_to_send;
        }
        return sending_phase_offset - start_phase_offset;
    }

    int DataFlowCalculator::calculate_receiving_phases(DataFlowNode* df_node,
                                                       uint32_t data_offset,
                                                       uint32_t start_phase_offset)
    {
        // Phase offset starts from the given phase offset and it is increased for every input into this node.
        // The amount of increment is determined recursively for every input.
        uint32_t inputs_phase_offset = start_phase_offset;

        DataFlowNode* prev_sender = nullptr;
        uint32_t prev_data_offset = 0;

        const DataFlowNodeInputRange df_inputs_to_process = df_node->get_current_inputs_to_process();
        for (auto it = df_inputs_to_process.begin(); it != df_inputs_to_process.end(); ++it)
        {
            const DataFlowNodeInput& input = *it;
            DataFlowNode* current_sender = input.get_node();
            const uint32_t current_data_offset = input.get_offset();

            // Whether we can accumulate messages to the same phase on the sender side. It is important to call
            // try_update_last_sending_phase() after we check if we can accumulate messages on the receiver side.
            // TODO: refactor try_update_last_sending_phase() into 2 functions: can_accumulate() and update().
            const bool can_sender_accumulate =
                (prev_sender == current_sender) &&
                current_sender->should_accumulate_sending_phases() &&
                ((prev_data_offset + prev_sender->get_tiles_to_send()) == current_data_offset) &&
                df_node->can_expand_last_receiving_phase(current_sender->get_tiles_to_send()) &&
                current_sender->try_update_last_sending_phase(df_node);

            // Save the index of the last phase we have assigned to this node from the current sender.
            uint32_t prev_sender_phase_idx = current_sender->get_num_sending_phases(df_node);

            if (!can_sender_accumulate)
            {
                inputs_phase_offset += calculate_phases(
                    current_sender, current_data_offset, df_node, inputs_phase_offset);
            }

            if (prev_sender != current_sender)
            {
                // If source of data changes, new phases have to be assigned to this node.
                const std::vector<PhaseInfo>& sender_phases = current_sender->get_sending_phases(df_node);

                // Assign new phases to this node starting after the last phase we have assigned corresponding to
                // the current sender.
                for (uint32_t i = prev_sender_phase_idx; i < sender_phases.size(); ++i)
                {
                    df_node->add_receiving_phase(sender_phases[i].phase_offset, sender_phases[i].num_msgs);
                }
            }
            else
            {
                // Try updating the number of messages received in the last phase from the current sender. If not
                // possible, add a new phase at the offset of the last phase from the current sender.
                df_node->update_receiving_phases(inputs_phase_offset - 1, current_sender->get_tiles_to_send());
            }

            prev_sender = current_sender;
            prev_data_offset = current_data_offset;
        }

        return inputs_phase_offset - start_phase_offset;
    }

    void DataFlowCalculator::calculate_sending_phases(DataFlowNode* df_node,
                                                      uint32_t data_offset,
                                                      DataFlowNode* dest,
                                                      uint32_t start_phase_offset)
    {
        uint32_t acc_num_msgs = 0;
        uint32_t sending_phase_offset = start_phase_offset;

        const std::vector<PhaseInfo>& receiving_phases = df_node->get_current_input_group_receiving_phases();
        for (const PhaseInfo& phase_info : receiving_phases)
        {
            if (acc_num_msgs + phase_info.num_msgs > df_node->get_max_num_tiles_per_phase())
            {
                df_node->add_sending_phase(dest, sending_phase_offset, data_offset, acc_num_msgs);
                acc_num_msgs = phase_info.num_msgs;
                sending_phase_offset = phase_info.phase_offset;
            }
            else
            {
                acc_num_msgs += phase_info.num_msgs;
            }
        }
        if (acc_num_msgs > 0)
        {
            df_node->add_sending_phase(dest, sending_phase_offset, data_offset, acc_num_msgs);
        }
    }

    void DataFlowCalculator::visit_nodes_in_topological_order(std::vector<DataFlowNode*> root_nodes,
                                                              const std::function<void(DataFlowNode*)>& visit_node_func,
                                                              std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        std::stack<DataFlowNode*> topological_ordering;
        for (DataFlowNode* root_node : root_nodes)
        {
            if (!visited_nodes.count(root_node))
            {
                topological_order_helper_dfs(root_node, visited_nodes, topological_ordering);
            }
        }

        while (!topological_ordering.empty())
        {
            visit_node_func(topological_ordering.top());
            topological_ordering.pop();
        }
    }

    void DataFlowCalculator::topological_order_helper_dfs(DataFlowNode* node,
                                                          std::unordered_set<DataFlowNode*>& visited_nodes,
                                                          std::stack<DataFlowNode*>& topological_ordering)
    {
        visited_nodes.insert(node);
        for (DataFlowNode* dest : node->get_destinations())
        {
            if (!visited_nodes.count(dest))
            {
                topological_order_helper_dfs(dest, visited_nodes, topological_ordering);
            }
        }
        topological_ordering.push(node);
    }

    void DataFlowCalculator::split_into_connected_components()
    {
        std::unordered_set<DataFlowNode*> visited_nodes;

        for (auto& kv : m_rg_node_2_df_node)
        {
            DataFlowNode* node = kv.second.get();
            if (visited_nodes.find(node) == visited_nodes.end())
            {
                m_connected_components.push_back(find_connected_subgraph(node, visited_nodes));
            }
        }
    }

    std::unique_ptr<DataFlowGraph> DataFlowCalculator::find_connected_subgraph(
        DataFlowNode* root_node,
        std::unordered_set<DataFlowNode*>& visited_nodes)
    {
        std::unique_ptr<DataFlowGraph> graph = std::make_unique<DataFlowGraph>();
        std::stack<DataFlowNode*> stack;
        DataFlowNode* current_node;

        stack.push(root_node);

        while (!stack.empty())
        {
            current_node = stack.top();
            stack.pop();

            if (visited_nodes.find(current_node) != visited_nodes.end())
            {
                continue;
            }

            // Add node to graph.
            graph->add_node(current_node);
            visited_nodes.insert(current_node);

            // Add its neighbours to stack to visit them.
            for (DataFlowNode* dest : current_node->get_destinations())
            {
                stack.push(dest);
            }
            for (DataFlowNode* src : current_node->get_sources())
            {
                stack.push(src);
            }
        }

        return graph;
    }
}
