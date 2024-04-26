// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/base_stream_node.h"

namespace pipegen2
{
    BaseStreamNode::BaseStreamNode(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter, unsigned int reg_update_vc,
        std::unique_ptr<BaseStreamSource> source, std::unique_ptr<BaseStreamDestination> destination) :
        m_stream_config(msg_size, num_iters_in_epoch, num_unroll_iter, reg_update_vc),
        m_source(std::move(source)),
        m_destination(std::move(destination))
    {
    }

    BaseStreamNode* BaseStreamNode::clone() const
    {
        BaseStreamNode* stream = clone_internal();
        stream->m_stream_config = m_stream_config;
        stream->m_phases.insert(stream->m_phases.end(), m_phases.begin(), m_phases.end());
        stream->m_source = std::unique_ptr<BaseStreamSource>(m_source->clone());
        stream->m_destination = std::unique_ptr<BaseStreamDestination>(m_destination->clone());

        return stream;
    }
}