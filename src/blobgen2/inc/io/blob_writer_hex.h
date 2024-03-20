// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io/blob_writer.h"

#include <string>

namespace blobgen2
{

    class BlobWriterHex : public BlobWriter
    {
    public:
        // Constructor.
        BlobWriterHex(const std::string& output_directory_path);

        // Destructor.
        ~BlobWriterHex();

    private:
        // Writes the BlobGraph into binary .hex files,
        // stored under the m_output_directory_path directory.
        // A .hex file per Tensix core is written.
        // Hex files are named as:
        //   <blob_graph_name>_epoch<epoch_id>_<chip_id>_<core_y>_<core_x>.hex
        // For example:
        //   my_graph_epoch0_0_0_0.hex
        //   my_graph_epoch0_0_1_0.hex
        //   my_graph_epoch0_0_1_1.hex
        //   ...
        void write_impl(const BlobGraph *const blob_graph) const;

        // Path to the directory where the generated .hex files are stored.
        std::string m_output_directory_path;
    };

}