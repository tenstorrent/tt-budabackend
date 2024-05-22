// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "graph_creator/pipe_graph/pipe_graph_handler.h"
#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

class E2EQueueHandler : public IPipeGraphHandler
{
public:
    // Handle the embbeding indexes in a given pipe graph.
    void handle(PipeGraph& pipe_graph) override;
};

}  // namespace pipegen2