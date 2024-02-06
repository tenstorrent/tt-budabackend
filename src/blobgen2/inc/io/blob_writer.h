// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace blobgen2
{

    class BlobGraph;

    class BlobWriter
    {
    public:
        // Virtual destructor.
        virtual ~BlobWriter();

        // Writes BlobGraph data structure to the output
        // by calling the write_imp method implemented in child classes.
        void write(const BlobGraph *const blob_graph);

    protected:
        // Child classes to implement writing the BlobGraph
        // onto a desired output and in a desired output format.
        virtual void write_impl(const BlobGraph *const blob_graph) const = 0;
    };

}