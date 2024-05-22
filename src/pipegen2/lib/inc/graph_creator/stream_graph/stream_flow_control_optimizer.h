// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "model/stream_graph/stream_graph.h"

namespace pipegen2
{
class StreamFlowControlOptimizer
{
public:
    // Flow control optimization.
    //
    // Walks through all streams in stream graph and optimizes flow control setting on source->dest pairs where it's
    // possible.
    void optimize_stream_graph_flow_control(StreamGraph* stream_graph);

private:
    // Sets `data_buf_no_flow_ctrl` and `dest_data_buf_no_flow_ctrl` flags to true when possible. This optimization
    // is possible when there is large enough buffer on the destination side to receive all the data in a phase
    // without wrapping.
    void optimize_stream_flow_control(StreamNode* stream_node);

    // Helper function used by optimize_stream_flow_control. Sets `data_buf_no_flow_ctrl` flag to true or false
    // depending on whether `dest_buffer_size_bytes` is large enough to receive all the data.
    // Also sets the `dest_data_buf_no_flow_ctrl` flag on the destination stream node.
    void set_no_flow_control(StreamNode* source_stream, PhaseId start_phase_id, bool no_flow_control);
};
}  // namespace pipegen2