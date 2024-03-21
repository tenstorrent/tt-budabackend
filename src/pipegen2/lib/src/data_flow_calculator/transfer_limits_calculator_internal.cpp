// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfer_limits_calculator_internal.h"

#include <numeric>

#include "model/data_flow/data_flow_node.h"
#include "utils/logger.hpp"

namespace pipegen2
{
namespace data_flow_internal
{

bool find_single_source_paths(DataFlowNode* df_node, bool is_upstream_single_source)
{
    if (!df_node->is_on_single_source_path())
    {
        return false;
    }

    bool is_single_source_path = is_upstream_single_source;

    if (df_node->is_scatter() || df_node->get_total_num_inputs() > 1)
    {
        is_single_source_path = false;
    }

    for (DataFlowNode* dest_node : df_node->get_destinations())
    {
        bool is_downstream_single_source_path = find_single_source_paths(dest_node, is_single_source_path);
        is_single_source_path = is_single_source_path && is_downstream_single_source_path;
    }

    df_node->set_is_on_single_source_path(is_single_source_path);

    return is_single_source_path;
}

void calculate_num_iterations(const std::vector<DataFlowNode*>& leaf_nodes,
                              const std::vector<DataFlowNode*>& root_nodes,
                              unsigned int max_num_tiles_per_phase,
                              unsigned int max_num_phases_per_iteration)
{
    if (can_all_leaf_nodes_do_epoch_in_single_iteration(leaf_nodes))
    {
        set_num_iterations_for_graph(root_nodes, 1 /* num_iterations */);
    }
    else if (are_all_leaf_nodes_on_single_source_path(leaf_nodes))
    {
        // All leaf nodes are on single source path and are expected to have the same single root node (since gather
        // is not supported on single source paths).
        unsigned int num_iterations = calculate_num_iterations_for_single_source_path(
            root_nodes[0], leaf_nodes, max_num_tiles_per_phase, max_num_phases_per_iteration);
        set_num_iterations_for_graph(root_nodes, num_iterations);
    }
    else
    {
        calculate_num_iterations_for_multi_source_path(root_nodes);
    }
}

bool can_all_leaf_nodes_do_epoch_in_single_iteration(const std::vector<DataFlowNode*>& leaf_nodes)
{
    return std::all_of(leaf_nodes.begin(), leaf_nodes.end(), [](const DataFlowNode* leaf_node)
        {
            return leaf_node->can_do_epoch_in_single_iteration();
        });
}

bool are_all_leaf_nodes_on_single_source_path(const std::vector<DataFlowNode*>& leaf_nodes)
{
    return std::all_of(leaf_nodes.begin(), leaf_nodes.end(), [](const DataFlowNode* leaf_node)
        {
            return leaf_node->is_on_single_source_path();
        });
}

unsigned int calculate_num_iterations_for_single_source_path(
    const DataFlowNode* root_node,
    const std::vector<DataFlowNode*>& leaf_nodes,
    unsigned int max_num_tiles_per_phase,
    unsigned int max_num_phases_per_iteration)
{
    const unsigned int epoch_tiles = root_node->get_num_epoch_tiles();

    // If receiver can handle epoch_tiles so that they fit in less or equal than max_num_phases_per_iteration
    // phases, it can do it in one iteration as well.
    // Note that sending nodes can send chunks of maximum max_num_tiles_per_phase, and that division tells us how
    // many phases are actually needed to transfer epoch_tiles provided we can handle it in one iteration.
    // TODO: In case when num_epoch_tiles % max_num_tiles_per_phase > 0 we would technically need
    // (epoch_tiles / max_num_tiles_per_phase + 1) phases, so instead of int div, ceil int div should be used, but
    // legacy code doesn't check for that.
    unsigned int num_phases_required = epoch_tiles / max_num_tiles_per_phase;
    if (num_phases_required <= max_num_phases_per_iteration)
    {
        return 1;
    }

    // If none of the above works out, we have to figure out how many iterations we will need in order for all
    // leaf nodes to receive epoch_tiles. Ideal num of iterations can be calculated if we assume nodes are sending
    // max num of allowed tiles per phase and are utilizing max allowed num of phases per iteration.
    // TODO: same here: ceil division should be used, but legacy code uses / instead, so it is left as is.
    unsigned int ideal_num_iterations = epoch_tiles / (max_num_phases_per_iteration * max_num_tiles_per_phase);
    unsigned int clearing_size = get_single_source_path_clearing_granularity(root_node, leaf_nodes);

    // Ideally, we can do it in ideal_num_iterations or below.
    for (unsigned int num_iterations = ideal_num_iterations; num_iterations > 1; --num_iterations)
    {
        if (number_of_iterations_satisfies_divisibility_constraints(num_iterations, epoch_tiles, clearing_size))
        {
            return num_iterations;
        }
    }

    // If none of those satisfy divisibility constraints, we search for first num_iterations higher than
    // ideal_num_iterations that satisfies those constraints. We want minimal number because iterations are
    // requiring firmware control, therefore they are expensive.
    for (unsigned int num_iterations = ideal_num_iterations + 1; num_iterations < epoch_tiles; ++num_iterations)
    {
        if (number_of_iterations_satisfies_divisibility_constraints(num_iterations, epoch_tiles, clearing_size))
        {
            return num_iterations;
        }
    }

    // In worst case, epoch_tiles are sent one by one.
    return epoch_tiles;
}

bool number_of_iterations_satisfies_divisibility_constraints(
    unsigned int num_iterations, unsigned int num_epoch_tiles, unsigned int tile_clear_granularity)
{
    return num_epoch_tiles % num_iterations == 0 &&
           (num_epoch_tiles / num_iterations) % tile_clear_granularity == 0;
}

unsigned int get_single_source_path_clearing_granularity(
    const DataFlowNode* root_node, const std::vector<DataFlowNode*>& leaf_nodes)
{
    // Calculate clearing granularity on this single source path to ensure that the number of tiles transfered
    // satisfies divisibility constraints of root and all leaf nodes.
    unsigned int clearing_granularity = 1;
    for (const DataFlowNode* leaf_node : leaf_nodes)
    {
        clearing_granularity = std::lcm(
            clearing_granularity, get_single_source_path_clearing_granularity(root_node, leaf_node));
    }

    return clearing_granularity;
}

unsigned int get_single_source_path_clearing_granularity(const DataFlowNode* root_node, const DataFlowNode* leaf_node)
{
    // If the single source path ends in DRAM, we need to ensure that number of messages per phase is divisible by
    // the dram write granularity (which is the scatter gather num tiles of the source node), otherwise NCRISC will
    // hang.
    // In other cases, we need to ensure that the number of messages is divisible by the consume granularity of the
    // leaf node.
    return leaf_node->is_dram_or_pcie_output() ? root_node->get_scatter_gather_num_tiles() :
                                                 leaf_node->get_consume_granularity();
}

void set_num_iterations_for_graph(const std::vector<DataFlowNode*>& root_nodes, const int num_iterations)
{
    std::unordered_set<DataFlowNode*> visited_nodes;
    auto set_num_iterations = [num_iterations](DataFlowNode* node)
    {
        node->set_num_iterations(num_iterations);
    };
    visit_nodes_in_topological_order(root_nodes, set_num_iterations, visited_nodes);
}

void calculate_num_iterations_for_multi_source_path(const std::vector<DataFlowNode*>& root_nodes)
{
    std::unordered_set<DataFlowNode*> visited_nodes;

    // Calculate the number of iterations first for root nodes. Then every node will calculate its number of
    // iterations based on its input node and its own repeat and serialization factors.
    auto set_num_iterations = [](DataFlowNode* node)
    {
        if (node->is_root_node())
        {
            node->set_num_iterations(node->get_num_epoch_tiles() / node->get_scatter_gather_num_tiles());
            return;
        }

        const DataFlowNode* input_node = node->get_first_input_node();
        const unsigned int input_num_iterations = input_node->get_num_iterations();

        node->set_num_iterations(
            input_num_iterations * node->get_repeat_factor() / node->get_serialization_factor());
    };
    visit_nodes_in_topological_order(root_nodes, set_num_iterations, visited_nodes);
}

void calculate_tiles_to_send(DataFlowNode* df_node,
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

void calculate_number_of_paths_through_node(DataFlowNode* df_node,
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

void calculate_subtree_common_divisor(DataFlowNode* df_node,
                                      std::unordered_set<DataFlowNode*>& visited_nodes)
{
    if (df_node->is_leaf_node())
    {
        df_node->set_subtree_common_divisor(df_node->get_consume_granularity());
        visited_nodes.insert(df_node);
        return;
    }

    unsigned int common_divisor = 1;
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

void calculate_max_tiles_per_phase(DataFlowNode* root_node,
                                   std::unordered_set<DataFlowNode*>& visited_nodes,
                                   unsigned int upper_limit_max_num_tiles_per_phase)
{
    log_assert(root_node->is_root_node(),
               "Expecting root node argument to start with max_tiles_per_phase calculation but got node {}",
               root_node->get_id());

    unsigned int root_num_tiles_per_input = root_node->get_num_tiles_per_input();

    auto set_max_num_tiles_per_phase =
        [root_num_tiles_per_input, upper_limit_max_num_tiles_per_phase](DataFlowNode* df_node)
        {
            calculate_max_tiles_per_phase_for_node(
                df_node, root_num_tiles_per_input, upper_limit_max_num_tiles_per_phase);
        };
    visit_nodes_in_topological_order({root_node}, set_max_num_tiles_per_phase, visited_nodes);
}

void calculate_max_tiles_per_phase_for_node(DataFlowNode* df_node,
                                            unsigned int root_num_tiles_per_input,
                                            unsigned int upper_limit_max_num_tiles_per_phase)
{
    if (is_end_of_single_source_edge(df_node))
    {
        const DataFlowNode* parent_node = df_node->get_first_input_node();
        df_node->set_max_num_tiles_per_phase(parent_node->get_max_num_tiles_per_phase());
        return;
    }

    // Starting value for root nodes not reading from DRAM or PCIe is their size in tiles.
    unsigned int common_divisor = df_node->is_root_node() && !df_node->is_dram_or_pcie_input() ?
                                  df_node->get_size_tiles() : 1;

    // If more than max_num_tiles_per_phase tiles flows through this node, we have to ensure that we send at
    // the granularity of the subtree common divisor.
    if (df_node->get_tiles_to_send() > upper_limit_max_num_tiles_per_phase)
    {
        common_divisor = std::lcm(common_divisor, df_node->get_subtree_common_divisor());
    }

    // If number of tiles per input of the root node of the path that the given data flow node is on is less than
    // max tiles per phase, try to ensure that we send at the granularity of the root's tiles per input.
    if (root_num_tiles_per_input <= upper_limit_max_num_tiles_per_phase)
    {
        unsigned int lcm_with_tiles_per_input = std::lcm(common_divisor, root_num_tiles_per_input);
        if (lcm_with_tiles_per_input > upper_limit_max_num_tiles_per_phase)
        {
            log_warning(tt::LogPipegen2, "Pipegen cannot ensure divisibility with "
                        "root_num_tiles_per_input - therefore fork-join paths may hang");
        }
        else
        {
            common_divisor = lcm_with_tiles_per_input;
        }
    }

    df_node->set_max_num_tiles_per_phase((upper_limit_max_num_tiles_per_phase / common_divisor) * common_divisor);
}

// TODO: make use of this function in find_single_source_paths()
bool is_end_of_single_source_edge(DataFlowNode* node)
{
    if (node->is_root_node() || node->get_total_num_inputs() > 1)
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

void visit_nodes_in_topological_order(const std::vector<DataFlowNode*>& root_nodes,
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

void topological_order_helper_dfs(DataFlowNode* node,
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

} // namespace data_flow_internal
} // namespace pipegen2
