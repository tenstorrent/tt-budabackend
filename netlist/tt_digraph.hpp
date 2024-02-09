// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "common/tt_digraph_base.h"
#include "netlist_info_types.hpp"
#include "model/tensor.hpp"
#include "common/tt_queue.hpp"
#include "common/tt_ram.hpp"

struct tt_queue_producer_info_t {
    bool is_tilized = true;
    Dim ublock_scan = Dim::R;
    set<string> programs = {};
    set<string> graphs = {};
};
struct tt_queue_consumer_info_t {
    set<string> programs = {};
    set<string> graphs = {};
};
class tt_queue_wrap {
   public:
    tt_queue_info my_queue_info;
    tt_queue_producer_info_t my_producer_info;
    tt_queue_consumer_info_t my_consumer_info;
    std::shared_ptr<tt_io<std::shared_ptr<tt_tensor>>> my_io;
};
struct tt_digraph_node_struct {
    bool op_not_queue = false;
    tt_op_info* my_op_info_ptr = nullptr;
    tt_queue_wrap* my_queue_wrap_ptr = nullptr;
    std::shared_ptr<tt_tensor> my_golden_output_ptr = nullptr;
    string name = "";
};

struct tt_digraph_edge_struct {
    std::uint32_t input_index = 0;
};

class tt_digraph : public tt_digraph_base<tt_digraph_node_struct,tt_digraph_edge_struct>{
   public:
    tt_graph_info my_graph_info;
    vector<std::shared_ptr<tt_op>> op_list = {};
    std::unordered_map<std::string, tt_queue_wrap> op_name_to_output_queue_map = {};

    tt_digraph(tt_graph_info graph_info);
    tt_digraph(){};
    virtual void connect_nodes();
    virtual void remove_unconnected_nodes();
    virtual void dump_graphviz(string directory, string name);
    virtual vector<int> get_input_nodes_ordered_by_input_index(int current_node);
    virtual void create_ops(const unordered_map<string, tt_fused_op_info>& fused_ops_map);

    virtual std::shared_ptr<tt_op> create_op(tt_op_info* op_info, const unordered_map<string, tt_fused_op_info>& fused_ops_map);
    virtual void insert_op_node(tt_op_info* op_info_ptr);

    virtual void add_all_nodes(std::map<string, tt_queue_wrap>& queues, tt_graph_info& graph_info);
    virtual void insert_queue_node(tt_queue_wrap* q_wrap_ptr);
};
