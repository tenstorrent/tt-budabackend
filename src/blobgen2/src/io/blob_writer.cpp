// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_writer.h"
namespace blobgen2
{

    BlobWriter::~BlobWriter() = default;

    void BlobWriter::write(const BlobGraph *const blob_graph)
    {
        write_impl(blob_graph);
    }

}