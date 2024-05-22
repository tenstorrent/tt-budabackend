//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{

class ForkJoinGraphStreamInfo
{
public:
    // Makes the mappings outlined below with the information from a stream graph collection.
    void populate_with_stream_info(const StreamGraphCollection* stream_graph_collection);
};

}  // namespace pipegen2