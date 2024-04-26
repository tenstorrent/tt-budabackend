// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

// An interface which provides an interface for handling (modifying) a given PipeGraph object.
// Used in pipe graph creation to produce a valid pipe graph after parsing it from a pipegen.yaml.
class IPipeGraphHandler
{
public:
    // Virtual destructor, necessary for base class.
    virtual ~IPipeGraphHandler() = default;

    // Handle (modify) the given pipe graph in a specific way.
    virtual void handle(PipeGraph& pipe_graph) = 0;
};

} // namespace pipegen2