// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_yaml_writer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "model/stream_graph/dram_config.h"
#include "model/stream_graph/base_stream_node.h"
#include "model/stream_graph/base_stream_destination.h"
#include "model/stream_graph/base_stream_source.h"
#include "model/stream_graph/endpoint_stream_destination.h"
#include "model/stream_graph/endpoint_stream_source.h"
#include "model/stream_graph/local_stream_destination.h"
#include "model/stream_graph/local_stream_source.h"
#include "model/stream_graph/receiving_stream_node.h"
#include "model/stream_graph/remote_stream_destination.h"
#include "model/stream_graph/remote_stream_source.h"
#include "model/stream_graph/sending_stream_node.h"

namespace pipegen2
{
    std::string BlobYamlWriter::s_dram_blob_yaml_line = "dram_blob:";
    std::string BlobYamlWriter::s_phase_blob_yaml_prefix = "phase_";
    std::string BlobYamlWriter::s_overlay_blob_extra_size_blob_yaml_key = "overlay_blob_extra_size";

    BlobYamlWriter::BlobYamlWriter(const std::string& blob_yaml_path)
        : m_blob_yaml_path(blob_yaml_path)
    {
    }

    void BlobYamlWriter::write_blob_yaml(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        std::ofstream yaml_writer;
        yaml_writer.open(m_blob_yaml_path);

        // Write dram_blob.
        yaml_writer << s_dram_blob_yaml_line << std::endl;
        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graphs)
        {
            for (const std::unique_ptr<BaseStreamNode>& stream : stream_graph->get_streams())
            {
                const std::vector<DramConfig> dram_configs = stream->get_dram_configs();
                if (dram_configs.size() > 0)
                {
                    yaml_writer << "  " << BlobYamlWriter::get_system_wide_stream_id(stream.get()) << ":" << std::endl;

                    for (const DramConfig& dram_config : dram_configs)
                    {
                        yaml_writer << get_string(dram_config);
                    }
                }
            }
        }

        // Write phases.
        std::map<PhaseId, std::vector<std::pair<BaseStreamNode*, int>>> phase_map = collect_stream_graphs_phases(stream_graphs);
        for (const auto& phaseIt : phase_map)
        {
            yaml_writer << s_phase_blob_yaml_prefix << phaseIt.first << ":" << std::endl;

            for (const auto& stream_and_phase_index : phaseIt.second)
            {
                yaml_writer << get_string(stream_and_phase_index.first, stream_and_phase_index.second);
            }
        }

        // Write overlay_blob_extra_size.
        for (const auto& stream_graph : stream_graphs)
        {
            if (&stream_graph == &stream_graphs[0])
            {
                unsigned int extra_size = (*stream_graph).get_overlay_blob_extra_size();
                yaml_writer << s_overlay_blob_extra_size_blob_yaml_key << ": " << extra_size << std::endl;
            }
        }

        yaml_writer.close();
    }

    std::map<PhaseId, std::vector<std::pair<BaseStreamNode*, int>>> BlobYamlWriter::collect_stream_graphs_phases(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        std::map<PhaseId, std::vector<std::pair<BaseStreamNode*, int>>> phase_map;

        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graphs)
        {
            for (const std::unique_ptr<BaseStreamNode>& stream_node : stream_graph->get_streams())
            {
                for (size_t phase_index = 0; phase_index < stream_node->get_phases().size(); ++phase_index)
                {
                    const Phase& phase = stream_node->get_phases()[phase_index];
                    auto phaseIt = phase_map.find(phase.get_id());
                    if (phaseIt == phase_map.end())
                    {
                        phaseIt = phase_map.insert(std::make_pair(phase.get_id(), std::vector<std::pair<BaseStreamNode*, int>>())).first;
                    }

                    phaseIt->second.push_back(std::make_pair(stream_node.get(), phase_index));
                }
            }
        }

        return phase_map;
    }

    std::string BlobYamlWriter::get_string(const DramConfig& dram_config)
    {
        std::vector<std::string> param_lines = get_dram_config_params(dram_config);

        std::stringstream result_stream;
        result_stream << "    " << dram_config.buffer_id << ":" << std::endl;
        for (const std::string& param_line : param_lines)
        {
            result_stream << "      " << param_line << std::endl;
        }

        return result_stream.str();
    }

    std::string BlobYamlWriter::get_string(const BaseStreamNode* stream_node, std::size_t phase_index)
    {
        std::vector<std::string> param_lines = get_stream_params(stream_node);
        std::vector<std::string> phase_params = get_phase_params(stream_node->get_phases()[phase_index]);
        param_lines.insert(param_lines.end(), phase_params.begin(), phase_params.end());

        std::sort(param_lines.begin(), param_lines.end());

        std::stringstream result_stream;
        result_stream << "  " << BlobYamlWriter::get_system_wide_stream_id(stream_node) << ":" << std::endl;
        for (const std::string& param_line : param_lines)
        {
            result_stream << "    " << param_line << std::endl;
        }

        return result_stream.str();
    }

    std::vector<std::string> BlobYamlWriter::get_dram_config_params(const DramConfig& dram_config)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("dram_buf_noc_addr: " + get_hex_string(dram_config.dram_buf_noc_addr));
        if (dram_config.dram_buf_read_chunk_size_tiles.has_value())
        {
            const std::string& value_str = std::to_string(dram_config.dram_buf_read_chunk_size_tiles.value());
            param_lines.push_back("dram_buf_read_chunk_size_tiles: " + value_str);
        }
        param_lines.push_back("dram_buf_size_bytes: " + std::to_string(dram_config.dram_buf_size_bytes));
        param_lines.push_back("dram_buf_size_q_slots: " + std::to_string(dram_config.dram_buf_size_q_slots));
        param_lines.push_back("dram_buf_size_tiles: " + std::to_string(dram_config.dram_buf_size_tiles));
        if (dram_config.dram_buf_write_chunk_size_tiles.has_value())
        {
            const std::string& value_str = std::to_string(dram_config.dram_buf_write_chunk_size_tiles.value());
            param_lines.push_back("dram_buf_write_chunk_size_tiles: " + value_str);
        }
        if (dram_config.dram_buf_write_row_chunk_size_bytes.has_value())
        {
            const std::string& value_str = std::to_string(dram_config.dram_buf_write_row_chunk_size_bytes.value());
            param_lines.push_back("dram_buf_write_row_chunk_size_bytes: " + value_str);
        }
        if (dram_config.dram_input.has_value())
        {
            param_lines.push_back("dram_input: " + get_string(dram_config.dram_input.value()));
        }
        if (dram_config.dram_io.has_value())
        {
            param_lines.push_back("dram_io: " + get_string(dram_config.dram_io.value()));
        }
        if (dram_config.dram_output.has_value())
        {
            param_lines.push_back("dram_output: " + get_string(dram_config.dram_output.value()));
        }
        if (dram_config.dram_ram.has_value())
        {
            param_lines.push_back("dram_ram: " + get_string(dram_config.dram_ram.value()));
        }
        if (dram_config.dram_streaming.has_value())
        {
            param_lines.push_back("dram_streaming: " + get_string(dram_config.dram_streaming.value()));
        }
        if (dram_config.dram_streaming_dest.has_value())
        {
            param_lines.push_back("dram_streaming_dest: " + dram_config.dram_streaming_dest.value());
        }
        if (dram_config.dram_scatter_chunk_size_tiles.has_value())
        {
            const std::string& value_str = std::to_string(dram_config.dram_scatter_chunk_size_tiles.value());
            param_lines.push_back("dram_scatter_chunk_size_tiles: " + value_str);
        }
        if (dram_config.dram_scatter_offsets.has_value())
        {
            param_lines.push_back("dram_scatter_offsets: " + get_hex_string(dram_config.dram_scatter_offsets.value()));
        }
        param_lines.push_back("msg_size: " + std::to_string(dram_config.msg_size));
        param_lines.push_back("num_msgs: " + std::to_string(dram_config.num_msgs));
        if (dram_config.reader_index.has_value())
        {
            param_lines.push_back("reader_index: " + std::to_string(dram_config.reader_index.value()));
        }
        if (dram_config.total_readers.has_value())
        {
            param_lines.push_back("total_readers: " + std::to_string(dram_config.total_readers.value()));
        }

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_params(const BaseStreamNode* stream_node)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("msg_size: " + std::to_string(stream_node->get_tile_size()));
        param_lines.push_back("num_iters_in_epoch: " + std::to_string(stream_node->get_num_iterations_in_epoch()));
        param_lines.push_back("num_unroll_iter: " + std::to_string(stream_node->get_num_unrolled_iterations()));
        param_lines.push_back("data_auto_send: " + get_string(stream_node->is_data_auto_send()));
        param_lines.push_back("phase_auto_advance: " + get_string(stream_node->is_phase_auto_advance()));
        param_lines.push_back("reg_update_vc: " + std::to_string(stream_node->get_reg_update_vc()));
        if (stream_node->get_buffer_id().has_value())
        {
            param_lines.push_back("buf_id: " + std::to_string(stream_node->get_buffer_id().value()));
        }
        if (stream_node->is_buffer_intermediate().has_value())
        {
            param_lines.push_back("intermediate: " + get_string(stream_node->is_buffer_intermediate().value()));
        }
        if (stream_node->get_buffer_address().has_value())
        {
            param_lines.push_back("buf_addr: " + get_hex_string(stream_node->get_buffer_address().value()));
        }
        if (stream_node->get_msg_info_buf_addr().has_value())
        {
            param_lines.push_back("msg_info_buf_addr: " + std::to_string(stream_node->get_msg_info_buf_addr().value()));
        }
        if (stream_node->get_dram_buf_noc_addr().has_value())
        {
            param_lines.push_back("dram_buf_noc_addr: " + get_hex_string(stream_node->get_dram_buf_noc_addr().value()));
        }
        if (stream_node->get_buffer_size().has_value())
        {
            param_lines.push_back("buf_size: " + std::to_string(stream_node->get_buffer_size().value()));
        }
        if (stream_node->get_fork_stream_ids().has_value())
        {
            param_lines.push_back("fork_stream_ids: " + get_string(stream_node->get_fork_stream_ids().value()));
        }
        if (stream_node->get_num_fork_streams().has_value())
        {
            param_lines.push_back("num_fork_streams: " + std::to_string(stream_node->get_num_fork_streams().value()));
        }
        if (stream_node->get_group_priority().has_value())
        {
            param_lines.push_back("group_priority: " + std::to_string(stream_node->get_group_priority().value()));
        }
        if (stream_node->is_dram_io().has_value())
        {
            param_lines.push_back("dram_io: " + get_string(stream_node->is_dram_io().value()));
        }
        if (stream_node->is_dram_streaming().has_value())
        {
            param_lines.push_back("dram_streaming: " + get_string(stream_node->is_dram_streaming().value()));
        }
        if (stream_node->is_dram_input().has_value())
        {
            param_lines.push_back("dram_input: " + get_string(stream_node->is_dram_input().value()));
        }
        if (stream_node->is_dram_input_no_push().has_value())
        {
            param_lines.push_back("dram_input_no_push: " + get_string(stream_node->is_dram_input_no_push().value()));
        }
        if (stream_node->is_dram_output().has_value())
        {
            param_lines.push_back("dram_output: " + get_string(stream_node->is_dram_output().value()));
        }
        if (stream_node->is_dram_output_no_push().has_value())
        {
            param_lines.push_back("dram_output_no_push: " + get_string(stream_node->is_dram_output_no_push().value()));
        }
        if (stream_node->get_producer_epoch_id().has_value())
        {
            param_lines.push_back("producer_epoch_id: " + std::to_string(stream_node->get_producer_epoch_id().value()));
        }
        if (stream_node->get_num_scatter_inner_loop().has_value())
        {
            param_lines.push_back("num_scatter_inner_loop: " +
                                  std::to_string(stream_node->get_num_scatter_inner_loop().value()));
        }
        if (stream_node->get_pipe_id().has_value())
        {
            param_lines.push_back("pipe_id: " + std::to_string(stream_node->get_pipe_id().value()));
        }
        if (stream_node->get_pipe_scatter_output_loop_count().has_value())
        {
            param_lines.push_back("pipe_scatter_output_loop_count: " +
                                  std::to_string(stream_node->get_pipe_scatter_output_loop_count().value()));
        }
        if (stream_node->is_resend().has_value())
        {
            param_lines.push_back("resend: " + get_string(stream_node->is_resend().value()));
        }
        if (stream_node->get_buf_base_addr().has_value())
        {
            param_lines.push_back("buf_base_addr: " + get_hex_string(stream_node->get_buf_base_addr().value()));
        }
        if (stream_node->get_buf_full_size_bytes().has_value())
        {
            param_lines.push_back("buf_full_size_bytes: " +
                                  std::to_string(stream_node->get_buf_full_size_bytes().value()));
        }
        if (stream_node->get_num_mblock_buffering().has_value())
        {
            param_lines.push_back("num_mblock_buffering: " +
                                  std::to_string(stream_node->get_num_mblock_buffering().value()));
        }
        if (stream_node->get_scatter_idx().has_value())
        {
            param_lines.push_back("scatter_idx: " + std::to_string(stream_node->get_scatter_idx().value()));
        }
        if (stream_node->get_scatter_order_size().has_value())
        {
            param_lines.push_back("scatter_order_size: " +
                                  std::to_string(stream_node->get_scatter_order_size().value()));
        }
        if (stream_node->get_num_msgs_in_block().has_value())
        {
            param_lines.push_back("num_msgs_in_block: " + std::to_string(stream_node->get_num_msgs_in_block().value()));
        }
        if (stream_node->is_scatter_pack().has_value())
        {
            param_lines.push_back("is_scatter_pack: " + get_string(stream_node->is_scatter_pack().value()));
        }

        if (dynamic_cast<const SendingStreamNode*>(stream_node) != nullptr)
        {
            std::vector<std::string> sending_param_lines = get_sending_stream_params(static_cast<const SendingStreamNode*>(stream_node));
            param_lines.insert(param_lines.end(), sending_param_lines.begin(), sending_param_lines.end());
        }
        else if (dynamic_cast<const ReceivingStreamNode*>(stream_node) != nullptr)
        {
            std::vector<std::string> receiving_param_lines = get_receiving_stream_params(static_cast<const ReceivingStreamNode*>(stream_node));
            param_lines.insert(param_lines.end(), receiving_param_lines.begin(), receiving_param_lines.end());
        }
        else
        {
            throw std::runtime_error("BlobYamlWriter: Unknown stream node type");
        }

        std::vector<std::string> source_lines = get_stream_source_params(stream_node->get_stream_source());
        param_lines.insert(param_lines.end(), source_lines.begin(), source_lines.end());

        std::vector<std::string> destination_lines = get_stream_destination_params(stream_node->get_stream_destination());
        param_lines.insert(param_lines.end(), destination_lines.begin(), destination_lines.end());

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_sending_stream_params(const SendingStreamNode* stream_node)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("dest: " + get_string(stream_node->get_destination_stream_ids()));
        if (stream_node->is_legacy_pack().has_value())
        {
            param_lines.push_back("legacy_pack: " + get_string(stream_node->is_legacy_pack().value()));
        }
        if (stream_node->get_outgoing_data_noc().has_value())
        {
            param_lines.push_back("outgoing_data_noc: " + std::to_string(stream_node->get_outgoing_data_noc().value()));
        }
        if (stream_node->get_vc().has_value())
        {
            param_lines.push_back("vc: " + std::to_string(stream_node->get_vc().value()));
        }
        if (stream_node->get_operand_output_index().has_value())
        {
            param_lines.push_back("output_index: " + std::to_string(stream_node->get_operand_output_index().value()));
        }
        if (stream_node->is_sending_raw_data().has_value())
        {
            param_lines.push_back("moves_raw_data: " + get_string(stream_node->is_sending_raw_data().value()));
        }
        if (stream_node->get_tilized_data_config().has_value())
        {
            const SendingStreamNode::TilizedDataConfig& tilized_data_config = stream_node->get_tilized_data_config().value();
            param_lines.push_back("batch_dim_size: " + std::to_string(tilized_data_config.batch_dim_size));
            param_lines.push_back("r_dim_size: " + std::to_string(tilized_data_config.r_dim_size));
            param_lines.push_back("c_dim_size: " + std::to_string(tilized_data_config.c_dim_size));
            param_lines.push_back("zr_dim_size: " + std::to_string(tilized_data_config.zr_dim_size));
            param_lines.push_back("zc_dim_size: " + std::to_string(tilized_data_config.zc_dim_size));
            param_lines.push_back("skip_col_bytes: " + std::to_string(tilized_data_config.skip_col_bytes));
            param_lines.push_back("skip_col_tile_row_bytes: " + std::to_string(tilized_data_config.skip_col_tile_row_bytes));
            param_lines.push_back("skip_col_row_bytes: " + std::to_string(tilized_data_config.skip_col_row_bytes));
            param_lines.push_back("skip_zcol_bytes: " + std::to_string(tilized_data_config.skip_zcol_bytes));
            param_lines.push_back("skip_col_zrow_bytes: " + std::to_string(tilized_data_config.skip_col_zrow_bytes));
            param_lines.push_back("stride: " + std::to_string(tilized_data_config.stride));
            param_lines.push_back("total_strides: " + std::to_string(tilized_data_config.total_strides));
            param_lines.push_back("stride_offset_size_bytes: " + std::to_string(tilized_data_config.stride_offset_size_bytes));
        }
        if (stream_node->get_buffer_dimensions().has_value())
        {
            const SendingStreamNode::BufferDimensions& buffer_dimensions = stream_node->get_buffer_dimensions().value();
            param_lines.push_back("mblock_k: " + std::to_string(buffer_dimensions.mblock_k));
            param_lines.push_back("mblock_m: " + std::to_string(buffer_dimensions.mblock_m));
            param_lines.push_back("mblock_n: " + std::to_string(buffer_dimensions.mblock_n));
            param_lines.push_back("ublock_ct: " + std::to_string(buffer_dimensions.ublock_ct));
            param_lines.push_back("ublock_rt: " + std::to_string(buffer_dimensions.ublock_rt));
        }

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_receiving_stream_params(const ReceivingStreamNode* stream_node)
    {
        std::vector<std::string> param_lines;
        if (stream_node->get_incoming_data_noc().has_value())
        {
            param_lines.push_back("incoming_data_noc: " + std::to_string(stream_node->get_incoming_data_noc().value()));
        }
        if (stream_node->get_operand_input_index().has_value())
        {
            param_lines.push_back("input_index: " + std::to_string(stream_node->get_operand_input_index().value()));
        }
        if (stream_node->get_buf_space_available_ack_thr().has_value())
        {
            param_lines.push_back("buf_space_available_ack_thr: " + std::to_string(stream_node->get_buf_space_available_ack_thr().value()));
        }

        // Receiving streams do not have destination but we still print this to keep the same output as the legacy pipegen.
        // TODO: Remove this once we refactor blobgen.
        param_lines.push_back("dest: []");

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_source_params(const BaseStreamSource* stream_source)
    {
        if (dynamic_cast<const EndpointStreamSource*>(stream_source) != nullptr)
        {
            return get_stream_endpoint_source_params(static_cast<const EndpointStreamSource*>(stream_source));
        }
        else if (dynamic_cast<const LocalStreamSource*>(stream_source) != nullptr)
        {
            return get_stream_local_source_params(static_cast<const LocalStreamSource*>(stream_source));
        }
        else if (dynamic_cast<const RemoteStreamSource*>(stream_source) != nullptr)
        {
            return get_stream_remote_source_params(static_cast<const RemoteStreamSource*>(stream_source));
        }
        else
        {
            throw std::runtime_error("BlobYamlWriter: Unknown stream source type");
        }
    }

    std::vector<std::string> BlobYamlWriter::get_stream_endpoint_source_params(const EndpointStreamSource* stream_source)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("source_endpoint: true");
        if (stream_source->is_ptrs_not_zero().has_value())
        {
            param_lines.push_back("ptrs_not_zero: " + get_string(stream_source->is_ptrs_not_zero().value()));
        }

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_local_source_params(const LocalStreamSource* stream_source)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("local_sources_connected: true");

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_remote_source_params(const RemoteStreamSource* stream_source)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("remote_source: true");

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_destination_params(const BaseStreamDestination* stream_destination)
    {
        if (dynamic_cast<const EndpointStreamDestination*>(stream_destination) != nullptr)
        {
            return get_stream_endpoint_destination_params(static_cast<const EndpointStreamDestination*>(stream_destination));
        }
        else if (dynamic_cast<const LocalStreamDestination*>(stream_destination) != nullptr)
        {
            return get_stream_local_destination_params(static_cast<const LocalStreamDestination*>(stream_destination));
        }
        else if (dynamic_cast<const RemoteStreamDestination*>(stream_destination) != nullptr)
        {
            return get_stream_remote_destination_params(static_cast<const RemoteStreamDestination*>(stream_destination));
        }
        else
        {
            throw std::runtime_error("BlobYamlWriter: Unknown stream destination type");
        }
    }

    std::vector<std::string> BlobYamlWriter::get_stream_endpoint_destination_params(const EndpointStreamDestination* stream_destination)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("receiver_endpoint: true");

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_local_destination_params(const LocalStreamDestination* stream_destination)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("local_receiver: true");
        if (stream_destination->is_local_receiver_tile_clearing().has_value())
        {
            param_lines.push_back("local_receiver_tile_clearing: " + get_string(stream_destination->is_local_receiver_tile_clearing().value()));
        }

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_stream_remote_destination_params(const RemoteStreamDestination* stream_destination)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("remote_receiver: true");

        return param_lines;
    }

    std::vector<std::string> BlobYamlWriter::get_phase_params(const Phase& phase)
    {
        std::vector<std::string> param_lines;
        param_lines.push_back("num_msgs: " + std::to_string(phase.get_tiles_count()));
        param_lines.push_back("next_phase_src_change: " + get_string(phase.is_next_phase_src_change()));
        param_lines.push_back("next_phase_dest_change: " + get_string(phase.is_next_phase_dest_change()));
        param_lines.push_back("phase_auto_config: " + get_string(phase.is_next_phase_auto_configured()));
        param_lines.push_back("unroll_iter: " + std::to_string(phase.get_unrolled_iteration_index()));

        return param_lines;
    }

    std::string BlobYamlWriter::get_system_wide_stream_id(const pipegen2::BaseStreamNode *const stream)
    {
        std::stringstream stream_id;
        stream_id << blob_yaml_chip_key << stream->get_chip_id()
                  << blob_yaml_tensix_y_key << stream->get_tensix_coord_y()
                  << blob_yaml_tensix_x_key << stream->get_tensix_coord_x()
                  << blob_yaml_stream_key << stream->get_stream_id();
        return stream_id.str();
    }

    std::string BlobYamlWriter::get_hex_string(int64_t number)
    {
        std::stringstream stream;
        stream << "0x" << std::hex << number;
        return stream.str();
    }

    std::string BlobYamlWriter::get_string(bool value)
    {
        std::stringstream stream;
        stream << std::boolalpha << value;
        return stream.str();
    }

    template <typename T> std::string BlobYamlWriter::get_string(const std::vector<T>& values)
    {
        std::stringstream result_stream;
        result_stream << "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            result_stream << values[i];
            if (i < values.size() - 1)
            {
                result_stream << ", ";
            }
        }
        result_stream << "]";

        return result_stream.str();
    }

    template <typename T> std::string BlobYamlWriter::get_hex_string(const std::vector<T>& values)
    {
        std::stringstream result_stream;
        result_stream << std::hex << "[0x";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            result_stream << std::hex << values[i];
            if (i < values.size() - 1)
            {
                result_stream << ", 0x";
            }
        }
        result_stream << "]";

        return result_stream.str();
    }
}