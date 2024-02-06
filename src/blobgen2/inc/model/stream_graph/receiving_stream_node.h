// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>

#include "model/stream_graph/base_stream_node.h"

namespace pipegen2
{
    // Stream that receives data over NOC.
    class ReceivingStreamNode : public BaseStreamNode
    {
    private:
        // Contains parameters for receiving stream configuration.
        // Parameters are extracted into structure to make their copying easier and less error prone.
        struct ReceivingStreamConfig
        {
            // TODO: fill in
            std::optional<unsigned int> incoming_data_noc;

            // Input operand index to the unpacker destination.
            std::optional<unsigned int> input_index;

            // TODO: fill in
            std::optional<unsigned int> buf_space_available_ack_thr;
        };

    public:
        using Ptr = std::shared_ptr<ReceivingStreamNode>;
        using UPtr = std::unique_ptr<ReceivingStreamNode>;

        ReceivingStreamNode(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter,
                            unsigned int reg_update_vc, std::unique_ptr<BaseStreamSource> source,
                            std::unique_ptr<BaseStreamDestination> destination);

        const std::optional<unsigned int>& get_incoming_data_noc() const
        {
            return m_receiving_stream_config.incoming_data_noc;
        }

        void set_incoming_data_noc(std::optional<unsigned int> value) { m_receiving_stream_config.incoming_data_noc = value; }
        void set_incoming_data_noc(unsigned int value) { m_receiving_stream_config.incoming_data_noc = value; }

        const std::optional<unsigned int>& get_operand_input_index() const
        {
            return m_receiving_stream_config.input_index;
        }

        void set_operand_input_index(std::optional<unsigned int> value) { m_receiving_stream_config.input_index = value; }
        void set_operand_input_index(unsigned int value) { m_receiving_stream_config.input_index = value; }

        const std::optional<unsigned int>& get_buf_space_available_ack_thr() const
        {
            return m_receiving_stream_config.buf_space_available_ack_thr;
        }

        void set_buf_space_available_ack_thr(std::optional<unsigned int> value)
        {
            m_receiving_stream_config.buf_space_available_ack_thr = value;
        }
        void set_buf_space_available_ack_thr(unsigned int value)
        {
            m_receiving_stream_config.buf_space_available_ack_thr = value;
        }

    protected:
        ReceivingStreamNode() = default;

        virtual BaseStreamNode* clone_internal() const;

        // Receiving stream config.
        ReceivingStreamConfig m_receiving_stream_config;
    };
}