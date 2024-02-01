// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "base_stream_node.h"

namespace pipegen2
{
    class StreamGraph
    {
    public:
        using UPtr = std::unique_ptr<StreamGraph>;

        void add_stream(std::unique_ptr<BaseStreamNode> stream) { m_streams.push_back(std::move(stream)); }

        const std::vector<std::unique_ptr<BaseStreamNode>>& get_streams() const { return m_streams; }

        void set_overlay_blob_extra_size(const unsigned int size) { m_overlay_blob_extra_size = size; }

        const unsigned int get_overlay_blob_extra_size() const { return m_overlay_blob_extra_size; }

        void set_graph_name(const std::string& name) { m_graph_name = name; }

        const std::string& get_graph_name() const { return m_graph_name; }

    private:
        std::string m_graph_name;

        std::vector<std::unique_ptr<BaseStreamNode>> m_streams;

        unsigned int m_overlay_blob_extra_size;
    };
}