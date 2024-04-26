// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>

#include "model/stream_graph/base_stream_node.h"

namespace pipegen2
{
    // Stream that sends data over NOC.
    class SendingStreamNode : public BaseStreamNode
    {
    public:
        // Stream configuration parameters when sending raw (tilized) data.
        struct TilizedDataConfig
        {
            // TODO: fill in
            unsigned int batch_dim_size;

            // TODO: fill in
            unsigned int r_dim_size;

            // TODO: fill in
            unsigned int c_dim_size;

            // TODO: fill in
            unsigned int zr_dim_size;

            // TODO: fill in
            unsigned int zc_dim_size;

            // TODO: fill in
            unsigned int skip_col_bytes;

            // TODO: fill in
            unsigned int skip_col_tile_row_bytes;

            // TODO: fill in
            unsigned int skip_col_row_bytes;

            // TODO: fill in
            unsigned int skip_zcol_bytes;

            // TODO: fill in
            unsigned int skip_col_zrow_bytes;

            // TODO: fill in
            unsigned int stride;

            // TODO: fill in
            unsigned int total_strides;

            // TODO: fill in
            unsigned int stride_offset_size_bytes;
        };

        // Buffer dimension parameters.
        struct BufferDimensions
        {
            // TODO: fill in
            unsigned int mblock_k;

            // TODO: fill in
            unsigned int mblock_m;

            // TODO: fill in
            unsigned int mblock_n;

            // Ublock number of columns.
            unsigned int ublock_ct;

            // Ublock number of rows.
            unsigned int ublock_rt;
        };

    private:
        // Contains parameters for sending stream configuration.
        // Parameters are extracted into structure to make their copying easier and less error prone.
        struct SendingStreamConfig
        {
            // TODO: fill in
            std::optional<bool> legacy_pack;

            // Vector of destination stream ids.
            std::vector<std::string> dest;

            // TODO: fill in
            std::optional<unsigned int> outgoing_data_noc;

            // TODO: fill in
            std::optional<unsigned int> vc;

            // Packer source output operand index.
            std::optional<unsigned int> output_index;

            // Is stream sending raw (tilized) data.
            std::optional<bool> moves_raw_data;

            // Configuration parameters when sending raw (tilized) data.
            std::optional<TilizedDataConfig> tilized_data_config;

            // Buffer dimension parameters.
            std::optional<BufferDimensions> buffer_dimensions;
        };

    public:
        SendingStreamNode(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter, unsigned int reg_update_vc,
            std::unique_ptr<BaseStreamSource> source, std::unique_ptr<BaseStreamDestination> destination);

        const std::optional<bool>& is_legacy_pack() const { return m_sending_stream_config.legacy_pack; }

        void set_legacy_pack(std::optional<bool> value) { m_sending_stream_config.legacy_pack = value; }
        void set_legacy_pack(bool value) { m_sending_stream_config.legacy_pack = value; }

        const std::vector<std::string>& get_destination_stream_ids() const { return m_sending_stream_config.dest; }

        void set_destination_stream_ids(const std::vector<std::string>& dests)
        {
            m_sending_stream_config.dest = dests;
        }

        void add_destination_stream_id(const std::string& stream_id) { m_sending_stream_config.dest.push_back(stream_id); }

        const std::optional<unsigned int>& get_outgoing_data_noc() const { return m_sending_stream_config.outgoing_data_noc; }

        void set_outgoing_data_noc(std::optional<unsigned int> value) { m_sending_stream_config.outgoing_data_noc = value; }
        void set_outgoing_data_noc(unsigned int value) { m_sending_stream_config.outgoing_data_noc = value; }

        // TODO: Rename once we find out what "vc" stands for.
        const std::optional<unsigned int>& get_vc() const { return m_sending_stream_config.vc; }

        // TODO: Rename once we find out what "vc" stands for.
        void set_vc(std::optional<unsigned int> value) { m_sending_stream_config.vc = value; }
        void set_vc(unsigned int value) { m_sending_stream_config.vc = value; }

        const std::optional<unsigned int>& get_operand_output_index() const { return m_sending_stream_config.output_index; }

        void set_operand_output_index(std::optional<unsigned int> value) { m_sending_stream_config.output_index = value; }
        void set_operand_output_index(unsigned int value) { m_sending_stream_config.output_index = value; }

        const std::optional<bool>& is_sending_raw_data() const { return m_sending_stream_config.moves_raw_data; }

        void set_sending_raw_data(std::optional<bool> value) { m_sending_stream_config.moves_raw_data = value; }
        void set_sending_raw_data(bool value) { m_sending_stream_config.moves_raw_data = value; }

        const std::optional<TilizedDataConfig>& get_tilized_data_config() const { return m_sending_stream_config.tilized_data_config; }

        void set_tilized_data_config(const std::optional<TilizedDataConfig>& config) { m_sending_stream_config.tilized_data_config = config; }
        void set_tilized_data_config(const TilizedDataConfig& config) { m_sending_stream_config.tilized_data_config = config; }

        const std::optional<BufferDimensions>& get_buffer_dimensions() const { return m_sending_stream_config.buffer_dimensions; }

        void set_buffer_dimensions(const std::optional<BufferDimensions>& dimensions) { m_sending_stream_config.buffer_dimensions = dimensions; }
        void set_buffer_dimensions(const BufferDimensions& dimensions) { m_sending_stream_config.buffer_dimensions = dimensions; }

    protected:
        SendingStreamNode() = default;

        virtual BaseStreamNode* clone_internal() const;

        // Sending stream config.
        SendingStreamConfig m_sending_stream_config;
    };
}