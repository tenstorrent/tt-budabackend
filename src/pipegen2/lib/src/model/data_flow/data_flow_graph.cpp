// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/data_flow_graph.h"

namespace pipegen2
{
    
#ifdef TT_DEBUG
std::string pipegen2::DataFlowGraph::to_json() const
{
    std::stringstream json_builder;

    json_builder << "{ \"kind\": { \"graph\": true }, ";

    json_builder << "\"nodes\": [";
    for (const std::unique_ptr<DataFlowNode>& df_node : m_nodes)
    {
        const NodeId df_node_id = df_node->get_id();
        std::string df_node_type = data_flow_type_to_string(df_node->get_dataflow_type());
        std::string df_node_visualize_label = df_node->get_visualize_label();
        json_builder << " { \"id\": \"" << df_node->get_id()
                        << "\", \"label\": \"" << df_node_visualize_label
                        << "\", \"color\": \"" << df_node->get_node_color()
                        << "\", \"shape\": \"ellipse"
                        << "\"},";
    }
    // Erasing last ',' because that makes improper json.
    json_builder.seekp(-1, json_builder.cur);
    json_builder << " ], ";
    
    json_builder << "\"edges\": [";
    for (const std::unique_ptr<DataFlowNode>& df_node : m_nodes)
    {   
        for (const DataFlowNode* dest : df_node->get_destinations())
        {
            json_builder << " { \"from\": \"" << df_node->get_id()
                            << "\", \"to\": \"" << dest->get_id()
                            << "\" },";
        }
    }
    // Erasing last ',' because that makes improper json.
    json_builder.seekp(-1, json_builder.cur);
    json_builder << " ] }";
    return json_builder.str();
}
#endif

}