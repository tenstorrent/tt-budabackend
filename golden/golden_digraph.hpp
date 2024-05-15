// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/graph/graphviz.hpp>
#include <memory>
#include <string>

#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/topological_sort.hpp"
#include "model/buffer.hpp"
#include "netlist/netlist_parser.hpp"
#include "netlist/tt_digraph.hpp"
#include "common/tt_queue.hpp"
#include "common/tt_ram.hpp"

namespace tt::golden {
class golden_digraph : public tt_digraph {
   public:
    golden_digraph(tt_graph_info graph_info, const ARCH arch, const bool en_quantize = false);
    golden_digraph(){};
    void add_all_nodes(std::map<string, tt_queue_wrap>& queues, tt_graph_info& graph_info);
    void remove_unconnected_nodes();
    void insert_op_node(tt_op_info* op_info_ptr);
    void insert_queue_node(tt_queue_wrap* q_wrap_ptr);
    vector<int> get_input_nodes_ordered_by_input_index(int current_node);
    void create_ops(const unordered_map<string, tt_fused_op_info>& fused_ops_map);
    void run();

   private:
    bool should_adjust_output_tensor_for_accuracy(DataFormat output_data_format);
    bool en_quantize_golden = false;
    ARCH m_arch = ARCH::Invalid;
};
}
