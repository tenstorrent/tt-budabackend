// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_digraph.hpp"

#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include "ops/eltwise_binary_bare.hpp"
#include "ops/eltwise_unary_sfpu_bare.hpp"
#include "ops/mm_bare.hpp"
#include "netlist_utils.hpp"
#include "ops/tm_bare.hpp"

#include <boost/graph/graphviz.hpp>
#include <ostream>

using namespace netlist_utils;

//////////////////////
// tt_digraph
//////////////////////
tt_digraph::tt_digraph(tt_graph_info graph_info) { my_graph_info = graph_info; }

void tt_digraph::add_all_nodes(std::map<string, tt_queue_wrap>& queues, tt_graph_info& graph_info) {
    // add all op nodes
    for (auto& op_info_it : graph_info.op_map) {
        insert_op_node(&op_info_it.second);  // q pointer null
    }
    log_debug(tt::LogNetlist, "All op nodes created for graph: {}", graph_info.name);
    // add all queue nodes
    for (auto& q_wrap_it : queues) {
        insert_queue_node(&q_wrap_it.second);  // q pointer null
    }
    log_debug(tt::LogNetlist, "All queue nodes created for graph: {}", graph_info.name);
}

void tt_digraph::connect_nodes() {
    string output_name;

    using node_type_t = decltype(*(std::begin(boost::make_iterator_range(boost::vertices(graph)))));
    std::unordered_map<std::string, node_type_t> node_name_to_vertex_map;
    std::unordered_map<node_type_t, std::vector<std::string>> node_name_to_inputs_map;
    for (auto vd : boost::make_iterator_range(boost::vertices(graph))) {
        node_name_to_vertex_map.insert({graph[vd].name, vd});
    }
    for (auto vd : boost::make_iterator_range(boost::vertices(graph))) {
        if (graph[vd].op_not_queue) {
            node_name_to_inputs_map.insert({vd, graph[vd].my_op_info_ptr->input_names});
        } else {
            node_name_to_inputs_map.insert({vd, {graph[vd].my_queue_wrap_ptr->my_queue_info.input}});
        }
    }


    // std::cout << "TEST RUN" << std::endl;
    // go through all nodes
    {
    auto vertices_range = boost::make_iterator_range(boost::vertices(graph));
    for (auto vd : vertices_range) {
        edge_t e;

        int input_index = 0;
        for (std::string const& input_name : node_name_to_inputs_map.at(vd)) {
            if (node_name_to_vertex_map.find(input_name) == node_name_to_vertex_map.end()) {
                // Presumed host
                continue;
            }
            auto vvd = node_name_to_vertex_map.at(input_name);

            if (graph[vd].op_not_queue ||
                (!graph[vd].op_not_queue && graph[vd].my_queue_wrap_ptr->my_queue_info.type == IO_TYPE::Queue)) {
                e = this->add_edge(vvd, vd);
                graph[e].input_index = input_index;
                // std::cout << "Connecting " << graph[vvd].name << " to " << graph[vd].name << " at index " << input_index << std::endl;
                if (!graph[vd].op_not_queue) {
                    // std::cout << "op_name_to_output_queue_map[" << input_name << "] = " << (void*)graph[vd].my_queue_wrap_ptr << "(" << graph[vd].name << ")" << std::endl;
                    op_name_to_output_queue_map.insert({input_name, *graph[vd].my_queue_wrap_ptr});
                }
            } else {
                if (graph[vd].my_queue_wrap_ptr->my_queue_info.type != IO_TYPE::Queue) {
                    // std::cout << "Adding new vertex " << std::endl;
                    vertex_t vid = this->add_vertex();
                    graph[vid].my_queue_wrap_ptr = graph[vd].my_queue_wrap_ptr;
                    graph[vid].name = graph[vd].my_queue_wrap_ptr->my_queue_info.name + "_from_" + graph[vvd].name;
                    graph[vid].op_not_queue = false;
                    e = this->add_edge(vvd, vid);
                    graph[e].input_index = 0;
                    // std::cout << "Connecting " << graph[vvd].name << " to " << graph[vd].my_queue_wrap_ptr->my_queue_info.name << "_from_" << graph[vvd].name << " at index " << 0 << std::endl;
                    // std::cout << "\tvid.my_queue_ptr=" << graph[vd].my_queue_wrap_ptr << std::endl;
                    // std::cout << "op_name_to_output_queue_map[" << input_name << "] = " << (void*)graph[vd].my_queue_wrap_ptr << "(" << graph[vd].name << ")" << std::endl;
                    op_name_to_output_queue_map.insert({input_name, *graph[vd].my_queue_wrap_ptr});
                }
            }
            input_index++;
        }
    }
    }
    // std::cout << "-----------------------------------" << std::endl;

    // go through all nodes
    // auto vertices_range = boost::make_iterator_range(boost::vertices(graph));
    // for (auto vd : vertices_range) {
    //     edge_t e;
    //     if (graph[vd].op_not_queue) {
    //         output_name = graph[vd].my_op_info_ptr->name;
    //     } else {
    //         output_name = graph[vd].my_queue_wrap_ptr->my_queue_info.name;
    //     }
    //     // op - go through ops and queues and connect to inputs
    //     for (auto vvd : boost::make_iterator_range(boost::vertices(graph))) {
    //         if (vd != vvd)  // dont connect to self
    //         {
    //             if (graph[vvd].op_not_queue) {
    //                 // iterate through inputs and connect the right one
    //                 int input_index = 0;
    //                 for (string input_name : graph[vvd].my_op_info_ptr->input_names) {
    //                     if (output_name == input_name) {
    //                         e = this->add_edge(vd, vvd);
    //                         std::cout << "Connecting " << graph[vd].name << " to " << graph[vvd].name << " at index " << input_index << std::endl;
    //                         graph[e].input_index = input_index;
    //                         if (!graph[vd].op_not_queue) {
    //                             string op_name = graph[vvd].my_op_info_ptr->name;
    //                         }
    //                     }
    //                     input_index++;
    //                 }
    //             } else {
    //                 // input to a queue
    //                 // just connect if names match
    //                 if (output_name == graph[vvd].my_queue_wrap_ptr->my_queue_info.input) {
    //                     if (graph[vvd].my_queue_wrap_ptr->my_queue_info.type == IO_TYPE::Queue) {
    //                         e = this->add_edge(vd, vvd);
    //                         graph[e].input_index = 0;
    //                         std::cout << "Connecting " << graph[vd].name << " to " << graph[vvd].name << " at index " << 0 << std::endl;
    //                     } else {
    //                         vertex_t vid = this->add_vertex();
    //                         std::cout << "Adding new vertex " << std::endl;
    //                         graph[vid].my_queue_wrap_ptr = graph[vvd].my_queue_wrap_ptr;
    //                         graph[vid].name =
    //                             graph[vvd].my_queue_wrap_ptr->my_queue_info.name + "_from_" + graph[vd].name;
    //                         graph[vid].op_not_queue = false;
    //                         e = this->add_edge(vd, vid);
    //                         std::cout << "Connecting " << graph[vd].name << " to " << graph[vid].name << " at index " << 0 << std::endl;
    //                         std::cout << "\tvid.my_queue_ptr=" << graph[vvd].my_queue_wrap_ptr << std::endl;
    //                         graph[e].input_index = 0;
    //                     }
    //                     op_name_to_output_queue_map.insert({output_name, *graph[vvd].my_queue_wrap_ptr});
    //                     std::cout << "op_name_to_output_queue_map[" << output_name << "] = " << (void*)graph[vvd].my_queue_wrap_ptr << "(" << graph[vvd].name << ")" << std::endl;
    //                 }
    //             }
    //         }
    //     }
    // }
}

void tt_digraph::remove_unconnected_nodes() {
    for (auto vd : boost::make_iterator_range(vertices(graph))) {
        if (boost::degree(vd, graph) == 0)
            boost::remove_vertex(vd, graph);
    }
}

void tt_digraph::dump_graphviz(string directory, string name) {
    ofstream outfile;
    if (!std::experimental::filesystem::exists(directory)) {
        std::experimental::filesystem::create_directories(directory);
    }
    string output_file = directory + "graph_" + name + ".dump";
    log_info (tt::LogNetlist, "Dumping graphviz: {}", output_file);
    outfile.open(output_file);

    boost::write_graphviz(
        outfile,
        graph,
        [&](auto& out, auto v) { out << "[label=\"" << graph[v].name << "\"]"; },
        [&](auto& out, auto e) { out << "[label=\"" << graph[e].input_index << "\"]"; });
    outfile.close();
}

void tt_digraph::insert_op_node(tt_op_info* op_info_ptr) {
    vertex_t vid = this->add_vertex();
    graph[vid].my_op_info_ptr = op_info_ptr;
    graph[vid].name = op_info_ptr->name;
    graph[vid].op_not_queue = true;
}
void tt_digraph::insert_queue_node(tt_queue_wrap* q_wrap_ptr) {
    vertex_t vid = this->add_vertex();
    graph[vid].my_queue_wrap_ptr = q_wrap_ptr;
    graph[vid].name = q_wrap_ptr->my_queue_info.name;
    graph[vid].op_not_queue = false;
}

std::shared_ptr<tt_op> tt_digraph::create_op(tt_op_info* op_info_ptr, const unordered_map<string, tt_fused_op_info>& fused_ops_map) { return netlist_utils::create_op(op_info_ptr, fused_ops_map); }

void tt_digraph::create_ops(const unordered_map<string, tt_fused_op_info>& fused_ops_map) {
    std::deque<vertex_t> topo_order;
    boost::topological_sort(graph, std::front_inserter(topo_order));

    for (int i = 0; i < topo_order.size(); ++i) {
        if (graph[topo_order[i]].op_not_queue) {
            graph[topo_order[i]].my_op_info_ptr->my_op = create_op(graph[topo_order[i]].my_op_info_ptr, fused_ops_map);
            op_list.push_back(graph[topo_order[i]].my_op_info_ptr->my_op);
        }
    }
}

vector<int> tt_digraph::get_input_nodes_ordered_by_input_index(int current_node) {
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
