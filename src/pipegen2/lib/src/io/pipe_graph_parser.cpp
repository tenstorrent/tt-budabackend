// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/pipe_graph_parser.h"

#include "io/pipe_graph_parser_internal.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

void PipeGraphParser::parse_graph(PipeGraph& pipe_graph, const std::string& pipegen_yaml_path)
{
    try
    {
        std::ifstream pipegen_yaml_stream(pipegen_yaml_path);
        pipe_graph_parser_internal::parse_graph(pipe_graph, pipegen_yaml_stream);
    }
    catch(const std::exception& e)
    {
        throw InvalidPipegenYamlFormatException("Can't open pipegen yaml file: " + std::string(e.what()));
    }
}

} // namespace pipegen2
