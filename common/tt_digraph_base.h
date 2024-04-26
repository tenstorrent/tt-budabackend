// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/graph/graphviz.hpp>

#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/topological_sort.hpp"

template <class GRAPH_NODE_T, class GRAPH_EDGE_T> 
class tt_digraph_base {
  public:
    using digraph_t = 
        typename boost::adjacency_list<boost::listS, boost::vecS, boost::bidirectionalS, GRAPH_NODE_T, GRAPH_EDGE_T>;
    using vertex_t = typename boost::graph_traits<digraph_t>::vertex_descriptor;
    using edge_t = typename boost::graph_traits<digraph_t>::edge_descriptor;
    
    digraph_t graph;

    // boost::iterator_range<digraph_t::iterator> vertices_iterator_range() {
    //     return boost::make_iterator_range(boost::vertices(this->graph));
    // }

    vertex_t add_vertex() {
        vertex_t vid = boost::add_vertex(this->graph);
        return vid;
    }

    edge_t add_edge(vertex_t u, vertex_t v) {
        edge_t eid = boost::add_edge(u, v, this->graph).first;
        return eid;
    }

    GRAPH_NODE_T &get_vertex(size_t id) {
        return this->graph[id];
    }
    const GRAPH_NODE_T &get_vertex(size_t id) const {
        return this->graph[id];
    }

    int num_vertices() const { return boost::num_vertices(this->graph); }

    vertex_t edge_source_node_id(edge_t eid) const { return boost::source(eid, this->graph); }
    GRAPH_NODE_T edge_source_node(edge_t eid) const { return this->get_vertex(this->edge_source_node_id(eid)); }
    vertex_t edge_target_node_id(edge_t eid) const { return boost::target(eid, this->graph); }
    GRAPH_NODE_T edge_target_node(edge_t eid) const { return this->get_vertex(this->edge_target_node_id(eid)); }

};

