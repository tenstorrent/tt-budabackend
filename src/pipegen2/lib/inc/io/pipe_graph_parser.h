// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{
    class PipeGraphParser
    {
    public:
        // Constructs graph parser to parse given pipegen graph yaml.
        PipeGraphParser(const std::string& pipegen_yaml_path);

        // Parses buffers and pipes from pipegen graph yaml into the pipe graph.
        void parse_graph(PipeGraph& pipe_graph);

    private:
        // Buffer starting line prefix.
        static const std::string s_buffer_prefix;

        // Pipe starting line prefix.
        static const std::string s_pipe_prefix;

        // Delimiter line prefix.
        static const std::string s_delimiter_prefix;

        // Parses graph node from pipegen graph yaml lines.
        void parse_node(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph);

        // Parses buffer from pipegen graph yaml lines.
        void parse_buffer(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph);

        // Parses pipe from pipegen graph yaml lines.
        void parse_pipe(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph);

        // Pipegen graph yaml file input stream.
        std::ifstream m_pipegen_yaml_stream;
    };
}