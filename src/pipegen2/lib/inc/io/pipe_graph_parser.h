// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

class PipeGraphParser
{
public:
    // Parses buffers and pipes from pipegen graph yaml into the pipe graph.
    static void parse_graph(PipeGraph& pipe_graph, const std::string& pipegen_yaml_path);
};

}  // namespace pipegen2