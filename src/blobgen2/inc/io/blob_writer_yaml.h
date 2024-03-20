// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io/blob_writer.h"

#include <string>

namespace blobgen2
{

    class BlobWriterYaml : public BlobWriter
    {
    public:
        // Constructor.
        BlobWriterYaml(const std::string& output_directory_path);

        // Destructor.
        ~BlobWriterYaml();

    private:
        // Writes the BlobGraph into human readable .yaml files,
        // stored under the m_output_directory_path directory.
        // A .yaml file per Tensix core is written.
        // Yaml files are named as:
        //   <blob_graph_name>_epoch<epoch_id>_<chip_id>_<core_y>_<core_x>.yaml
        // For example:
        //   my_graph_epoch0_0_0_0.yaml
        //   my_graph_epoch0_0_1_0.yaml
        //   my_graph_epoch0_0_1_1.yaml
        //   ...
        void write_impl(const BlobGraph *const blob_graph) const;

        // Path to the directory where the generated .yaml files are stored.
        std::string m_output_directory_path;
    };

}