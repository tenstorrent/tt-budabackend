// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

namespace pipegen2
{
    class StreamGraph;
}

namespace blobgen2
{

    class BlobGraph;

    class BlobGraphCreator
    {
    public:
        // Constructor.
        BlobGraphCreator();

        // Virtual destructor.
        virtual ~BlobGraphCreator();

        // Creates a BlobGraph out of a vector of StreamGraphs, by calling the create_impl method.
        std::unique_ptr<BlobGraph> create(
            const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);

    private:
        // Implements creating the BlobGraph out of a vector of StreamGraphs.
        virtual std::unique_ptr<BlobGraph> create_impl(
            const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);
    };

}