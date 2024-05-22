// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/pipe_graph/pg_buffer.h"

namespace pipegen2
{

class PGBufferMock : public PGBuffer
{
public:
    PGBufferMock(NodeId id) { set_id(id); }

    PGBufferMock(
        const BufferType buffer_type,
        const NodeId uniqid,
        const int id,
        const unsigned int epoch_tiles,
        const unsigned int tiles_per_input,
        const std::vector<ChipId>& chip_id,
        const std::vector<std::size_t>& core_coordinates,
        const unsigned int size_tiles,
        const unsigned int scatter_gather_num_tiles,
        const unsigned int replicate,
        const unsigned int tile_size,
        const int dram_io_flag_is_remote,
        const int dram_buf_flag,
        const int dram_buf_streaming,
        const int write_dram_buf_flag,
        const unsigned int dram_chan,
        const unsigned int dram_sub_chan,
        const std::uint64_t dram_addr,
        const unsigned int q_slots,
        const int dram_io_flag,
        const unsigned int ublock_rt,
        const unsigned int ublock_ct,
        const unsigned int mblock_m,
        const unsigned int mblock_n,
        const unsigned int mblock_k,
        const unsigned int untilized_output,
        const unsigned int untilized_output_full_r_dim,
        const unsigned int untilized_output_full_c_dim,
        const unsigned int untilized_output_r_dim,
        const unsigned int untilized_output_c_dim,
        const unsigned int untilized_output_z_dim,
        const unsigned int untilized_output_type_0_zdim,
        const unsigned int untilized_output_type_1_zdim,
        const unsigned int untilized_output_tile_dim_r,
        const unsigned int untilized_output_tile_dim_c,
        const int dram_ram_flag,
        const unsigned int tile_clear_granularity,
        const NodeId buffer_space_shared,
        const int producer_epoch_id,
        const int is_scatter,
        const int ethernet_chan,
        const int dram_prefetch_incoming_noc_id,
        const PrefetchType prefetch_type,
        const int embedding_table,
        const int embedding_table_row_size_per_core,
        const int embedding_table_core_c_div,
        const int embedding_index,
        const unsigned int embedding_indices_per_tile,
        const unsigned int embedding_indices_per_input,
        const int hw_tilize,
        const unsigned int tilize_mblock_n_loop_num_rows,
        const unsigned int tilize_row_col_offset,
        const int is_padding,
        const int use_ethernet_fw_stream,
        const unsigned int overlay_blob_size,
        const int is_post_tm_relay_buf)
    {
        set_type(buffer_type);
        set_id(uniqid);
        set_operand_id(id);
        set_num_epoch_tiles(epoch_tiles);
        set_num_tiles_per_input(tiles_per_input);
        set_chip_id(chip_id[0]);
        set_logical_core_y(core_coordinates[0]);
        set_logical_core_x(core_coordinates[1]);
        set_size_tiles(size_tiles);
        set_scatter_gather_num_tiles(scatter_gather_num_tiles);
        set_replicate_count(replicate);
        set_tile_size(tile_size);
        set_dram_io_flag_is_remote(dram_io_flag_is_remote == 1);
        set_dram_buf_flag(dram_buf_flag == 1);
        set_dram_buf_streaming(dram_buf_streaming == 1);
        set_write_dram_buf_flag(write_dram_buf_flag == 1);
        set_dram_channel(dram_chan);
        set_dram_sub_channel(dram_sub_chan);
        set_dram_address(dram_addr);
        set_num_queue_slots(q_slots);
        set_dram_io_flag(dram_io_flag == 1);
        set_ublock_rt(ublock_rt);
        set_ublock_ct(ublock_ct);
        set_mblock_m(mblock_m);
        set_mblock_n(mblock_n);
        set_mblock_k(mblock_k);
        set_moves_raw_data(untilized_output == 1);
        set_untilized_output_full_r_dim(untilized_output_full_r_dim);
        set_untilized_output_full_c_dim(untilized_output_full_c_dim);
        set_untilized_output_r_dim(untilized_output_r_dim);
        set_untilized_output_c_dim(untilized_output_c_dim);
        set_untilized_output_z_dim(untilized_output_z_dim);
        set_untilized_output_type_0_zdim(untilized_output_type_0_zdim);
        set_untilized_output_type_1_zdim(untilized_output_type_1_zdim);
        set_untilized_output_tile_dim_r(untilized_output_tile_dim_r);
        set_untilized_output_tile_dim_c(untilized_output_tile_dim_c);
        set_dram_ram_flag(dram_ram_flag == 1);
        set_tile_clear_granularity(tile_clear_granularity);
        set_shared_space_buffer_id(buffer_space_shared);
        set_producer_epoch_id(producer_epoch_id);
        set_is_scatter(is_scatter == 1);
        set_ethernet_channel(ethernet_chan);
        set_dram_prefetch_incoming_noc_id(dram_prefetch_incoming_noc_id);
        set_prefetch_type(prefetch_type);
        set_embedding_table(embedding_table);
        set_embedding_table_row_size_per_core(embedding_table_row_size_per_core);
        set_embedding_table_core_c_div(embedding_table_core_c_div);
        set_embedding_index(embedding_index);
        set_embedding_indices_per_tile(embedding_indices_per_tile);
        set_embedding_indices_per_input(embedding_indices_per_input);
        set_hw_tilize(hw_tilize == 1);
        set_tilize_mblock_n_loop_num_rows(tilize_mblock_n_loop_num_rows);
        set_tilize_row_col_offset(tilize_row_col_offset);
        set_is_padding(is_padding == 1);
        set_use_ethernet_fw_stream(use_ethernet_fw_stream == 1);
        set_overlay_blob_size(overlay_blob_size);
        set_is_post_tm_relay_buf(is_post_tm_relay_buf == 1);
    }

    std::vector<std::string> to_json_list_of_strings_all_attributes()
    {
        return {
            "buffer_" + std::to_string(get_id()) + ":",
            "buffer_type: " + convert_buffer_type_to_string(get_type()),
            "uniqid: " + std::to_string(get_id()),
            "id: " + std::to_string(get_operand_id()),
            "epoch_tiles: " + std::to_string(get_num_epoch_tiles()),
            "size_tiles: " + std::to_string(get_size_tiles()),
            "tile_size: " + std::to_string(get_tile_size()),
            "tiles_per_input: " + std::to_string(get_num_tiles_per_input()),
            "scatter_gather_num_tiles: " + std::to_string(get_scatter_gather_num_tiles()),
            "q_slots: " + std::to_string(get_num_queue_slots()),
            "dram_io_flag: " + std::to_string(get_dram_io_flag() ? 1 : 0),
            "dram_io_flag_is_remote: " + std::to_string(get_dram_io_flag_is_remote() ? 1 : 0),
            "dram_buf_streaming: " + std::to_string(get_dram_buf_streaming() ? 1 : 0),
            "dram_buf_flag: " + std::to_string(get_dram_buf_flag() ? 1 : 0),
            "write_dram_buf_flag: " + std::to_string(get_write_dram_buf_flag() ? 1 : 0),
            "dram_ram_flag: " + std::to_string(get_dram_ram_flag() ? 1 : 0),
            "untilized_output: " + std::to_string(get_moves_raw_data() ? 1 : 0),
            "untilized_output_full_r_dim: " + std::to_string(get_untilized_output_full_r_dim()),
            "untilized_output_full_c_dim: " + std::to_string(get_untilized_output_full_c_dim()),
            "untilized_output_r_dim: " + std::to_string(get_untilized_output_r_dim()),
            "untilized_output_c_dim: " + std::to_string(get_untilized_output_c_dim()),
            "untilized_output_z_dim: " + std::to_string(get_untilized_output_z_dim()),
            "untilized_output_type_0_zdim: " + std::to_string(get_untilized_output_type_0_zdim()),
            "untilized_output_type_1_zdim: " + std::to_string(get_untilized_output_type_1_zdim()),
            "untilized_output_tile_dim_r: " + std::to_string(get_untilized_output_tile_dim_r()),
            "untilized_output_tile_dim_c: " + std::to_string(get_untilized_output_tile_dim_c()),
            "ublock_rt: " + std::to_string(get_ublock_rt()),
            "ublock_ct: " + std::to_string(get_ublock_ct()),
            "mblock_m: " + std::to_string(get_mblock_m()),
            "mblock_n: " + std::to_string(get_mblock_n()),
            "mblock_k: " + std::to_string(get_mblock_k()),
            "tile_clear_granularity: " + std::to_string(get_tile_clear_granularity()),
            "buffer_space_shared: " + std::to_string(get_shared_space_buffer_id()),
            "producer_epoch_id: " + std::to_string(get_producer_epoch_id()),
            "is_scatter: " + std::to_string(is_scatter() ? 1 : 0),
            "replicate: " + std::to_string(get_replicate_count()),
            "ethernet_chan: " + std::to_string(get_ethernet_channel()),
            "dram_chan: " + std::to_string(get_dram_channel()),
            "dram_sub_chan: " + std::to_string(get_dram_sub_channel()),
            "dram_addr: " + std::to_string(get_dram_address()),
            "chip_id: [" + std::to_string(get_logical_location().chip) + "]",
            "core_coordinates: " + flatten_locations(get_logical_location()),
            "dram_prefetch_incoming_noc_id: " + std::to_string(get_dram_prefetch_incoming_noc_id()),
            "prefetch_type: " + std::to_string((int)get_prefetch_type()),
            "embedding_table: " + std::to_string(is_embedding_table() ? 1 : 0),
            "embedding_table_core_c_div: " + std::to_string(get_embedding_table_core_c_div()),
            "embedding_table_row_size_per_core: " + std::to_string(get_embedding_table_row_size_per_core()),
            "embedding_index: " + std::to_string(is_embedding_index() ? 1 : 0),
            "embedding_indices_per_tile: " + std::to_string(get_embedding_indices_per_tile()),
            "embedding_indices_per_input: " + std::to_string(get_embedding_indices_per_input()),
            "hw_tilize: " + std::to_string(is_hw_tilize() ? 1 : 0),
            "tilize_mblock_n_loop_num_rows: " + std::to_string(get_tilize_mblock_n_loop_num_rows()),
            "tilize_row_col_offset: " + std::to_string(get_tilize_row_col_offset()),
            "is_padding: " + std::to_string(is_padding() ? 1 : 0),
            "use_ethernet_fw_stream: " + std::to_string(get_use_ethernet_fw_stream() ? 1 : 0),
            "overlay_blob_size: " + std::to_string(get_overlay_blob_size()),
            "is_post_tm_relay_buf: " + std::to_string(is_post_tm_relay_buf() ? 1 : 0)};
    }

    std::vector<std::string> to_json_list_of_strings_id_only()
    {
        return {"buffer_" + std::to_string(get_id()) + ":", "uniqid: " + std::to_string(get_id())};
    }

private:
    std::string convert_buffer_type_to_string(const BufferType& type)
    {
        switch (type)
        {
            case BufferType::kDramProlog:
                return "dram_prolog";
            case BufferType::kDramEpilog:
                return "dram_epilog";
            case BufferType::kGradientOp:
                return "gradient_op";
            case BufferType::kIntermediate:
                return "intermediate";
            case BufferType::kPacker:
                return "packer";
            case BufferType::kUnpacker:
                return "unpacker";
            case BufferType::kDramIO:
                return "dram_io";
            case BufferType::kRelay:
                return "relay";
            case BufferType::kEthernetRelay:
                return "ethernet_relay";
            case BufferType::kPrologRelay:
                return "prolog_relay";
            default:
                return "unknown";
        }
    }

    static std::string flatten_locations(const tt_cxy_pair& locations)
    {
        std::ostringstream oss;
        oss << "[";
        oss << std::to_string(locations.y) << ", ";
        oss << std::to_string(locations.x);
        oss << "]";
        return oss.str();
    }
};

}  // namespace pipegen2