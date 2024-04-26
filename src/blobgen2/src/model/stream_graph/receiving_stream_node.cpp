// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/receiving_stream_node.h"

namespace pipegen2
{
    ReceivingStreamNode::ReceivingStreamNode(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter,
        unsigned int reg_update_vc, std::unique_ptr<BaseStreamSource> source, std::unique_ptr<BaseStreamDestination> destination) :
        BaseStreamNode(msg_size, num_iters_in_epoch, num_unroll_iter, reg_update_vc, std::move(source), std::move(destination))
    {
    }

    BaseStreamNode* ReceivingStreamNode::clone_internal() const
    {
        ReceivingStreamNode* stream = new ReceivingStreamNode();
        stream->m_receiving_stream_config = m_receiving_stream_config;

        return stream;
    }
}