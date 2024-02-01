// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "blobgen2.h"

//TODO: Remove the iostream include when print debug info is removed.
#include <iostream>

#include "blob_graph_creator/blob_graph_creator.h"
#include "io/blob_writer.h"
#include "model/blob_graph.h"

namespace blobgen2
{

    Blobgen2::Blobgen2(
        std::unique_ptr<BlobGraphCreator> blob_graph_creator,
        std::vector<std::unique_ptr<BlobWriter>> blob_writers)
            : m_blob_graph_creator(std::move(blob_graph_creator)),
              m_blob_writers(std::move(blob_writers))
    {
    }

    Blobgen2::~Blobgen2() = default;

    void Blobgen2::generate_blob(
        std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
    {
        update_stream_graph(stream_graphs);
//      create_blob_graph(stream_graphs);
//      output_blob_graph();
        output_stream_graph(stream_graphs);
    }

    void Blobgen2::update_stream_graph(
        std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
    {
        //TODO: Implement StreamGraph optimizations.
        for (const auto& stream_graph : stream_graphs)
        {
            std::cout << "Blobgen2: Updating: '" << stream_graph->get_graph_name() << "' stream graph." << std::endl;
        }
    }

//  void Blobgen2::create_blob_graph(
//      const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
//  {
//      std::cout << "Blobgen2: Creating blob graph." << std::endl;
//      m_blob_graph = m_blob_graph_creator->create(stream_graphs);
//  }

//  void Blobgen2::output_blob_graph()
//  {
//      std::cout << "Blobgen2: Outputting blob graph." << std::endl;
//      for (auto&& blob_writer: m_blob_writers)
//      {
//          blob_writer->write(m_blob_graph.get());
//      }
//  }

    void Blobgen2::output_stream_graph(
        const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
    {
        for (const auto& stream_graph : stream_graphs)
        {
            for (auto&& blob_writer: m_blob_writers)
            {
                std::cout << "Blobgen2: Outputting: '" << stream_graph->get_graph_name() << "' stream graph." << std::endl;
//              blob_writer->write(stream_graph.get());
            }
        }
    }

}