// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "graph_creator/pipe_graph/pipe_graph_handler.h"
#include "graph_creator/pipe_graph/pipe_graph_info.h"
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/pipe_graph/pipe_graph.h"
#include "model/typedefs.h"

namespace pipegen2
{

class PipeGraphCreator
{
public:
    // Creates pipe graph from the pipegen yaml.
    std::unique_ptr<PipeGraph> create_pipe_graph(const std::string& pipegen_yaml_path);

private:
    // Creates pipe inputs for scatter buffers and adds them to map per buffer ID.
    void create_pipe_inputs_for_scatter_buffers(const PipeGraph& pipe_graph);

    // Populates map of buffers per ID.
    void populate_buffers_map(PipeGraph& pipe_graph);

    // Finds and sets input and output nodes for each pipe graph node.
    void find_pipe_graph_nodes_connections(PipeGraph& pipe_graph);

    // Function which uses handlers circumvent shortcomings of pipegen.yaml on the given PipeGraph.
    // TODO: Once net2pipe is fixed, we will have all info needed to properly create a read-only pipe graph,
    // with no need to change it.
    void patch_deficiencies(PipeGraph& pipe_graph);

    // A utility object which keeps track of all the helper maps necessary for PipeGraph creation.
    PipeGraphInfo m_pipe_graph_info;
};

}  // namespace pipegen2