// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <string>

#include "model/stream_graph/stream_graph.h"

namespace blobgen2
{

    class BlobGraphCreator;
    class BlobWriter;
    class BlobGraph;

    class Blobgen2
    {
    friend class Blobgen2Factory;

    public:
        // Constructor.
        Blobgen2(
            std::unique_ptr<BlobGraphCreator> blob_graph_creator,
            std::vector<std::unique_ptr<BlobWriter>> blob_writers);

        // Destructor.
        ~Blobgen2();

        // Serializes StreamGraph graphs into blobgen blob outputs by the provided vector of BlobWriter writers.
        void generate_blob(
            //TODO: Make it a const ref. once stream graph is not updated by blobgen2.
            std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);

    private:
        // Implements all blobgen1 optimizations that will in long-term be implemented by pipegen2 instead blobgen2.
        // When all the optimizations are moved to pipegen2 the method will be removed.
        //TODO: Move these optimizations to pipegen2.
        void update_stream_graph(
            std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);

        // Creates a BlobGraph out of a vector of StreamGraphs using the provided BlobGraphCreator object.
        // A BlobGraph node contains all info to configure a Tensix core,
        // whereas StreamGraph nodes contain info of all entities involved in a data transfer,
        // i.e. multiple Tensix cores, multiple streams, etc.
        // This method collects per Tensix core info from the StreamGraph nodes.
//      void create_blob_graph(
//          const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);

        // Outputs the created BlobGraph using all the provided BlobWriters.
//      void output_blob_graph();

        // Outputs the updated stream graph using all the provided blob writers.
        void output_stream_graph(
            const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs);

        // BlobGraph creator.
        std::unique_ptr<BlobGraphCreator> m_blob_graph_creator;

        // BlobGraph writers.
        std::vector<std::unique_ptr<BlobWriter>> m_blob_writers;

        // Blob graph.
        std::unique_ptr<BlobGraph> m_blob_graph;

    };

}