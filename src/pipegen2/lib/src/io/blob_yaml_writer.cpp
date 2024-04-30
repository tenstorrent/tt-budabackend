// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_yaml_writer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "device/tt_xy_pair.h"
#include "model/typedefs.h"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{
    std::string BlobYamlWriter::s_dram_blob_yaml_line = "dram_blob:";
    std::string BlobYamlWriter::s_dram_perf_dump_line = "dram_perf_dump_blob:";
    // TODO: this attribute has unfortunate naming for host spill mode. Changing it will influence blobgen. Fix at
    // some point.
    std::string BlobYamlWriter::s_dram_perf_buf_noc_addr = "dram_perf_buf_noc_addr:";
    // TODO: this attribute has unfortunate naming for host spill mode. Changing it will influence blobgen. Fix at
    // some point.
    std::string BlobYamlWriter::s_dram_perf_buf_max_req = "dram_perf_buf_max_req:";
    std::string BlobYamlWriter::s_phase_blob_yaml_prefix = "phase_";
    std::string BlobYamlWriter::s_global_info_blob_yaml_prefix = "global_info_blob:";

    // Writes content line into blob yaml. Using macro instead of a function for better perf, because otherwise we
    // would create/concatenate bunch of strings bunch of times in order to pass output string to the function.
    // Don't replace '\n' with std::endl since writing std::endl causes a buffer flush and degrades performance.
    #define BYW_WRITE_LINE(content) m_blob_yaml_file_stream << m_current_indentation << content << '\n'

    BlobYamlWriter::BlobYamlWriter(const std::string& blob_yaml_path) :
        m_current_indentation_level(0)
    {
        try
        {
            std::filesystem::path directory = std::filesystem::path(blob_yaml_path).parent_path();
            if (!std::filesystem::exists(directory))
            {
                std::filesystem::create_directories(directory);
            }
        }
        catch (std::filesystem::filesystem_error const& ex)
        {
            throw BlobYamlIOException("Failed to create directory for blob.yaml: " + std::string(ex.what()));
        }

        m_blob_yaml_file_stream.open(blob_yaml_path);

        if (!m_blob_yaml_file_stream.is_open())
        {
            throw BlobYamlIOException("Failed to open " + blob_yaml_path + " for writing.");
        }
    }

    BlobYamlWriter::~BlobYamlWriter()
    {
        m_blob_yaml_file_stream.close();
    }

    void BlobYamlWriter::write_blob_yaml(const StreamGraphCollection* stream_graph_collection,
                                         const PerfInfoManager& perf_info_manager)
    {
        write_ncrisc_configs(stream_graph_collection->get_stream_graphs());
        write_phase_configs(stream_graph_collection->get_stream_graphs());
        write_global_info(stream_graph_collection);

        if (perf_info_manager.is_perf_dump_enabled())
        {
            write_dram_perf_info(perf_info_manager);
        }
    }

    void BlobYamlWriter::write_ncrisc_configs(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        BYW_WRITE_LINE(s_dram_blob_yaml_line);

        std::map<tt_cxys_pair, const std::vector<NcriscConfig>*> ncrisc_config_list;

        for (const auto& stream_graph : stream_graphs)
        {
            ncrisc_config_list.merge(collect_ncrisc_configs(stream_graph.get()));
        }

        for (const auto& [stream_id_key, ncrisc_configs] : ncrisc_config_list)
        {
            if (ncrisc_configs->empty())
            {
                continue;
            }

            IndentYaml indent(this);
            BYW_WRITE_LINE(get_stream_node_string(stream_id_key) << ":");

            IndentYaml indent2(this);
            unsigned int idx = 0;
            for (const NcriscConfig& ncrisc_config : *ncrisc_configs)
            {
                BYW_WRITE_LINE(get_string(idx) << ":");

                IndentYaml indent3(this);
                write_ncrisc_params(ncrisc_config);
                ++idx;
            }
        }
    }

    std::map<tt_cxys_pair, const std::vector<NcriscConfig>*>
    BlobYamlWriter::collect_ncrisc_configs(const StreamGraph* stream_graph)
    {
        std::map<tt_cxys_pair, const std::vector<NcriscConfig>*> ncrisc_configs_map;

        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            ncrisc_configs_map[stream_node->get_stream_location_with_id()] = &stream_node->get_ncrisc_configs();
        }

        return ncrisc_configs_map;
    }

    void BlobYamlWriter::write_phase_configs(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        std::map<PhaseId, std::map<tt_cxys_pair, const StreamConfig*>> phase_map =
            collect_phase_map(stream_graphs);

        write_phase_map(phase_map);
    }

    std::map<PhaseId, std::map<tt_cxys_pair, const StreamConfig*>> BlobYamlWriter::collect_phase_map(
        const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        std::map<PhaseId, std::map<tt_cxys_pair, const StreamConfig*>> phase_map;

        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graphs)
        {
            collect_stream_graph_phases(stream_graph.get(), phase_map);
        }

        return phase_map;
    }

    void BlobYamlWriter::collect_stream_graph_phases(
        const StreamGraph* stream_graph,
        std::map<PhaseId, std::map<tt_cxys_pair, const StreamConfig*>>& phase_map)
    {
        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            for (const PhaseConfig& phase_config : stream_node->get_phase_configs())
            {
                phase_map[phase_config.phase_id][stream_node->get_stream_location_with_id()] = &phase_config.config;
            }
        }
    }

    void BlobYamlWriter::write_phase_map(
        const std::map<PhaseId, std::map<tt_cxys_pair, const StreamConfig*>>& phase_map)
    {
        for (const auto& [phase_id, phase_configs] : phase_map)
        {
            BYW_WRITE_LINE(s_phase_blob_yaml_prefix << get_string(phase_id) << ":");
            for (const auto& [stream_id, stream_config] : phase_configs)
            {
                IndentYaml indent(this);
                BYW_WRITE_LINE(get_stream_node_string(stream_id) << ":");

                IndentYaml indent2(this);
                write_stream_params(*stream_config, phase_id);
            }
        }
    }

    void BlobYamlWriter::write_dram_perf_info(const PerfInfoManager& perf_info_manager)
    {
        BYW_WRITE_LINE(s_dram_perf_dump_line);

        std::map<tt_cxy_pair, std::vector<uint64_t>> workers_to_noc_addr_info =
            perf_info_manager.get_dram_perf_buf_noc_addr_info();
        std::map<tt_cxy_pair, std::vector<uint64_t>> workers_to_max_req_info =
            perf_info_manager.get_dram_perf_buf_max_req_info();

        IndentYaml indent(this);

        for (auto& worker_info_pair : workers_to_noc_addr_info)
        {
            const tt_cxy_pair& worker = worker_info_pair.first;

            BYW_WRITE_LINE(get_core_location_string(worker) << ":");

            IndentYaml indent(this);
            BYW_WRITE_LINE(s_dram_perf_buf_noc_addr << " " << get_hex_string(worker_info_pair.second));
            BYW_WRITE_LINE(s_dram_perf_buf_max_req << " " << get_string(workers_to_max_req_info[worker]));
        }
    }

    void BlobYamlWriter::write_global_info(const StreamGraphCollection* stream_graph_collection)
    {
        BYW_WRITE_LINE(s_global_info_blob_yaml_prefix);

        if (stream_graph_collection->get_ncrisc_fallback_buffers_allocations_per_core().empty())
        {
            return;
        }
        
        IndentYaml indent(this);

        for (const auto& [core_location, ncrisc_fallback_buffer_allocation] :
             stream_graph_collection->get_ncrisc_fallback_buffers_allocations_per_core())
        {
            BYW_WRITE_LINE(get_core_location_string(core_location) << ":");

            IndentYaml indent2(this);

            BYW_WRITE_LINE(
                "ncrisc_fallback_buffer_l1_address: " <<
                get_hex_string(ncrisc_fallback_buffer_allocation->get_address()));

            BYW_WRITE_LINE("ncrisc_fallback_buffer_size: " << ncrisc_fallback_buffer_allocation->get_size());
        }
    }

    std::string BlobYamlWriter::get_core_location_string(const tt_cxy_pair& core_location)
    {
        std::stringstream ss;
        ss << "chip_" << core_location.chip << "__y_" << core_location.y << "__x_" << core_location.x;
        return ss.str();
    }

    std::string BlobYamlWriter::get_stream_node_string(const tt_cxys_pair& stream_location)
    {
        std::stringstream ss;
        std::string core_location_string = get_core_location_string(stream_location.to_cxy_pair());
        ss <<  core_location_string << "__stream_id_" << stream_location.stream_id;
        return ss.str();
    }

    void BlobYamlWriter::write_ncrisc_params(const NcriscConfig& ncrisc_config)
    {
        // Keep these parameters in sorted order, for easier manual comparison with pipegen1 yamls.
        write_param("dram_buf_noc_addr", ncrisc_config.dram_buf_noc_addr, &BlobYamlWriter::get_hex_string);
        write_optionally("dram_buf_read_chunk_size_tiles", ncrisc_config.dram_buf_read_chunk_size_tiles);
        write_param("dram_buf_size_bytes", ncrisc_config.dram_buf_size_bytes);
        write_param("dram_buf_size_q_slots", ncrisc_config.dram_buf_size_q_slots);
        write_param("dram_buf_size_tiles", ncrisc_config.dram_buf_size_tiles);
        write_optionally("dram_buf_write_chunk_size_tiles", ncrisc_config.dram_buf_write_chunk_size_tiles);
        write_optionally("dram_buf_write_row_chunk_size_bytes", ncrisc_config.dram_buf_write_row_chunk_size_bytes);
        write_optionally("dram_input", ncrisc_config.dram_input);
        write_optionally("dram_io", ncrisc_config.dram_io);
        write_optionally("dram_output", ncrisc_config.dram_output);
        write_optionally("dram_padding", ncrisc_config.dram_padding);
        write_optionally("dram_ram", ncrisc_config.dram_ram);
        write_optionally("dram_scatter_chunk_size_tiles", ncrisc_config.dram_scatter_chunk_size_tiles);
        write_optionally("dram_scatter_offsets", ncrisc_config.dram_scatter_offsets, &BlobYamlWriter::get_hex_string);
        write_optionally("dram_scatter_offsets_full_size", ncrisc_config.dram_scatter_offsets_full_size);
        write_optionally("dram_streaming", ncrisc_config.dram_streaming);
        write_optionally("dram_streaming_dest", ncrisc_config.dram_streaming_dest);
        write_param("msg_size", ncrisc_config.msg_size);
        write_param("num_msgs", ncrisc_config.num_msgs);
        write_optionally("reader_index", ncrisc_config.reader_index);
        write_optionally("total_readers", ncrisc_config.total_readers);
    }

    void BlobYamlWriter::write_stream_params(const StreamConfig& stream_config, const PhaseId phase_id)
    {
        // TODO: Maybe move string serialization to StreamConfig class. It would save some code and we would not have to
        // keep track of newly added fields in multiple places.

        // Write phase_id in the beginning of the stream config. This is useful for debugging.
        write_param("phase_id", phase_id);

        // Keep these parameters in sorted order, for easier manual comparison with pipegen1 yamls.
        write_optionally("arb_group_size", stream_config.get_arb_group_size());
        write_optionally("batch_dim_size", stream_config.get_batch_dim_size());
        write_optionally("buf_addr", stream_config.get_buf_addr(), &BlobYamlWriter::get_hex_string);
        write_optionally("buf_base_addr", stream_config.get_buf_base_addr(), &BlobYamlWriter::get_hex_string);
        write_optionally("buf_full_size_bytes", stream_config.get_buf_full_size_bytes());
        write_optionally("buf_id", stream_config.get_buf_id());
        write_optionally("buf_size", stream_config.get_buffer_size());
        write_optionally("buf_space_available_ack_thr", stream_config.get_buf_space_available_ack_thr());
        write_optionally("c_dim_loop_num_rows", stream_config.get_c_dim_loop_num_rows());
        write_optionally("c_dim_size", stream_config.get_c_dim_size());
        write_optionally("data_auto_send", stream_config.get_data_auto_send());
        write_optionally("data_buf_no_flow_ctrl", stream_config.get_data_buf_no_flow_ctrl());
        write_optionally("dest_data_buf_no_flow_ctrl", stream_config.get_dest_data_buf_no_flow_ctrl());
        write_optionally("dest", stream_config.get_dest(), &BlobYamlWriter::get_stream_dest_string);
        write_optionally("dram_buf_noc_addr", stream_config.get_dram_buf_noc_addr(), &BlobYamlWriter::get_hex_string);
        write_optionally("dram_embeddings_row_shift", stream_config.get_dram_embeddings_row_shift());
        write_optionally("dram_embeddings_row_tiles", stream_config.get_dram_embeddings_row_tiles());
        write_optionally("dram_input", stream_config.get_dram_input());
        write_optionally("dram_input_no_push", stream_config.get_dram_input_no_push());
        write_optionally("dram_io", stream_config.get_dram_io());
        write_optionally("dram_output", stream_config.get_dram_output());
        write_optionally("dram_output_no_push", stream_config.get_dram_output_no_push());
        write_optionally("dram_ram", stream_config.get_dram_ram());
        write_optionally("dram_streaming", stream_config.get_dram_streaming());
        write_optionally("dram_writes_with_cmd_buf", stream_config.get_dram_writes_with_cmd_buf());
        write_optionally("eth_sender", stream_config.get_eth_sender());
        write_optionally("eth_receiver", stream_config.get_eth_receiver());
        write_optionally("follow_by_receiver_dummy_phase", stream_config.get_follow_by_receiver_dummy_phase());
        write_optionally("follow_by_sender_dummy_phase", stream_config.get_follow_by_sender_dummy_phase());
        write_optionally("fork_stream_ids", stream_config.get_fork_streams(), &BlobYamlWriter::get_stream_ids_string);
        write_optionally("has_packer_mcast_opt", stream_config.get_has_packer_mcast_opt());
        write_optionally("hw_tilize", stream_config.get_hw_tilize());
        write_optionally("incoming_data_noc", stream_config.get_incoming_data_noc());
        write_optionally("input_index", stream_config.get_input_index());
        write_optionally("intermediate", stream_config.get_buffer_intermediate());
        write_optionally("is_scatter_pack", stream_config.get_is_scatter_pack());
        write_optionally("legacy_pack", stream_config.get_legacy_pack());
        write_optionally("linked", stream_config.get_linked());
        write_optionally("local_receiver", stream_config.get_local_receiver());
        write_optionally("local_receiver_tile_clearing", stream_config.get_local_receiver_tile_clearing());
        write_optionally("local_sources_connected", stream_config.get_local_sources_connected());
        write_optionally("local_stream_clear_num", stream_config.get_local_stream_clear_num());
        write_optionally("mblock_k", stream_config.get_mblock_k());
        write_optionally("mblock_m", stream_config.get_mblock_m());
        write_optionally("mblock_n", stream_config.get_mblock_n());
        write_optionally("moves_raw_data", stream_config.get_moves_raw_data());
        write_optionally("msg_info_buf_addr", stream_config.get_msg_info_buf_addr(), &BlobYamlWriter::get_hex_string);
        write_optionally("msg_size", stream_config.get_msg_size());
        write_optionally("ncrisc_clear", stream_config.get_ncrisc_clear());
        write_optionally("next_phase_dest_change", stream_config.get_next_phase_dest_change());
        write_optionally("next_phase_src_change", stream_config.get_next_phase_src_change());
        write_optionally("num_mcast_dests", stream_config.get_num_mcast_dests());
        write_optionally("num_fork_streams", stream_config.get_num_fork_streams());
        write_optionally("num_iters_in_epoch", stream_config.get_num_iters_in_epoch());
        write_optionally("num_mblock_buffering", stream_config.get_num_mblock_buffering());
        write_optionally("num_msgs", stream_config.get_num_msgs());
        write_optionally("num_msgs_in_block", stream_config.get_num_msgs_in_block());
        write_optionally("num_scatter_inner_loop", stream_config.get_num_scatter_inner_loop());
        write_optionally("num_unroll_iter", stream_config.get_num_unroll_iter());
        write_optionally("outgoing_data_noc", stream_config.get_outgoing_data_noc());
        write_optionally("output_index", stream_config.get_output_index());
        write_optionally("overlay_blob_extra_size", stream_config.get_overlay_blob_extra_size());
        write_optionally("padding_scatter_order_size", stream_config.get_padding_scatter_order_size());
        write_optionally("phase_auto_advance", stream_config.get_phase_auto_advance());
        write_optionally("phase_auto_config", stream_config.get_phase_auto_config());
        write_optionally("pipe_id", stream_config.get_pipe_id());
        write_optionally("pipe_scatter_output_loop_count", stream_config.get_pipe_scatter_output_loop_count());
        write_optionally("producer_epoch_id", stream_config.get_producer_epoch_id());
        write_optionally("ptrs_not_zero", stream_config.get_ptrs_not_zero());
        write_optionally("r_dim_size", stream_config.get_r_dim_size());
        write_optionally("receiver_endpoint", stream_config.get_receiver_endpoint());
        write_optionally("reg_update_vc", stream_config.get_reg_update_vc());
        write_optionally("remote_receiver", stream_config.get_remote_receiver());
        write_optionally("remote_source", stream_config.get_remote_source());
        write_optionally("remote_src_is_mcast", stream_config.get_remote_src_is_mcast());
        write_optionally("remote_src_update_noc", stream_config.get_remote_src_update_noc());
        write_optionally("resend", stream_config.get_resend());
        write_optionally("scatter_idx", stream_config.get_scatter_idx());
        write_optionally("scatter_list_indicies_per_input", stream_config.get_scatter_list_indicies_per_input());
        write_optionally("scatter_list_indicies_per_tile", stream_config.get_scatter_list_indicies_per_tile());
        write_optionally("scatter_list_stream_id", stream_config.get_scatter_list_stream_id(),
                         &BlobYamlWriter::get_stream_id_string);
        write_optionally("scatter_order_size", stream_config.get_scatter_order_size());
        write_optionally("skip_col_bytes", stream_config.get_skip_col_bytes());
        write_optionally("skip_col_row_bytes", stream_config.get_skip_col_row_bytes());
        write_optionally("skip_col_tile_row_bytes", stream_config.get_skip_col_tile_row_bytes());
        write_optionally("skip_col_zrow_bytes", stream_config.get_skip_col_zrow_bytes());
        write_optionally("skip_zcol_bytes", stream_config.get_skip_zcol_bytes());
        write_optionally("source_endpoint", stream_config.get_source_endpoint());
        write_optionally("space_shared_with_operand", stream_config.get_space_shared_with_operand());
        write_optionally("space_shared_with_stream", stream_config.get_space_shared_with_stream(),
                         &BlobYamlWriter::get_stream_id_string);
        write_optionally("src", stream_config.get_source());
        write_optionally("src_dest_index", stream_config.get_src_dest_index());
        write_optionally("stride", stream_config.get_stride());
        write_optionally("stride_offset_size_bytes", stream_config.get_stride_offset_size_bytes());
        write_optionally("tile_dim_c", stream_config.get_tile_dim_c());
        write_optionally("tile_dim_r", stream_config.get_tile_dim_r());
        write_optionally("tilize_row_col_offset", stream_config.get_tilize_row_col_offset());
        write_optionally("total_strides", stream_config.get_total_strides());
        write_optionally("ublock_ct", stream_config.get_ublock_ct());
        write_optionally("ublock_rt", stream_config.get_ublock_rt());
        write_optionally("unroll_iter", stream_config.get_unroll_iter());
        write_optionally("use_ethernet_fw_stream", stream_config.get_use_ethernet_fw_stream());
        write_optionally("vc", stream_config.get_vc());
        write_optionally("zc_dim_size", stream_config.get_zc_dim_size());
        write_optionally("zr_dim_size", stream_config.get_zr_dim_size());
    }

    template <typename T>
    void BlobYamlWriter::write_optionally(std::string&& name,
                                          const std::optional<T>& opt,
                                          std::string (*converter_f)(const T&))
    {
        if (opt.has_value())
        {
            write_param(std::move(name), opt.value(), converter_f);
        }
    }

    template <typename T>
    void BlobYamlWriter::write_param(std::string&& name,
                                     const T& value,
                                     std::string (*converter_f)(const T&))
    {
        BYW_WRITE_LINE(name << ": " << (converter_f ? converter_f(value) : get_string(value)));
    }

    std::string BlobYamlWriter::get_string(const StreamNode* stream_node)
    {
        // TODO: Remove function get_stream_node_string and replace this call with stream_node->get_string() or similar.
        return get_stream_node_string(stream_node->get_stream_location_with_id());
    }

    std::string BlobYamlWriter::get_string(bool value)
    {
        std::stringstream stream;
        stream << std::boolalpha << value;
        return stream.str();
    }

    std::string BlobYamlWriter::get_string(NOC_ROUTE value)
    {
        return get_string(static_cast<unsigned int>(value));
    }

    std::string BlobYamlWriter::get_string(const std::string& value)
    {
        return value;
    }

    template<typename T>
    typename std::enable_if<
        std::is_constructible<
            std::string,
            decltype(std::to_string(std::declval<T>()))
        >::value,
        std::string
    >::type BlobYamlWriter::get_string(T value)
    {
        return std::to_string(value);
    }

    template<typename T>
    std::string BlobYamlWriter::get_string(const std::vector<T>& values,
                                           std::string (*converter_f)(const T&))
    {
        std::stringstream result_stream;
        result_stream << "[";
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            result_stream
                << (converter_f ? converter_f(values[i]) : get_string(values[i]))
                << (i < values.size() - 1 ? ", " : "");
        }
        result_stream << "]";
        return result_stream.str();
    }

    std::string BlobYamlWriter::get_hex_string(const unsigned int& number)
    {
        return get_hex_string((std::uint64_t)number);
    }

    std::string BlobYamlWriter::get_hex_string(const std::uint64_t& number)
    {
        std::stringstream stream;
        stream << "0x" << std::hex << number;
        return stream.str();
    }

    std::string BlobYamlWriter::get_hex_string(const std::vector<std::uint64_t>& numbers)
    {
        return get_string(numbers, &BlobYamlWriter::get_hex_string);
    }

    std::string BlobYamlWriter::get_stream_id_string(StreamNode* const & stream_node)
    {
        return std::to_string(stream_node->get_stream_id());
    }

    std::string BlobYamlWriter::get_stream_ids_string(const std::vector<StreamNode*>& stream_nodes)
    {
        return get_string(stream_nodes, &BlobYamlWriter::get_stream_id_string);
    }

    std::string BlobYamlWriter::get_stream_dest_string(const std::vector<StreamNode*>& stream_dest)
    {
        if (stream_dest.size() < 2)
        {
            return get_string(stream_dest);
        }

        std::vector<StreamNode*> sorted_stream_dest(stream_dest);
        std::sort(sorted_stream_dest.begin(), sorted_stream_dest.end(),
                  [](const StreamNode* stream1, const StreamNode* stream2) -> bool
                  {
                      return stream1->get_physical_location() < stream2->get_physical_location();
                  });

        std::stringstream dest_str;
        dest_str <<
            "[" << get_string(sorted_stream_dest.front()) << "," << get_string(sorted_stream_dest.back()) << "]";
        return dest_str.str();
    }

    BlobYamlWriter::IndentYaml::IndentYaml(BlobYamlWriter* blob_yaml_writer) :
        m_blob_yaml_writer(blob_yaml_writer)
    {
        m_blob_yaml_writer->m_current_indentation_level += c_indentation_increment;
        update_indentation();
    }

    BlobYamlWriter::IndentYaml::~IndentYaml()
    {
        m_blob_yaml_writer->m_current_indentation_level -= c_indentation_increment;
        update_indentation();
    }

    void BlobYamlWriter::IndentYaml::update_indentation()
    {
        m_blob_yaml_writer->m_current_indentation =
            m_blob_yaml_writer->m_current_indentation_level > 0 ?
            std::string(m_blob_yaml_writer->m_current_indentation_level, ' ') : "";
    }

    #undef BYW_WRITE_LINE
}