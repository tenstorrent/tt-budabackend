// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "golden_digraph.hpp"

#include "ops/eltwise_binary_bare.hpp"
#include "ops/eltwise_unary_sfpu_bare.hpp"
#include "ops/mm_bare.hpp"
#include "netlist/netlist_utils.hpp"
#include "common/tensor_lib.hpp"
#include "ops/tm_bare.hpp"
#include "tensor.hpp"
#include "tt_backend_api_types.hpp"

using namespace netlist_utils;
namespace tt::golden {
//////////////////////
// golden_digraph
//////////////////////
golden_digraph::golden_digraph(tt_graph_info graph_info, const ARCH arch, const bool en_quantize) {
    my_graph_info = graph_info;
    en_quantize_golden = en_quantize;
    m_arch = arch;
}

void golden_digraph::add_all_nodes(std::map<string, tt_queue_wrap>& queues, tt_graph_info& graph_info) {
    // add all op nodes
    for (auto& op_info_it : graph_info.op_map) {
        insert_op_node(&op_info_it.second);  // q pointer null
    }
    log_trace(tt::LogGolden, "All op nodes created for graph: {}", graph_info.name);
    // add all queue nodes
    for (auto& q_wrap_it : queues) {
        insert_queue_node(&q_wrap_it.second);  // q pointer null
    }
    log_trace(tt::LogGolden, "All queue nodes created for graph: {}", graph_info.name);
}

void golden_digraph::remove_unconnected_nodes() {
    for (auto vd : boost::make_iterator_range(vertices(graph))) {
        if (boost::degree(vd, graph) == 0)
            boost::remove_vertex(vd, graph);
    }
}

void golden_digraph::insert_op_node(tt_op_info* op_info_ptr) {
    vertex_t vid;
    vid = boost::add_vertex(graph);
    graph[vid].my_op_info_ptr = op_info_ptr;
    graph[vid].name = op_info_ptr->name;
    graph[vid].op_not_queue = true;
}
void golden_digraph::insert_queue_node(tt_queue_wrap* q_wrap_ptr) {
    vertex_t vid;
    vid = boost::add_vertex(graph);
    graph[vid].my_queue_wrap_ptr = q_wrap_ptr;
    graph[vid].name = q_wrap_ptr->my_queue_info.name;
    graph[vid].op_not_queue = false;
}

void golden_digraph::create_ops(const unordered_map<string, tt_fused_op_info>& fused_ops_map) {
    std::deque<vertex_t> topo_order;
    boost::topological_sort(graph, std::front_inserter(topo_order));

    for (unsigned int i = 0; i < topo_order.size(); ++i) {
        if (graph[topo_order[i]].op_not_queue) {
            graph[topo_order[i]].my_op_info_ptr->my_op = create_op(graph[topo_order[i]].my_op_info_ptr, fused_ops_map);
            op_list.push_back(graph[topo_order[i]].my_op_info_ptr->my_op);
            graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr = std::make_shared<tt_tensor>();
        }
    }
}

vector<int> golden_digraph::get_input_nodes_ordered_by_input_index(int current_node) {
    // make a vector with number of elements equal to input degree of node
    vector<int> in_nodes(boost::in_degree(current_node, graph));

    // go through incoming edges and add the nodes that are driving them
    digraph_t::in_edge_iterator in_begin, in_end;
    for (boost::tie(in_begin, in_end) = in_edges(current_node, graph); in_begin != in_end; ++in_begin) {
        int input_index = graph[(*in_begin)].input_index;
        in_nodes[input_index] = boost::source(*in_begin, graph);
    }
    return (in_nodes);
}

void golden_digraph::run() {
    std::deque<vertex_t> topo_order;
    boost::topological_sort(graph, std::front_inserter(topo_order));
    for (unsigned int i = 0; i < topo_order.size(); ++i) {
        vector<int> input_nodes = get_input_nodes_ordered_by_input_index(topo_order[i]);
        vector<tt_tensor*> input_tensor_ptrs(input_nodes.size());

        if (graph[topo_order[i]].op_not_queue) {
            log_trace(
                tt::LogGolden,
                "running_op: {} on node {}",
                *graph[topo_order[i]].my_op_info_ptr,
                graph[topo_order[i]].name);

            vector<tt_tensor> tm_output_tensors(input_nodes.size());
            vector<bool> ublock_order_changed(input_nodes.size());
            auto op_ublock_order = graph[topo_order[i]].my_op_info_ptr->output_dim.ublock_order;
            // fill input tensors into input_tensor_ptrs vector
            for (unsigned int j = 0; j < input_nodes.size(); ++j) {
                input_tensor_ptrs[j] = graph[input_nodes[j]].my_golden_output_ptr.get();
                if (graph[input_nodes[j]].op_not_queue) {
                    ublock_order_changed.at(j) =
                        op_ublock_order != graph[input_nodes[j]].my_op_info_ptr->output_dim.ublock_order;
                } else {
                    ublock_order_changed.at(j) =
                        op_ublock_order != Dim::R;  // input node is from queue which is always row-major
                }
                // Copy inputs to temp tensors to perform tm ops
                log_assert(
                    graph[input_nodes[j]].my_golden_output_ptr->get_data_format() != DataFormat::Invalid,
                    "input={} data_format is invalid",
                    j);
                tm_output_tensors[j] = *graph[input_nodes[j]].my_golden_output_ptr;
                log_assert(
                    not tm_output_tensors[j].is_shape_only(),
                    "Input {} for op {} is uninitialized, missing queue settings could cause us to access out of "
                    "bounds queue.  Op Info: {}",
                    j,
                    graph[topo_order[i]].name,
                    *graph[topo_order[i]].my_op_info_ptr);

                log_trace(tt::LogGolden, "in {} tensor dims {}", j, tm_output_tensors[j].get_shape());
            }

            // Unpadding of all inputs
            for (uint32_t input = 0; input < graph[topo_order[i]].my_op_info_ptr->input_names.size(); input++) {
                const auto & in_unpad_info = graph[topo_order[i]].my_op_info_ptr->input_unpadding.at(input);
                const uint32_t unpad_r = in_unpad_info.rt;
                const uint32_t unpad_c = in_unpad_info.ct;
                if (unpad_r != 0 || unpad_c != 0) {
                    log_trace(
                        tt::LogGolden, "unpad input {} shape before: {}", input, tm_output_tensors[input].get_shape());
                    tm_output_tensors[input] = tt::tensor_lib::unpad(tm_output_tensors[input], unpad_r, unpad_c);
                    log_trace(
                        tt::LogGolden, "unpad input {} shape after: {}", input, tm_output_tensors[input].get_shape());
                }
            }

            // Apply TMs for each input
            for (const auto& it : graph[topo_order[i]].my_op_info_ptr->input_tm_ops) {
                std::uint32_t input = it.first;

                // Process list of all TM's in place
                for (const auto& tm : it.second) {
                    string tm_name = get<0>(tm);
                    if(tm_name == "pad") {
                        const auto & in_pad_info = graph[topo_order[i]].my_op_info_ptr->input_padding.at(input);
                        const float pad_val = in_pad_info.pad_value;
                        tm_output_tensors[input] =
                            tt::tensor_lib::pad_rc_val(tm_output_tensors[input], get<1>(tm).at(0), get<1>(tm).at(1), pad_val);
                    }
                    else {
                        tt_tm_config config({
                            .op = netlist_utils::get_valid_tm_op(tm_name),
                            .args = get<1>(tm),
                        });
                        log_assert(
                            (config.op != TmOp::TileBroadcast) or
                                ((input == 1) and
                                (netlist_utils::is_valid_binary_op(graph[topo_order[i]].my_op_info_ptr->type))),
                            "Can only do a tile_broadcast if it is the second input of a binary op");
                        log_trace(tt::LogGolden, "Running TM OP {} on input {}", tm_name, input);

                        // Create input tensor vector for TM op
                        vector<tt_tensor*> tm_input = {&tm_output_tensors[input]};
                        tt_tm::utils::golden_model(config, tm_input, &tm_output_tensors[input]);
                    }
                }
            }

            // Add padding
            for (uint32_t input = 0; input < graph[topo_order[i]].my_op_info_ptr->input_names.size(); input++) {
                const auto & in_pad_info = graph[topo_order[i]].my_op_info_ptr->input_padding.at(input);
                const uint32_t pad_r = in_pad_info.rt;
                const uint32_t pad_c = in_pad_info.ct;
                const float pad_val = in_pad_info.pad_value;
                if (pad_r > 0 || pad_c > 0) {
                    log_trace(
                        tt::LogGolden, "pad input {} shape before: {}", input, tm_output_tensors[input].get_shape());
                    tm_output_tensors[input] =
                        tt::tensor_lib::pad_rc_val(tm_output_tensors[input], pad_r, pad_c, pad_val);
                    log_trace(
                        tt::LogGolden, "pad input {} shape after: {}", input, tm_output_tensors[input].get_shape());
                }
            }

            std::shared_ptr<tt_op> op_ptr;
            op_ptr = graph[topo_order[i]].my_op_info_ptr->my_op;
            bool is_wormhole = ((m_arch == ARCH::WORMHOLE) or (m_arch == ARCH::WORMHOLE_B0));
            // Check if input/output tensors use bfp2/4 dataformats, enable quantization for bfp formats
            for (auto& input_tensor : tm_output_tensors) {
                bool is_bfp_df =
                    is_bfp2_format(input_tensor.get_data_format()) || is_bfp4_format(input_tensor.get_data_format());
                bool convert_float32_to_tf32 = (input_tensor.get_data_format() == DataFormat::Float32) and is_wormhole;
                // Quantize tensors if quantize_golden is enabled and Bfp4/2 is detected or it is TF32
                if (en_quantize_golden && (is_bfp_df or convert_float32_to_tf32)) {
                    if (convert_float32_to_tf32) {
                        input_tensor.set_data_format(DataFormat::Tf32);
                    }
                    input_tensor.adjust_tensor_for_accuracy();
                }
            }

            // Save prev output for gradient op
            tt_tensor* op_acc_output_tensor_ptr = nullptr;
            if (graph[topo_order[i]].my_op_info_ptr->gradient_op) {
                if (graph[topo_order[i]].my_golden_output_ptr != nullptr) {
                    tt_tensor* op_prev_output_tensor_ptr = graph[topo_order[i]].my_golden_output_ptr.get();
                    op_acc_output_tensor_ptr = new tt_tensor(op_prev_output_tensor_ptr->metadata);
                    log_trace(tt::LogGolden, "Using previous accumulator for op={}", op_ptr->name);
                    log_assert(
                        op_prev_output_tensor_ptr->get_data_format() != DataFormat::Invalid,
                        "gradient_op data_format is invalid");
                    *op_acc_output_tensor_ptr = *op_prev_output_tensor_ptr;
                }
            }
            std::vector<tt_tensor*> tm_output_tensor_ptrs = {};
            for (auto& tm_output_tensor : tm_output_tensors) {
                tm_output_tensor_ptrs.push_back(&tm_output_tensor);
            }
            // Run Golden for the OP.
            try {
                op_ptr->model(tm_output_tensor_ptrs, graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr.get());
            } catch (const std::exception& e) {
                log_error("{}", e.what());
                log_fatal("Hit Error while running op={}", graph[topo_order[i]].my_op_info_ptr->name);
            }
            // Accumulate output and delete temp tensor
            if (graph[topo_order[i]].my_op_info_ptr->gradient_op) {
                if (op_name_to_output_queue_map.find(op_ptr->name) == op_name_to_output_queue_map.end()) {
                    log_fatal(
                        "Op={} is being used as a gradient op and must output to a queue, but cannot find output "
                        "queue assosciated",
                        graph[topo_order[i]].my_op_info_ptr->name);
                }
                tt_queue_wrap output_queue = op_name_to_output_queue_map.at(op_ptr->name);

                if (op_acc_output_tensor_ptr != nullptr) {
                    if (output_queue.my_io->zero) {
                        log_trace(tt::LogGolden, "Zeroing accumulator for op={}", op_ptr->name);
                        output_queue.my_io->set_zero(false);
                    } else {
                        tt_tensor* op_output_tensor_ptr =
                            graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr.get();
                        *op_output_tensor_ptr = op_acc_output_tensor_ptr->add(*op_output_tensor_ptr);
                        delete op_acc_output_tensor_ptr;
                    }
                } else {
                    // Clear on the first iteration if zero flag is set
                    if (output_queue.my_io->zero) {
                        log_trace(tt::LogGolden, "Zeroing accumulator for op={}", op_ptr->name);
                        output_queue.my_io->set_zero(false);
                    }
                }
            }

            // Apply relu if enabled -- fused_op will handle relu in the op, so skip this one
            if (graph[topo_order[i]].my_op_info_ptr->attributes.relu_en and
                (not netlist_utils::is_valid_fused_op(graph[topo_order[i]].my_op_info_ptr->type))) {
                tensor_lib::relu_with_threshold(
                    *graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr,
                    *graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr,
                    Dim::RC,
                    graph[topo_order[i]].my_op_info_ptr->attributes.relu_mode,
                    graph[topo_order[i]].my_op_info_ptr->attributes.relu_threshold);
            }

            DataFormat output_df = graph[topo_order[i]].my_op_info_ptr->output_data_format;
            string op_type = graph[topo_order[i]].my_op_info_ptr->type;
            if (should_adjust_output_tensor_for_accuracy(output_df)) {
                tt_tensor* out = graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr.get();
                DataFormat df = output_df;
                if (op_type == "requantization" || op_type == "quantization" ||
                    graph[topo_order[i]].my_op_info_ptr->attributes.requant) {
                    df = DataFormat::Int8;
                }
                if (df == DataFormat::Int32) {
                    bool max_value_exceeded = out->check_for_max_abs_value(std::pow(2, 24));
                    if (max_value_exceeded) {
                        log_warning(
                            tt::LogOp,
                            "Op with Int32 output produced values bigger 2^24, this can lead to imprecisions in golden "
                            "implementation, due to golden implementation using float32 for integer ops");
                    }
                }
                out->set_data_format(df);
                out->adjust_tensor_for_accuracy();
            }

            // Copy to output ptr
            graph[topo_order[i]].my_golden_output_ptr = graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr;

            log_trace(
                tt::LogGolden,
                "Tensor Dump {}",
                graph[topo_order[i]].my_op_info_ptr->op_output_tensor_ptr->get_string());

        } else {
            // Run a Queue Node
            tt_queue_wrap* q_wrap = graph[topo_order[i]].my_queue_wrap_ptr;
            tt_queue_info& q_info = q_wrap->my_queue_info;

            if (q_info.type == IO_TYPE::Queue) {
                log_trace(tt::LogGolden, "Queue: {}", q_info.name);
                log_trace(tt::LogGolden, "Queue.local_rd_ptr(): {}", q_wrap->my_io->get_local_rd_ptr());
                log_trace(tt::LogGolden, "Queue.global_rd_ptr(): {}", q_wrap->my_io->get_global_rd_ptr());
                log_trace(tt::LogGolden, "Queue.entries(): {}", q_wrap->my_io->entries);
            } else {
                log_trace(tt::LogGolden, "RAM: {}", q_info.name);
                log_trace(tt::LogGolden, "ram.global_wr_ptr(): {}", q_wrap->my_io->get_global_wr_ptr());
                log_trace(tt::LogGolden, "ram.global_rd_ptr(): {}", q_wrap->my_io->get_global_rd_ptr());
                log_trace(tt::LogGolden, "ram.entries(): {}", q_wrap->my_io->entries);
            }
            // If there is an input to io, then check and push to io structure.
            if (boost::in_degree(topo_order[i], graph) != 0) {
                // IO is used as an output
                log_assert(
                    input_nodes.size() == 1,
                    "Internal Error -- Output Node for Queue={} must have 1 input when connected in Golden Backend",
                    q_info.name);
                if (q_info.input_tm_ops.size() == 0) {
                    for (unsigned int j = 0; j < input_nodes.size(); ++j) {
                        input_tensor_ptrs[j] = graph[input_nodes[j]].my_golden_output_ptr.get();
                    }               
                    q_wrap->my_io->push(std::make_shared<tt_tensor>(*input_tensor_ptrs[0]));
                } else {
                    tt_tensor tm_output_tensor;
                    // Copy input to temp tensor to perform TMs
                    tm_output_tensor = *graph[input_nodes[0]].my_golden_output_ptr; 
                    for (const auto& tm : q_info.input_tm_ops[0]) {
                        string tm_name = get<0>(tm);
                        if(tm_name == "pad") {
                            log_fatal("Queue {}: input padding not supported for queues", q_info.name);
                        }
                        else {
                            tt_tm_config config({
                                .op = netlist_utils::get_valid_tm_op(tm_name),
                                .args = get<1>(tm),
                            });
                            log_trace(tt::LogGolden, "Running TM OP {}", tm_name);
                            //
                            // Create input tensor vector for TM op
                            vector<tt_tensor*> tm_input = {&tm_output_tensor};
                            tt_tm::utils::golden_model(config, tm_input, &tm_output_tensor);
                        }
                    }
                    q_wrap->my_io->push(std::make_shared<tt_tensor>(tm_output_tensor));
                }
             }

            // If there is an output for IO, then check and copy to output_ptr
            if (boost::out_degree(topo_order[i], graph) != 0) {
                // check if node is connected
                graph[topo_order[i]].my_golden_output_ptr = q_wrap->my_io->get();
            }
        }
    }
}

bool golden_digraph::should_adjust_output_tensor_for_accuracy(DataFormat output_data_format) {
    if (output_data_format == DataFormat::Int8 || output_data_format == DataFormat::Int32) {
        // We need to limit the range of the output tensor 
        // to the range of the in type.
        return true;
    }

    if (en_quantize_golden) {
        return is_bfp2_format(output_data_format) || is_bfp4_format(output_data_format);
    }

    return false;
}

} // namespace tt::golden
