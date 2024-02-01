// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <string>

namespace blobgen2
{

    class Blobgen2;

    enum Blobgen2OutputType : unsigned char
    {
        Hex,
        Yaml
    };

    class Blobgen2Factory
    {
    public:
        // Constructor.
        Blobgen2Factory();

        // Destructor.
        ~Blobgen2Factory();

        // Returns an instance of the Blobgen2Factory.
        static std::unique_ptr<Blobgen2Factory> get_instance();

        // Creates a Blobgen2 object and transfers the ownership of it to the caller.
        std::unique_ptr<Blobgen2> create(
            const std::string& arch,
            const std::vector<Blobgen2OutputType>& out_types,
            const std::string& blob_out_dir);

    private:

    };

}