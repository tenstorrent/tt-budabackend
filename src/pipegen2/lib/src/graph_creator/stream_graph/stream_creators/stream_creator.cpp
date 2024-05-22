// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_creators/stream_creator.h"

#include <algorithm>
#include <numeric>

// clang-format off
#include "utils/logger.hpp"

#include "model/rational_graph/nodes/base_intermed_node.h"
#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/dram_output_node.h"
#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/shared_packer_intermed_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_constants.h"
// clang-format on

namespace pipegen2
{
void StreamCreator::configure_dram_relay_stream(
    const RGBasePipe* rg_pipe,
    const DataFlowInfo& data_flow_info,
    const NcriscConfig& base_ncrisc_config,
    const bool is_dram_prefetch_post_tm,
    StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    log_assert(
        base_ncrisc_config.dram_buf_read_chunk_size_tiles.has_value(), "NCRISC config was not properly configured");

    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_dram_buf_noc_addr(base_ncrisc_config.dram_buf_noc_addr);
    base_stream_config.set_dram_input(true);
    if (base_ncrisc_config.dram_io.value_or(false))
    {
        base_stream_config.set_dram_io(true);
    }
    else if (base_ncrisc_config.dram_streaming.value_or(false))
    {
        base_stream_config.set_dram_streaming(true);
    }
    if (base_ncrisc_config.dram_ram.value_or(false))
    {
        base_stream_config.set_dram_ram(true);
    }
    base_stream_config.set_incoming_data_noc(rg_pipe->get_incoming_noc_id());
    base_stream_config.set_msg_size(base_ncrisc_config.msg_size);
    base_stream_config.set_num_scatter_inner_loop(rg_pipe->get_periodic_repeat());
    base_stream_config.set_outgoing_data_noc(rg_pipe->get_outgoing_noc_id());
    base_stream_config.set_pipe_id(rg_pipe->get_id());
    // Pipe scattering from DRAM is not supported.
    base_stream_config.set_pipe_scatter_output_loop_count(1);
    // This field will be computed later in StreamGraphCreator.
    base_stream_config.set_ptrs_not_zero(false);
    base_stream_config.set_remote_receiver(true);
    base_stream_config.set_resend(is_dram_prefetch_post_tm);
    base_stream_config.set_source_endpoint(true);
    base_stream_config.set_vc(rg_pipe->get_outgoing_noc_vc());

    const unsigned int dram_buffer_size_bytes =
        is_dram_prefetch_post_tm
            ? get_prefetch_post_tm_dram_buffer_size_bytes(rg_pipe, data_flow_info, base_ncrisc_config)
            : base_ncrisc_config.dram_buf_read_chunk_size_tiles.value() * base_ncrisc_config.msg_size;

    base_stream_config.set_buffer_size(dram_buffer_size_bytes);
}

void StreamCreator::configure_noc_to_unpacker_stream(const UnpackerOutputNode* unpacker_node, StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_buf_id(unpacker_node->get_id());
    base_stream_config.set_buf_space_available_ack_thr(calculate_buf_space_available_ack_thr(unpacker_node));
    base_stream_config.set_buffer_size(unpacker_node->get_tile_size() * unpacker_node->get_size_tiles());
    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_input_index(unpacker_node->get_kernel_input_index());
    base_stream_config.set_msg_size(unpacker_node->get_tile_size());
    base_stream_config.set_remote_source(true);
    base_stream_config.set_tile_clear_granularity(unpacker_node->get_transfer_granularity());
    base_stream_config.set_overlay_blob_extra_size(unpacker_node->get_overlay_blob_extra_size());

    configure_unpacker_stream_receiver_params(base_stream_config);

    check_and_convert_unpacker_to_embedding_index_stream(stream, unpacker_node);

    stream->set_op_name(unpacker_node->get_op_name());
    stream->set_operand_id(unpacker_node->get_operand_id());
}

unsigned int StreamCreator::calculate_buf_space_available_ack_thr(const UnpackerOutputNode* node)
{
    return calculate_buf_space_available_ack_thr(node->get_size_tiles(), node->get_scatter_gather_num_tiles());
}

unsigned int StreamCreator::calculate_buf_space_available_ack_thr(const BaseIntermedNode* node)
{
    return calculate_buf_space_available_ack_thr(node->get_size_tiles(), node->get_scatter_gather_num_tiles());
}

unsigned int StreamCreator::calculate_buf_space_available_ack_thr(
    unsigned int size_tiles, unsigned int scatter_gather_num_tiles)
{
    switch (size_tiles / scatter_gather_num_tiles)
    {
        case 0:
            return 0;
        case 1:
        case 2:
            return 1;
        case 3:
        case 4:
        case 5:
            return 2;
        default:
            return 3;
    }
}

bool StreamCreator::can_configure_dram_to_unpacker_stream(
    const RGBasePipe* rg_pipe,
    const NcriscConfig& base_ncrisc_config,
    const UnpackerOutputNode* unpacker_node,
    unsigned int max_dram_input_buffer_size_tiles)
{
    if (rg_pipe->get_physical_location() != unpacker_node->get_physical_location())
    {
        // Cannot configure single stream if they are not on the same core.
        return false;
    }

    const unsigned int buffer_size = calculate_dram_to_unpacker_buffer_size(base_ncrisc_config, unpacker_node);

    // If required buffer size does not fall under threshold, we can't merge them.
    // TODO: Add separate threshold value for ethernet pipes.
    // TODO: Test if we can just check if buffer size is larger than the treshold,
    //       not sure why we also have to check if it is larger than the unpacker buffer size.
    const unsigned int unpacker_buffer_size = unpacker_node->get_tile_size() * unpacker_node->get_size_tiles();

    const unsigned int pipe_reduce_mem_usage_threshold = calculate_pipe_reduce_mem_usage_threshold(
        max_dram_input_buffer_size_tiles, rg_pipe->is_ethernet_pipe(), unpacker_node->get_tile_size());

    return buffer_size <= std::max(pipe_reduce_mem_usage_threshold, unpacker_buffer_size);
}

unsigned int StreamCreator::calculate_dram_to_unpacker_buffer_size(
    const NcriscConfig& base_ncrisc_config, const UnpackerOutputNode* unpacker_node)
{
    // Buffer size has to be divisible by both the chunk size of reading from dram and the buffer size necessary by
    // the unpacker.
    const unsigned int dram_read_chunk_size =
        base_ncrisc_config.dram_buf_read_chunk_size_tiles.value() * base_ncrisc_config.msg_size;

    const unsigned int unpacker_buffer_size = unpacker_node->get_tile_size() * unpacker_node->get_size_tiles();

    return std::lcm(dram_read_chunk_size, unpacker_buffer_size);
}

unsigned int StreamCreator::calculate_pipe_reduce_mem_usage_threshold(
    unsigned int max_dram_input_buffer_size_tiles, bool is_ethernet_core, unsigned int tile_size)
{
    if (max_dram_input_buffer_size_tiles > 0)
    {
        return max_dram_input_buffer_size_tiles * tile_size;
    }

    return is_ethernet_core ? constants::eth_pipe_reduce_mem_usage_threshold_bytes
                            : constants::pipe_reduce_mem_usage_threshold_bytes;
}

void StreamCreator::configure_dram_to_unpacker_prefetch_post_tm_stream(
    const RGBasePipe* rg_pipe,
    const DataFlowInfo& data_flow_info,
    const NcriscConfig& base_ncrisc_config,
    const UnpackerOutputNode* unpacker_node,
    StreamNode* stream)
{
    configure_dram_to_unpacker_stream(rg_pipe, base_ncrisc_config, unpacker_node, stream);
    StreamConfig& base_stream_config = stream->get_base_config();
    const unsigned int dram_buffer_size_bytes =
        get_prefetch_post_tm_dram_buffer_size_bytes(rg_pipe, data_flow_info, base_ncrisc_config);
    base_stream_config.set_buffer_size(dram_buffer_size_bytes);
    base_stream_config.set_resend(true);
}

void StreamCreator::configure_dram_to_unpacker_stream(
    const RGBasePipe* rg_pipe,
    const NcriscConfig& base_ncrisc_config,
    const UnpackerOutputNode* unpacker_node,
    StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_buffer_size(calculate_dram_to_unpacker_buffer_size(base_ncrisc_config, unpacker_node));
    base_stream_config.set_buf_space_available_ack_thr(0);
    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_dram_buf_noc_addr(base_ncrisc_config.dram_buf_noc_addr);
    base_stream_config.set_dram_input(true);
    base_stream_config.set_incoming_data_noc(rg_pipe->get_incoming_noc_id());
    base_stream_config.set_input_index(unpacker_node->get_kernel_input_index());
    base_stream_config.set_msg_size(unpacker_node->get_tile_size());
    base_stream_config.set_num_scatter_inner_loop(rg_pipe->get_periodic_repeat());
    base_stream_config.set_pipe_id(rg_pipe->get_id());
    // Pipe scattering from DRAM is not supported.
    base_stream_config.set_pipe_scatter_output_loop_count(1);
    // This field will be computed later in StreamGraphCreator.
    base_stream_config.set_ptrs_not_zero(false);
    // This field will be updated if it is prefetch, as needed.
    base_stream_config.set_resend(false);
    base_stream_config.set_source_endpoint(true);
    base_stream_config.set_tile_clear_granularity(unpacker_node->get_transfer_granularity());
    base_stream_config.set_overlay_blob_extra_size(unpacker_node->get_overlay_blob_extra_size());

    if (base_ncrisc_config.dram_io.value_or(false))
    {
        base_stream_config.set_dram_io(true);
        base_stream_config.set_dram_input_no_push(true);
    }
    else if (base_ncrisc_config.dram_streaming.value_or(false))
    {
        base_stream_config.set_dram_streaming(true);
    }
    if (base_ncrisc_config.dram_ram.value_or(false))
    {
        base_stream_config.set_dram_ram(true);
    }

    configure_unpacker_stream_receiver_params(base_stream_config);

    check_and_convert_unpacker_to_embedding_index_stream(stream, unpacker_node);

    stream->set_op_name(unpacker_node->get_op_name());
    stream->set_operand_id(unpacker_node->get_operand_id());
}

void StreamCreator::set_common_l1_to_noc_stream_properties(
    StreamNode* stream, const RGBasePipe* rg_pipe, const BaseInputNode* input_node)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_msg_size(input_node->get_tile_size());
    base_stream_config.set_remote_receiver(true);
    base_stream_config.set_source_endpoint(true);
    base_stream_config.set_outgoing_data_noc(rg_pipe->get_outgoing_noc_id());
    base_stream_config.set_is_l1_producer(true);

    if (input_node->is_scatter())
    {
        base_stream_config.set_buf_full_size_bytes(
            input_node->get_tile_size() * input_node->get_size_tiles() * input_node->get_num_scatter_chunks());
        base_stream_config.set_is_sending_tiles_out_of_order(true);
        base_stream_config.set_num_mblock_buffering(
            input_node->get_size_tiles() / input_node->get_scatter_gather_num_tiles());
        base_stream_config.set_num_msgs_in_block(
            input_node->get_num_scatter_chunks() * input_node->get_scatter_gather_num_tiles());
        base_stream_config.set_resend(true);
    }
    else
    {
        base_stream_config.set_buf_id(input_node->get_id());
        base_stream_config.set_buffer_size(input_node->get_tile_size() * input_node->get_size_tiles());
    }
}

void StreamCreator::configure_packer_to_noc_stream(
    StreamNode* stream, const RGBasePipe* rg_pipe, const PackerInputNode* packer_node)
{
    set_common_l1_to_noc_stream_properties(stream, rg_pipe, packer_node);

    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_mblock_k(packer_node->get_blocking_params().get_mblock_k());
    base_stream_config.set_mblock_m(packer_node->get_blocking_params().get_mblock_m());
    base_stream_config.set_mblock_n(packer_node->get_blocking_params().get_mblock_n());
    base_stream_config.set_ublock_ct(packer_node->get_blocking_params().get_ublock_ct());
    base_stream_config.set_ublock_rt(packer_node->get_blocking_params().get_ublock_rt());
    base_stream_config.set_output_index(packer_node->get_kernel_output_index());

    if (packer_node->is_sharing_buffer())
    {
        base_stream_config.set_shared_space_buffer_node_id(packer_node->get_shared_space_buffer_node_id());
    }

    if (packer_node->is_scatter())
    {
        // Blobgen needs this attribute to be set.
        base_stream_config.set_padding_scatter_order_size(1);
        base_stream_config.set_is_scatter_pack(true);
    }
    else
    {
        base_stream_config.set_legacy_pack(true);
    }

    stream->set_op_name(packer_node->get_op_name());
}

void StreamCreator::configure_prefetch_pre_tm_relay_stream(
    StreamNode* stream,
    const DramInputNode* prefetch_input_node,
    const RGBasePipe* rg_pipe,
    std::vector<NcriscConfig>&& ncrisc_configs)
{
    set_common_l1_to_noc_stream_properties(stream, rg_pipe, prefetch_input_node);
    stream->set_ncrisc_configs(std::move(ncrisc_configs));

    StreamConfig& base_stream_config = stream->get_base_config();
    const NcriscConfig& base_ncrisc_config = stream->get_base_ncrisc_config();

    base_stream_config.set_dram_buf_noc_addr(base_ncrisc_config.dram_buf_noc_addr);
    base_stream_config.set_incoming_data_noc(rg_pipe->get_incoming_noc_id());
    base_stream_config.set_dram_input(true);
    if (prefetch_input_node->get_is_ram())
    {
        base_stream_config.set_dram_ram(true);
    }
    base_stream_config.set_dram_io(false);

    base_stream_config.set_is_reading_from_padding_table(prefetch_input_node->is_padding());
}

void StreamCreator::update_stream_for_dram_write(
    StreamNode* stream, const bool is_remote_write, const bool is_untilized_output)
{
    StreamConfig& base_stream_config = stream->get_base_config();
    const NcriscConfig& base_ncrisc_config = stream->get_base_ncrisc_config();

    const bool is_scatter_pack = base_stream_config.get_is_scatter_pack().value_or(false);
    const bool is_packer_source = base_stream_config.get_legacy_pack().value_or(false) || is_scatter_pack;
    const bool is_dram_io = base_ncrisc_config.dram_io.value_or(false);
    const bool is_dram_streaming = base_ncrisc_config.dram_streaming.value_or(false);

    base_stream_config.set_dram_buf_noc_addr(base_ncrisc_config.dram_buf_noc_addr);
    base_stream_config.set_dram_output(true);
    base_stream_config.set_dram_ram(base_ncrisc_config.dram_ram.value_or(false));
    base_stream_config.set_dram_writes_with_cmd_buf(is_remote_write || is_untilized_output);

    if (!is_dram_io && !is_dram_streaming)
    {
        return;
    }

    base_stream_config.set_dram_output_no_push(is_packer_source && !is_untilized_output && !is_scatter_pack);
    base_stream_config.set_remote_receiver(false);
    base_stream_config.set_receiver_endpoint(true);

    if (is_dram_io)
    {
        base_stream_config.set_dram_io(true);
    }
    else if (is_dram_streaming)
    {
        base_stream_config.set_dram_streaming(true);
    }
}

void StreamCreator::configure_packer_stream_with_untilization_params(
    const RGBasePipe* rg_pipe, const unsigned int packer_stride, StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    log_assert(rg_pipe->get_output_nodes().size() == 1, "Expecting only a single dram output node");
    const DramOutputNode* dram_output_node = dynamic_cast<const DramOutputNode*>(*rg_pipe->get_output_nodes().begin());
    log_assert(dram_output_node, "Unexpected downcast failure from RGBaseNode to DramOutputNode");
    log_assert(dram_output_node->is_untilized_output(), "Expecting untilized output node");

    base_stream_config.set_batch_dim_size(dram_output_node->get_untilized_output_z_dim());
    base_stream_config.set_c_dim_size(dram_output_node->get_untilized_output_c_dim());
    base_stream_config.set_moves_raw_data(true);
    base_stream_config.set_r_dim_size(dram_output_node->get_untilized_output_r_dim());
    base_stream_config.set_skip_col_bytes(dram_output_node->get_bytes_count_in_untilized_row_of_datums());
    base_stream_config.set_skip_col_row_bytes(dram_output_node->get_bytes_count_in_untilized_output());
    base_stream_config.set_skip_col_tile_row_bytes(dram_output_node->get_bytes_count_in_untilized_row_of_tiles());
    base_stream_config.set_skip_col_zrow_bytes(dram_output_node->get_bytes_count_in_untilized_row_of_cores());
    base_stream_config.set_skip_zcol_bytes(dram_output_node->get_bytes_count_in_untilized_row_for_single_producer());
    base_stream_config.set_stride(packer_stride);
    base_stream_config.set_stride_offset_size_bytes(dram_output_node->get_stride_offset_bytes(packer_stride));
    base_stream_config.set_tile_dim_r(dram_output_node->get_untilized_output_tile_dim_r());
    base_stream_config.set_tile_dim_c(dram_output_node->get_untilized_output_tile_dim_c());
    base_stream_config.set_total_strides(rg_pipe->get_inputs().size());
    // TODO: Document why we don't take z dimension into account anymore.
    base_stream_config.set_zr_dim_size(1);
    base_stream_config.set_zc_dim_size(1);
}

void StreamCreator::configure_relay_stream_with_default_buffer_size(
    StreamNode* relay_stream,
    const unsigned int num_unique_input_nodes,
    const unsigned int tile_size,
    const bool is_ethernet_core)
{
    const unsigned int default_buffer_size_tiles =
        calculate_default_relay_stream_buffer_size_tiles(num_unique_input_nodes, is_ethernet_core);

    configure_relay_stream(relay_stream, default_buffer_size_tiles, tile_size);
}

void StreamCreator::configure_relay_stream(
    StreamNode* relay_stream, const unsigned int buffer_size_tiles, const unsigned int tile_size)
{
    StreamConfig& stream_config = relay_stream->get_base_config();

    stream_config.set_msg_size(tile_size);
    stream_config.set_buffer_size(buffer_size_tiles * tile_size);
    stream_config.set_data_auto_send(true);
    stream_config.set_remote_receiver(true);
    stream_config.set_remote_source(true);
}

void StreamCreator::configure_gather_relay_stream(StreamNode* relay_stream, const RGBasePipe* pipe)
{
    StreamConfig& stream_config = relay_stream->get_base_config();

    const unsigned int buffer_tile_size = pipe->get_inputs().front().get_node()->get_tile_size();

    configure_relay_stream_with_default_buffer_size(
        relay_stream, pipe->get_unique_input_nodes().size(), buffer_tile_size);

    stream_config.opt_set_remote_receiver(std::nullopt);
    stream_config.set_local_receiver(true);
    stream_config.set_local_stream_clear_num(1);
    stream_config.set_pipe_id(pipe->get_id());
}

void StreamCreator::configure_gather_to_pcie_relay_stream(StreamNode* relay_stream, const RGBasePipe* pipe)
{
    const PackerInputNode* packer_input_node =
        dynamic_cast<const PackerInputNode*>(pipe->get_inputs()[0].get_non_virtual_node());
    log_assert(packer_input_node, "Expecting packer input node at the input of gather to PCIe pipe");

    const RGBaseNode* output_node = pipe->get_output_node();

    configure_relay_stream(
        relay_stream, output_node->get_size_tiles() /* buffer_size_tiles */, output_node->get_tile_size());

    if (!packer_input_node->is_scatter())
    {
        relay_stream->get_base_config().set_incoming_data_noc(pipe->get_incoming_noc_id());
    }

    set_common_stream_properties_from_pipe(relay_stream, pipe, false /* incoming_stream */);

    // Always true for gather to PCIe relay.
    relay_stream->get_base_config().set_legacy_pack(true);
}

void StreamCreator::configure_gather_stream(const RGBasePipe* pipe, StreamNode* gather_stream)
{
    StreamConfig& stream_config = gather_stream->get_base_config();

    const unsigned int buffer_tile_size = pipe->get_inputs().at(0).get_node()->get_tile_size();
    stream_config.set_buf_addr(0);
    stream_config.set_buffer_size(0);
    stream_config.set_remote_receiver(true);
    stream_config.set_local_sources_connected(true);
    stream_config.set_local_stream_clear_num(1);
    stream_config.set_next_phase_src_change(true);
    stream_config.set_arb_group_size(1);
    stream_config.set_msg_size(buffer_tile_size);
    stream_config.set_reg_update_vc(3);
    stream_config.set_data_auto_send(true);

    // Blobgen needs this attribute to be set.
    stream_config.set_padding_scatter_order_size(1);
}

void StreamCreator::configure_multicast_stream(const RGBasePipe* rg_pipe, StreamNode* multicast_stream)
{
    StreamConfig& base_stream_config = multicast_stream->get_base_config();

    const unsigned int buffer_tile_size = rg_pipe->get_inputs().at(0).get_node()->get_tile_size();
    // Multicast streams who send input from a single buffer (scattered or non scattered) use the same
    // buffer size calculation as the gather relay streams.
    const unsigned int buffer_size_tiles =
        calculate_default_relay_stream_buffer_size_tiles(1 /* num_unique_input_nodes */);

    base_stream_config.set_buffer_size(buffer_size_tiles * buffer_tile_size);
    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_linked(true);
    base_stream_config.set_msg_size(buffer_tile_size);
    base_stream_config.set_remote_receiver(true);
    base_stream_config.set_remote_source(true);
}

void StreamCreator::convert_stream_to_multicast(const bool is_direct_packer_multicast, StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_linked(true);

    if (is_direct_packer_multicast)
    {
        base_stream_config.set_has_packer_mcast_opt(true);
        stream->set_stream_type(StreamType::PackerMulticast);
    }
    else
    {
        stream->set_stream_type(StreamType::Multicast);
    }
}

void StreamCreator::configure_dram_multicast_stream(
    const RGBasePipe* rg_pipe,
    const bool is_dram_prefetch,
    const NcriscConfig& base_ncrisc_config,
    StreamNode* multicast_stream)
{
    StreamConfig& base_stream_config = multicast_stream->get_base_config();

    base_stream_config.set_buffer_size(
        base_ncrisc_config.dram_buf_read_chunk_size_tiles.value() * base_ncrisc_config.msg_size);
    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_dram_buf_noc_addr(base_ncrisc_config.dram_buf_noc_addr);
    base_stream_config.set_dram_input(true);
    if (base_ncrisc_config.dram_io.value_or(false))
    {
        base_stream_config.set_dram_io(true);
    }
    else if (base_ncrisc_config.dram_streaming.value_or(false))
    {
        base_stream_config.set_dram_streaming(true);
    }
    if (base_ncrisc_config.dram_ram.value_or(false))
    {
        base_stream_config.set_dram_ram(true);
    }
    base_stream_config.set_incoming_data_noc(rg_pipe->get_incoming_noc_id());
    base_stream_config.set_linked(true);
    base_stream_config.set_msg_size(base_ncrisc_config.msg_size);
    // This field will be computed later in StreamGraphCreator.
    base_stream_config.set_ptrs_not_zero(false);
    base_stream_config.set_remote_receiver(true);
    base_stream_config.set_resend(is_dram_prefetch);
    base_stream_config.set_source_endpoint(true);
    base_stream_config.set_num_scatter_inner_loop(rg_pipe->get_periodic_repeat());
}

void StreamCreator::configure_address_offsets(
    StreamNode* stream, const RGBasePipe* pipe, const BaseInputNode* input_node)
{
    // TODO: Check if we can assign these offsets during dataflow calculation when we create phases info.
    std::vector<unsigned int> non_adjacent_offsets;

    const unsigned int adjacent_offsets_distance = input_node->get_scatter_gather_num_tiles();
    const std::vector<PipeInput>& pipe_inputs = pipe->get_inputs();

    std::vector<std::size_t> node_pipe_input_offset_indexes;
    for (std::size_t i = 0; i < pipe_inputs.size(); ++i)
    {
        if (pipe_inputs.at(i).get_node() == input_node)
        {
            node_pipe_input_offset_indexes.push_back(i);
        }
    }

    log_assert(!node_pipe_input_offset_indexes.empty(), "Expecting pipe node input to have at least one input offset");

    non_adjacent_offsets.push_back(pipe_inputs.at(node_pipe_input_offset_indexes.at(0)).get_offset());
    for (std::size_t i = 1; i < node_pipe_input_offset_indexes.size(); ++i)
    {
        const unsigned int current_offset = pipe_inputs.at(node_pipe_input_offset_indexes.at(i)).get_offset();
        const unsigned int prev_offset = pipe_inputs.at(node_pipe_input_offset_indexes.at(i - 1)).get_offset();

        const bool is_offset_adjacent = (current_offset == prev_offset + adjacent_offsets_distance);
        const bool are_indexes_adjacent =
            (node_pipe_input_offset_indexes.at(i) == node_pipe_input_offset_indexes.at(i - 1) + 1);

        if (!is_offset_adjacent || !are_indexes_adjacent)
        {
            non_adjacent_offsets.push_back(current_offset);
        }
    }

    log_assert(
        stream->get_phase_configs().size() % non_adjacent_offsets.size() == 0,
        "Number of phases isn't divisible by the number of non-adjacent scattered inputs");

    const unsigned int tile_size = stream->get_base_config().get_msg_size().value();

    for (std::size_t i = 0; i < stream->get_phase_configs().size(); ++i)
    {
        PhaseConfig& phase_config = stream->get_phase_configs()[i];
        // Setting address only to the offset, and later after we assign memory resources it will be incremented
        // by the buffer address in L1.
        unsigned int input_offset_in_tiles = non_adjacent_offsets[i % non_adjacent_offsets.size()];
        phase_config.config.set_buf_addr(input_offset_in_tiles * tile_size);
    }
}

std::vector<PhaseInfo> StreamCreator::decompose_phases(
    const std::vector<PhaseInfo>& phase_info_vec, unsigned int decomposed_phase_max_size)
{
    std::vector<PhaseInfo> decomposed_phases;

    // When decomposing, we start from the phase offset of the first given phase. Every phase after will have an
    // offset that is incremented by 1 from the previous phase. This is fine since we only decompose phases that are
    // associated with reading from dram (no source streams).
    unsigned int start_phase_offset = phase_info_vec[0].phase_offset;
    for (const PhaseInfo& phase_info : phase_info_vec)
    {
        if (decomposed_phase_max_size >= phase_info.num_msgs)
        {
            decomposed_phases.emplace_back(
                start_phase_offset + decomposed_phases.size(), phase_info.data_offset, phase_info.num_msgs);
            continue;
        }

        unsigned int decomposed_phase_size =
            std::gcd(decomposed_phase_max_size, phase_info.num_msgs - decomposed_phase_max_size);

        log_assert(
            phase_info.num_msgs % decomposed_phase_size == 0,
            "Expecting phase num_msgs to be divisible by decomposing size");
        const unsigned int decomposed_phases_count = phase_info.num_msgs / decomposed_phase_size;
        for (unsigned int i = 0; i < decomposed_phases_count; ++i)
        {
            decomposed_phases.emplace_back(
                start_phase_offset + decomposed_phases.size(),
                phase_info.data_offset + decomposed_phase_size * i,
                decomposed_phase_size);
        }
    }

    return decomposed_phases;
}

std::vector<PhaseInfo> StreamCreator::merge_phases(
    const std::vector<PhaseInfo>& phase_info_vec, unsigned int max_msgs_in_phase)
{
    std::vector<PhaseInfo> merged_phases{phase_info_vec[0]};

    for (std::size_t i = 1; i < phase_info_vec.size(); ++i)
    {
        const PhaseInfo& current_phase = phase_info_vec[i];
        if (merged_phases.back().num_msgs + current_phase.num_msgs <= max_msgs_in_phase)
        {
            merged_phases.back().num_msgs += current_phase.num_msgs;
        }
        else
        {
            merged_phases.emplace_back(current_phase);
        }
    }

    return merged_phases;
}

void StreamCreator::configure_stream_src_and_dest(
    StreamNode* stream, StreamNode* source_stream, StreamNode* destination_stream)
{
    for (PhaseConfig& phase_config : stream->get_phase_configs())
    {
        if (source_stream)
        {
            phase_config.config.set_source(source_stream);
        }
        if (destination_stream)
        {
            phase_config.config.set_dest({destination_stream});
        }
    }
}

void StreamCreator::configure_stream_src_and_dest(
    StreamNode* stream, StreamNode* source_stream, const std::vector<std::unique_ptr<StreamNode>>& destination_streams)
{
    std::vector<StreamNode*> stream_dest;
    for (const std::unique_ptr<StreamNode>& dest_stream : destination_streams)
    {
        stream_dest.push_back(dest_stream.get());
    }

    for (PhaseConfig& phase_config : stream->get_phase_configs())
    {
        if (source_stream)
        {
            phase_config.config.set_source(source_stream);
        }
        if (!stream_dest.empty())
        {
            phase_config.config.set_dest(stream_dest);
        }
    }
}

void StreamCreator::configure_multicast_streams_src_and_dest(
    StreamNode* multicast_stream, const std::vector<std::unique_ptr<StreamNode>>& unpacker_streams)
{
    // Source of multicast stream is already configured by connecting input stream to the multicast stream.
    configure_stream_src_and_dest(multicast_stream, nullptr /* source_stream */, unpacker_streams);
    multicast_stream->get_base_config().set_num_mcast_dests(unpacker_streams.size());

    for (const std::unique_ptr<StreamNode>& unpacker_stream : unpacker_streams)
    {
        configure_stream_src_and_dest(unpacker_stream.get(), multicast_stream, nullptr /* destination_stream */);
    }

    // We need sorted order of unpacker streams by their location. This is needed to set 'src_dest_index' in right
    // order. We don't want to copy all unpacker streams to a new vector. We can just sort the vector of pointers
    // to unpacker streams.
    std::vector<StreamNode*> unpacker_streams_sorted_by_location;
    for (const std::unique_ptr<StreamNode>& unpacker_stream : unpacker_streams)
    {
        unpacker_streams_sorted_by_location.push_back(unpacker_stream.get());
    }
    std::sort(
        unpacker_streams_sorted_by_location.begin(),
        unpacker_streams_sorted_by_location.end(),
        [](const StreamNode* lhs, const StreamNode* rhs)
        { return lhs->get_physical_location() < rhs->get_physical_location(); });
    for (std::size_t i = 0; i < unpacker_streams_sorted_by_location.size(); ++i)
    {
        unpacker_streams_sorted_by_location[i]->get_base_config().set_src_dest_index(i);
        unpacker_streams_sorted_by_location[i]->get_base_config().set_remote_src_is_mcast(true);
    }
}

unsigned int StreamCreator::calculate_default_relay_stream_buffer_size_tiles(
    const unsigned int num_unique_input_nodes, const bool is_ethernet_core)
{
    const unsigned int combined_min_tile_count =
        is_ethernet_core ? c_eth_sum_gather_input_min_size_tiles : c_sum_gather_input_min_size_tiles;

    unsigned int per_input_tile_count = is_ethernet_core ? c_eth_gather_streamed_read_input_buffer_num_tiles
                                                         : c_worker_gather_streamed_read_input_buffer_num_tiles;

    // Heuristic for scaling relay stream's buffer size, taken from pipegen1.
    if (per_input_tile_count * num_unique_input_nodes < combined_min_tile_count)
    {
        per_input_tile_count = combined_min_tile_count / num_unique_input_nodes;
    }

    return per_input_tile_count;
}

bool StreamCreator::is_packer_stream_sending_to_pipe_output_noc(
    const RGBasePipe* rg_pipe, const PackerInputNode* packer_node)
{
    const bool is_input_to_unicast_pipe = rg_pipe->get_unique_input_nodes().size() == 1;
    return packer_node->is_scatter() && is_input_to_unicast_pipe;
}

void StreamCreator::configure_phases_of_input_stream(
    const StreamPhasesCommonConfig& input_stream_static_config,
    const RGBaseNode* input_node,
    const RGBasePipe* rg_pipe,
    std::vector<StreamNode*> receiving_streams,
    const DataFlowInfo& data_flow_info)
{
    StreamNode* input_stream_node = input_stream_static_config.get_stream_node();
    log_assert(input_stream_node, "Input stream node for the pipe not found");
    const RGBaseNode* first_output_node = rg_pipe->get_output_nodes().at(0);

    // Stream config that is common for all phases of the input stream towards the given pipe.
    StreamConfig input_stream_common_phase_config = input_stream_static_config.get_common_config();
    const StreamConfig& input_stream_base_config = input_stream_node->get_base_config();

    const std::vector<PhaseInfo>& sender_phases = data_flow_info.get_edge_phases(input_node, rg_pipe);
    const PhaseId first_phase_id = sender_phases.front().phase_offset;

    auto first_phase_iter = find_phase_by_phase_id(input_stream_node, first_phase_id);
    if (first_phase_iter != input_stream_node->get_phase_configs().end() &&
        first_phase_iter->phase_id == first_phase_id)
    {
        // Phases of the input stream are already configured, so we just need to connect it to receiving streams.
        if (!receiving_streams.empty())
        {
            const std::vector<PhaseInfo>& receiver_phases = data_flow_info.get_edge_phases(rg_pipe, first_output_node);
            auto receiving_phase_iter = receiver_phases.begin();
            for (PhaseConfig& phase_config : input_stream_node->get_phase_configs())
            {
                receiving_phase_iter = connect_input_stream_phase_to_receiving_stream(
                    input_stream_node,
                    phase_config.phase_id,
                    phase_config.config,
                    receiver_phases,
                    receiving_phase_iter,
                    receiving_streams);
            }
        }

        set_common_stream_properties_from_pipe(input_stream_node, rg_pipe);
        configure_scatter_loops(rg_pipe, input_stream_node);
        return;
    }

    const std::vector<PhaseInfo>& receiver_phases = data_flow_info.get_edge_phases(rg_pipe, first_output_node);
    log_assert(!sender_phases.empty(), "Expecting at least one phase for the input stream");
    log_assert(!receiver_phases.empty(), "Expecting at least one phase for the receiving streams");

    auto receiving_phase_iter = receiver_phases.begin();
    std::vector<PhaseConfig> sender_phase_configs;
    for (const PhaseInfo& phase_info : sender_phases)
    {
        StreamConfig phase_config = input_stream_common_phase_config;
        phase_config.set_num_msgs(phase_info.num_msgs);

        if (!receiving_streams.empty())
        {
            receiving_phase_iter = connect_input_stream_phase_to_receiving_stream(
                input_stream_node,
                phase_info.phase_offset,
                phase_config,
                receiver_phases,
                receiving_phase_iter,
                receiving_streams);
        }
        if (input_stream_node->get_base_config().get_is_sending_tiles_out_of_order().value_or(false))
        {
            phase_config.set_buf_addr(phase_info.data_offset * input_stream_base_config.get_msg_size().value());
            phase_config.set_buffer_size(phase_info.num_msgs * input_stream_base_config.get_msg_size().value());
        }
        sender_phase_configs.emplace_back(phase_info.phase_offset, std::move(phase_config));
    }

    input_stream_node->add_phase_configs(
        std::move(sender_phase_configs), data_flow_info.get_max_num_tiles_per_phase(first_output_node));
    set_common_stream_properties_from_pipe(input_stream_node, rg_pipe);
    configure_scatter_loops(rg_pipe, input_stream_node);
    update_stream_iterations_in_epoch(input_stream_node, rg_pipe, data_flow_info);

    // We must sort the phases as they may be out of order due to pipe processing order.
    input_stream_node->sort_phases();

    first_phase_iter = find_phase_by_phase_id(input_stream_node, first_phase_id);
    log_assert(
        first_phase_iter != input_stream_node->get_phase_configs().end(),
        "Expecting phase id to be present in the stream");
    update_resend_of_sending_stream(input_stream_node, first_phase_iter, sender_phases.size());
}

std::vector<PhaseInfo>::const_iterator StreamCreator::connect_input_stream_phase_to_receiving_stream(
    StreamNode* input_stream,
    const unsigned int input_stream_phase_offset,
    StreamConfig& input_stream_phase_config,
    const std::vector<PhaseInfo>& receiver_phases,
    std::vector<PhaseInfo>::const_iterator receiver_phase_start_iter,
    const std::vector<StreamNode*>& receiving_streams)
{
    auto receiving_phase_iter = std::upper_bound(
        receiver_phase_start_iter,
        receiver_phases.end(),
        input_stream_phase_offset,
        [](const unsigned int phase_offset, const PhaseInfo& phase_info)
        { return phase_info.phase_offset > phase_offset; });
    log_assert(receiving_phase_iter != receiver_phases.begin(), "Expecting at least one receiving phase to exist");
    --receiving_phase_iter;

    uint32_t receiving_stream_idx =
        std::distance(receiver_phases.begin(), receiving_phase_iter) % receiving_streams.size();
    log_assert(receiving_stream_idx < receiving_streams.size(), "Expecting receiving stream index to be valid");

    uint32_t receiving_stream_phase_idx =
        std::distance(receiver_phases.begin(), receiving_phase_iter) / receiving_streams.size();
    log_assert(
        receiving_stream_phase_idx < receiving_streams[receiving_stream_idx]->get_num_phases(),
        "Expecting receiving stream phase index to be valid");

    StreamNode* receiving_stream = receiving_streams[receiving_stream_idx];
    receiving_stream->get_phase_config(receiving_stream_phase_idx).set_source(input_stream);
    input_stream_phase_config.set_dest({receiving_stream});

    return receiving_phase_iter;
}

void StreamCreator::update_stream_iterations_in_epoch(
    StreamNode* stream, const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    stream->get_base_config().set_num_iters_in_epoch(data_flow_info.get_num_iterations_in_epoch(pipe));
}

void StreamCreator::update_resend_of_sending_stream(
    StreamNode* stream, std::vector<PhaseConfig>::iterator starting_phase_iter, int num_phases_to_update)
{
    // If the sending stream is not a scatter pack, we don't need to set resend flag.
    if (!stream->get_base_config().get_is_scatter_pack().value_or(false))
    {
        return;
    }

    // Otherwise, we need to set resend for all the phases except the first one.
    starting_phase_iter->config.set_resend(false);

    for (auto phase_iter = starting_phase_iter + 1;
         phase_iter < starting_phase_iter + num_phases_to_update && phase_iter != stream->get_phase_configs().end();
         ++phase_iter)
    {
        phase_iter->config.set_resend(true);
    }
}

std::vector<PhaseConfig>::iterator StreamCreator::find_phase_by_phase_id(StreamNode* stream, const PhaseId phase_id)
{
    auto phase_iter = std::lower_bound(
        stream->get_phase_configs().begin(),
        stream->get_phase_configs().end(),
        phase_id,
        [](const PhaseConfig& phase_config, const PhaseId phase_id) { return phase_config.phase_id < phase_id; });
    return phase_iter;
}

void StreamCreator::set_common_stream_properties_from_pipe(
    StreamNode* stream, const RGBasePipe* rg_pipe, const bool incoming_stream)
{
    StreamConfig& base_config = stream->get_base_config();

    if (incoming_stream)
    {
        base_config.set_vc(rg_pipe->get_incoming_noc_vc());
        base_config.set_outgoing_data_noc(rg_pipe->get_incoming_noc_id());
    }
    else
    {
        base_config.set_outgoing_data_noc(rg_pipe->get_outgoing_noc_id());
        base_config.set_vc(rg_pipe->get_outgoing_noc_vc());
    }
    base_config.set_pipe_id(rg_pipe->get_id());
}

void StreamCreator::configure_scatter_loops(const RGBasePipe* rg_pipe, StreamNode* stream)
{
    StreamConfig& base_config = stream->get_base_config();

    const bool is_l1_scatter_producer = base_config.get_is_l1_producer().value_or(false) &&
                                        base_config.get_is_sending_tiles_out_of_order().value_or(false);

    if (is_l1_scatter_producer)
    {
        base_config.set_num_scatter_inner_loop(rg_pipe->get_periodic_repeat());
        base_config.set_pipe_scatter_output_loop_count(rg_pipe->get_consumer_repeat());
    }
}

void StreamCreator::configure_scatter_order_sizes(const SerialForkPipe* serial_fork_pipe, StreamNode* stream_node)
{
    StreamConfig& base_config = stream_node->get_base_config();

    base_config.set_pipe_id(serial_fork_pipe->get_id());
    base_config.set_scatter_order_size(serial_fork_pipe->get_num_serial_outputs());
    base_config.set_padding_scatter_order_size(serial_fork_pipe->get_num_serial_non_padding_outputs());
}

void StreamCreator::convert_packer_to_intermed_stream(
    StreamNode* stream_node, const SharedPackerIntermedNode* intermed_node)
{
    stream_node->set_stream_type(StreamType::Intermed);
    stream_node->set_physical_location(intermed_node->get_physical_location());
    // This stream is currently tied to packer operand with which this intermed buffer is associated. Thus we set
    // this attribute to packer operand id as a part of reconfiguring this stream.
    stream_node->get_base_config().set_space_shared_with_operand(stream_node->get_operand_id());
    stream_node->set_operand_id(intermed_node->get_operand_id());
}

void StreamCreator::configure_default_intermed_stream_properties(StreamNode* stream)
{
    StreamConfig& base_stream_config = stream->get_base_config();
    base_stream_config.set_num_iters_in_epoch(1);
    base_stream_config.set_legacy_pack(true);
    base_stream_config.set_data_auto_send(true);
    base_stream_config.set_buffer_intermediate(true);
    base_stream_config.set_source_endpoint(true);
    base_stream_config.set_reg_update_vc(3);
    base_stream_config.opt_set_num_fork_streams(0);

    configure_unpacker_stream_receiver_params(base_stream_config);

    // Following attrs are nullopted since they are not needed for intermed stream.
    base_stream_config.opt_set_fork_streams(std::nullopt);
    base_stream_config.opt_set_remote_receiver(std::nullopt);
    base_stream_config.opt_set_buf_full_size_bytes(std::nullopt);
    base_stream_config.opt_set_is_scatter_pack(std::nullopt);
    base_stream_config.opt_set_num_mblock_buffering(std::nullopt);
    base_stream_config.opt_set_num_msgs_in_block(std::nullopt);
    base_stream_config.opt_set_resend(std::nullopt);
    base_stream_config.opt_set_incoming_data_noc(std::nullopt);
    base_stream_config.opt_set_outgoing_data_noc(std::nullopt);
}

void StreamCreator::configure_default_dram_output_intermed_stream_properties(StreamNode* stream)
{
    configure_default_intermed_stream_properties(stream);
    StreamConfig& base_stream_config = stream->get_base_config();
    base_stream_config.set_dram_buf_noc_addr(stream->get_base_ncrisc_config().dram_buf_noc_addr);
    base_stream_config.set_dram_input(true);
    base_stream_config.set_dram_output(true);
    base_stream_config.set_dram_io(false);
    base_stream_config.set_dram_ram(true);
    base_stream_config.set_incoming_data_noc(NOC_ROUTE::NOC0);
    base_stream_config.set_outgoing_data_noc(NOC_ROUTE::NOC0);
    base_stream_config.set_vc(0);
}

void StreamCreator::configure_intermed_stream_properties_from_node(
    StreamNode* stream, const BaseIntermedNode* intermed_node)
{
    StreamConfig& base_stream_config = stream->get_base_config();

    base_stream_config.set_buf_id(intermed_node->get_id());
    base_stream_config.set_buffer_size(intermed_node->get_tile_size() * intermed_node->get_size_tiles());
    base_stream_config.set_msg_size(intermed_node->get_tile_size());
    base_stream_config.set_mblock_k(intermed_node->get_blocking_params().get_mblock_k());
    base_stream_config.set_mblock_m(intermed_node->get_blocking_params().get_mblock_m());
    base_stream_config.set_mblock_n(intermed_node->get_blocking_params().get_mblock_n());
    base_stream_config.set_ublock_ct(intermed_node->get_blocking_params().get_ublock_ct());
    base_stream_config.set_ublock_rt(intermed_node->get_blocking_params().get_ublock_rt());
    base_stream_config.set_input_index(intermed_node->get_kernel_input_index());
    base_stream_config.set_output_index(intermed_node->get_kernel_output_index());
    base_stream_config.set_buf_space_available_ack_thr(calculate_buf_space_available_ack_thr(intermed_node));
}

void StreamCreator::check_and_convert_unpacker_to_embedding_index_stream(
    StreamNode* stream_node, const UnpackerOutputNode* unpacker_node)
{
    if (!unpacker_node->is_embedding_index_kernel_input())
    {
        return;
    }

    // TODO: create a new stream type for this. For now, we use the same stream type as the unpacker.
    StreamConfig& base_config = stream_node->get_base_config();
    base_config.set_ncrisc_clear(true);
    base_config.set_receiver_endpoint(true);
    base_config.opt_set_local_receiver(std::nullopt);
    base_config.opt_set_local_receiver_tile_clearing(std::nullopt);
    base_config.opt_set_dram_input_no_push(std::nullopt);
}

unsigned int StreamCreator::get_prefetch_post_tm_dram_buffer_size_bytes(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info, const NcriscConfig& ncrisc_config) const
{
    const unsigned int total_num_msgs = data_flow_info.get_tiles_to_send(pipe->get_first_dram_input_node()) *
                                        ncrisc_config.dram_scatter_offsets_full_size.value();
    return total_num_msgs * ncrisc_config.msg_size;
}
}  // namespace pipegen2