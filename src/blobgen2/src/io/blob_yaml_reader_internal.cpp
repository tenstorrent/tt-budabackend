// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_yaml_reader_internal.h"

#include <iostream> //TODO: Remove the iostream include when read_blob_yaml gets really implemented.
#include <optional>
#include <sstream>
#include <vector>

#include "yaml-cpp/yaml.h"

// In pipegen2.
#include "model/stream_graph/endpoint_stream_destination.h"
#include "model/stream_graph/endpoint_stream_source.h"
#include "model/stream_graph/local_stream_destination.h"
#include "model/stream_graph/local_stream_source.h"
#include "model/stream_graph/receiving_stream_node.h"
#include "model/stream_graph/remote_stream_destination.h"
#include "model/stream_graph/remote_stream_source.h"

namespace blobgen2::internal
{

    void read_phases(const YAML::Node& blob_yaml,
                     std::unordered_map<std::string, pipegen2::BaseStreamNode*>& streams_cache,
                     pipegen2::StreamGraph *const stream_graph)
    {
        for (YAML::const_iterator blob_yaml_it=blob_yaml.begin(); blob_yaml_it!=blob_yaml.end(); ++blob_yaml_it)
        {
            const std::string& blob_yaml_key = blob_yaml_it->first.as<std::string>();

            //TODO: Replate string literal for this and other cases with named constants here and in blob yaml writer.
            const std::string phase_key = "phase_";
            if (blob_yaml_key.rfind(phase_key, 0) == 0)
            {
                const std::string phase_id_str = blob_yaml_key.substr(phase_key.length());
                const YAML::Node& phase_map = blob_yaml_it->second;

                const pipegen2::PhaseId phase_id = get_phase_id(phase_id_str);

                for (YAML::const_iterator phase_it=phase_map.begin(); phase_it!=phase_map.end(); ++phase_it)
                {
                    const std::string& system_wide_stream_id = phase_it->first.as<std::string>();
                    const YAML::Node& stream_map = phase_it->second;

                    const pipegen2::Phase& phase = create_phase(stream_map, phase_id);

                    // Find previously created stream node, is exists, or create a new one.
                    pipegen2::BaseStreamNode* stream;
                    const auto& streams_cache_it = streams_cache.find(system_wide_stream_id);
                    if (streams_cache_it != streams_cache.end())
                    {
                        stream = streams_cache_it->second;
                    }
                    else
                    {
                        std::unique_ptr<pipegen2::BaseStreamNode> stream_uptr = create_stream(stream_map,
                                                                                              system_wide_stream_id);
                        stream = stream_uptr.get();
                        streams_cache[system_wide_stream_id] = stream;
                        stream_graph->add_stream(std::move(stream_uptr));
                    }
                    stream->add_phase(phase);
                }
            }
        }
    }

    void read_dram_blob(const YAML::Node& blob_yaml,
                        std::unordered_map<std::string, pipegen2::BaseStreamNode*>& streams_cache,
                        pipegen2::StreamGraph *const stream_graph)
    {
        for (YAML::const_iterator blob_yaml_it=blob_yaml.begin(); blob_yaml_it!=blob_yaml.end(); ++blob_yaml_it)
        {
            const std::string& blob_yaml_key = blob_yaml_it->first.as<std::string>();

            const std::string dram_blob_key = "dram_blob";
            if (blob_yaml_key == dram_blob_key)
            {
                const YAML::Node& streams_map = blob_yaml_it->second;

                for (YAML::const_iterator stream_it=streams_map.begin(); stream_it!=streams_map.end(); ++stream_it)
                {
                    const std::string& system_wide_stream_id = stream_it->first.as<std::string>();

                    const YAML::Node& buffers_map = stream_it->second;
                    for (YAML::const_iterator buffer_it=buffers_map.begin(); buffer_it!=buffers_map.end(); ++buffer_it)
                    {
                        const unsigned int buffer_id = buffer_it->first.as<unsigned int>();
                        const YAML::Node& dram_config_map = buffer_it->second;

                        const pipegen2::DramConfig& dram_config = create_dram_config(dram_config_map, buffer_id);

                        // Find previously created base stream node, is exists, or assert if not found.
                        pipegen2::BaseStreamNode* stream;
                        const auto& streams_cache_it = streams_cache.find(system_wide_stream_id);
                        if (streams_cache_it != streams_cache.end())
                        {
                            stream = streams_cache_it->second;
                        }
                        else
                        {
                            std::string err_msg = "NCRISC dram configuration for the stream '";
                            err_msg += system_wide_stream_id;
                            err_msg += "' cannot be specified without the stream phase(s) configuration.";
                            throw std::runtime_error(err_msg);
                        }
                        stream->add_dram_config(dram_config);
                    }
                }
            }
        }
    }

    void read_overlay_blob_extra_size(const YAML::Node& blob_yaml, pipegen2::StreamGraph *const stream_graph)
    {
        unsigned int extra_size = 0;

        const YAML::Node& node = blob_yaml["overlay_blob_extra_size"];
        if (node.IsDefined())
        {
            if (node.IsScalar())
            {
                extra_size = node.as<unsigned int>();
            }
            else
            {
                // TODO: Improve error message with string of the node hierarchy that contains the current node.
                std::string err_msg = "Property 'overlay_blob_extra_size' must be a ";
                err_msg += get_node_type_string(YAML::NodeType::Scalar);
                err_msg += ", whereas its type is: '";
                err_msg += get_node_type_string(node.Type());
                err_msg += "' and its value is: '";
                err_msg += node.as<std::string>();
                err_msg += "'.";
                throw std::runtime_error(err_msg);
            }
        }
        stream_graph->set_overlay_blob_extra_size(extra_size);
    }

    std::unique_ptr<pipegen2::BaseStreamNode> create_stream(
        const YAML::Node& stream_map,
        const std::string& system_wide_stream_id)
    {
        std::unique_ptr<pipegen2::BaseStreamSource> source = create_stream_source(stream_map);
        std::unique_ptr<pipegen2::BaseStreamDestination> destination = create_stream_destination(stream_map);

        //TODO: Update logic for the streams that can receive and send.

        // Determine stream node type.
        const YAML::Node dest = stream_map["dest"];
        bool is_dram_output = stream_map["dram_output"].as<bool>(false);
        bool is_dram_input = stream_map["dram_input"].as<bool>(false);

        // A sending stream can either send to a list of destinations or to DRAM.
        bool is_sending_to_another_stream = dest.size() > 0 && !is_dram_output && !is_dram_input;
        bool is_sending_to_dram = dest.size() == 0 && is_dram_output && !is_dram_input;

        // A receiving stream is either having an empty list of destinations or is receiving from DRAM.
        bool is_receiving_from_another_stream = dest.size() == 0 && !is_dram_output && !is_dram_input;
        bool is_receiving_from_dram = dest.size() == 0 && !is_dram_output && is_dram_input;

        bool is_sending_stream = is_sending_to_another_stream || is_sending_to_dram;
        bool is_receiving_stream = is_receiving_from_another_stream || is_receiving_from_dram;

        std::unique_ptr<pipegen2::BaseStreamNode> stream {nullptr};

        if (is_sending_stream || is_receiving_stream)
        {
            // Mandatory and common properties for sending and receiving streams.
            unsigned int msg_size = read_property<unsigned int>(stream_map, "msg_size");
            unsigned int num_iters_in_epoch = read_property<unsigned int>(stream_map, "num_iters_in_epoch");
            unsigned int num_unroll_iter = read_property<unsigned int>(stream_map, "num_unroll_iter");
            unsigned int reg_update_vc = read_property<unsigned int>(stream_map, "reg_update_vc");

            if (is_sending_stream)
            {
                // Create the sending stream.
                stream.reset(new pipegen2::SendingStreamNode(
                    msg_size,
                    num_iters_in_epoch,
                    num_unroll_iter,
                    reg_update_vc,
                    std::move(source),
                    std::move(destination))
                );

                pipegen2::SendingStreamNode* sending_stream = static_cast<pipegen2::SendingStreamNode*>(stream.get());

                // Set mandatory properties specific for sending streams.
                sending_stream->set_destination_stream_ids(read_vector_property<std::string>(stream_map, "dest"));

                // Set optional properties specific for sending streams.
                sending_stream->set_legacy_pack(read_optional_property<bool>(stream_map, "legacy_pack"));
                sending_stream->set_outgoing_data_noc(read_optional_property<unsigned int>(stream_map, "outgoing_data_noc"));
                sending_stream->set_vc(read_optional_property<unsigned int>(stream_map, "vc"));
                sending_stream->set_operand_output_index(read_optional_property<unsigned int>(stream_map, "output_index"));
                sending_stream->set_sending_raw_data(read_optional_property<bool>(stream_map, "moves_raw_data"));
                sending_stream->set_tilized_data_config(read_optional_tilized_data_config(stream_map));
                sending_stream->set_buffer_dimensions(read_optional_buffer_dimensions(stream_map));
            }
            else //is_receiving_stream
            {
                stream.reset(new pipegen2::ReceivingStreamNode(
                    msg_size,
                    num_iters_in_epoch,
                    num_unroll_iter,
                    reg_update_vc,
                    std::move(source),
                    std::move(destination))
                );

                pipegen2::ReceivingStreamNode* receiving_stream = static_cast<pipegen2::ReceivingStreamNode*>(stream.get());

                // Set optional properties specific for receiving streams.
                receiving_stream->set_incoming_data_noc(read_optional_property<unsigned int>(stream_map, "incoming_data_noc"));
                receiving_stream->set_operand_input_index(read_optional_property<unsigned int>(stream_map, "input_index"));
                receiving_stream->set_buf_space_available_ack_thr(read_optional_property<unsigned int>(stream_map, "buf_space_available_ack_thr"));
            }

            // Set individual properties from system wide unique stream identificator.
            stream->set_chip_id(extract_chip_id(system_wide_stream_id));
            stream->set_tensix_coord_y(extract_tensix_coord_y(system_wide_stream_id));
            stream->set_tensix_coord_x(extract_tensix_coord_x(system_wide_stream_id));
            stream->set_stream_id(extract_stream_id(system_wide_stream_id));

            // Set mandatory common properties for sending and receiving streams.
            //TODO: Possibly move data_auto_send and phase_auto_advance into the constructors.
            stream->set_data_auto_send(read_property<bool>(stream_map, "data_auto_send"));
            stream->set_phase_auto_advance(read_property<bool>(stream_map, "phase_auto_advance"));

            // Set optional common properties for sending and receiving streams.
            stream->set_buffer_id(read_optional_property<pipegen2::NodeId>(stream_map, "buf_id"));
            stream->set_buffer_intermediate(read_optional_property<bool>(stream_map, "intermediate"));
            stream->set_buffer_address(read_optional_property<unsigned int>(stream_map, "buf_addr"));
            stream->set_msg_info_buf_addr(read_optional_property<unsigned int>(stream_map, "msg_info_buf_addr"));
            stream->set_buffer_size(read_optional_property<unsigned int>(stream_map, "buf_size"));
            stream->set_fork_stream_ids(read_optional_vector_property<unsigned int>(stream_map, "fork_stream_ids"));
            stream->set_num_fork_streams(read_optional_property<unsigned int>(stream_map, "num_fork_streams"));
            stream->set_group_priority(read_optional_property<unsigned int>(stream_map, "group_priority"));

            // Set DRAM properties.
            //TODO: Clasify those properties to sending or receiving streams.
            stream->set_dram_buf_noc_addr(read_optional_property<uint64_t>(stream_map, "dram_buf_noc_addr"));
            stream->set_dram_io(read_optional_property<bool>(stream_map, "dram_io"));
            stream->set_dram_streaming(read_optional_property<bool>(stream_map, "dram_streaming"));
            stream->set_dram_input(read_optional_property<bool>(stream_map, "dram_input"));
            stream->set_dram_input_no_push(read_optional_property<bool>(stream_map, "dram_input_no_push"));
            stream->set_dram_output(read_optional_property<bool>(stream_map, "dram_output"));
            stream->set_dram_output_no_push(read_optional_property<bool>(stream_map, "dram_output_no_push"));
            stream->set_producer_epoch_id(read_optional_property<unsigned int>(stream_map, "producer_epoch_id"));

            // Set other optional properties.
            //TODO: Clasify those properties.
            stream->set_num_scatter_inner_loop(read_optional_property<unsigned int>(stream_map, "num_scatter_inner_loop"));
            stream->set_pipe_id(read_optional_property<uint64_t>(stream_map, "pipe_id"));
            stream->set_pipe_scatter_output_loop_count(read_optional_property<unsigned int>(stream_map, "pipe_scatter_output_loop_count"));
            stream->set_resend(read_optional_property<bool>(stream_map, "resend"));
            stream->set_buf_base_addr(read_optional_property<unsigned int>(stream_map, "buf_base_addr"));
            stream->set_buf_full_size_bytes(read_optional_property<unsigned int>(stream_map, "buf_full_size_bytes"));
            stream->set_num_mblock_buffering(read_optional_property<unsigned int>(stream_map, "num_mblock_buffering"));
            stream->set_scatter_idx(read_optional_property<unsigned int>(stream_map, "scatter_idx"));
            stream->set_scatter_order_size(read_optional_property<unsigned int>(stream_map, "scatter_order_size"));
            stream->set_num_msgs_in_block(read_optional_property<unsigned int>(stream_map, "num_msgs_in_block"));
            stream->set_scatter_pack(read_optional_property<bool>(stream_map, "is_scatter_pack"));
        }
        else
        {
            //TODO: Improve error message with the full stream name.
            std::string err_msg = "Each stream must be defined either as a sending or a receiving stream.\n";
            err_msg += "  - A sending stream can send to either another stream or to a DRAM queue.\n";
            err_msg += "    - A stream sending to another stream must have the dest property";
            err_msg += " defined as a list of comma separated streams enclosed in square brackets.\n";
            err_msg += "      An example is 'dest: [chip_0__y_1__x_1__stream_id_8, chip_0__y_1__x_1__stream_id_9]'.\n";
            err_msg += "    - A stream sending to a DRAM queue must have the dram_output property set to true.\n";
            err_msg += "  - A receiving stream can receive from either another stream or from a DRAM queue.\n";
            err_msg += "    - A stream receiving from another stream must have";
            err_msg += " the dest property defined as an empty list.\n";
            err_msg += "    - A stream receiving from a DRAM queue must have the dram_input property set to true.\n";
            err_msg += "For the stream '";
            err_msg += system_wide_stream_id;
            err_msg += "' none of the above options has been satisfied.\n";
            throw std::runtime_error(err_msg);
        }

        return std::move(stream);
    }

    pipegen2::DramConfig create_dram_config(const YAML::Node& dram_map, unsigned int buffer_id)
    {
        pipegen2::DramConfig config;

        config.buffer_id = buffer_id;

        // Set mandatory scalar properties.
        config.dram_buf_noc_addr = read_property<uint64_t>(dram_map, "dram_buf_noc_addr");
        config.dram_buf_size_bytes = read_property<uint32_t>(dram_map, "dram_buf_size_bytes");
        config.dram_buf_size_q_slots = read_property<uint32_t>(dram_map, "dram_buf_size_q_slots");
        config.dram_buf_size_tiles = read_property<uint32_t>(dram_map, "dram_buf_size_tiles");
        config.msg_size = read_property<uint32_t>(dram_map, "msg_size");
        config.num_msgs = read_property<uint32_t>(dram_map, "num_msgs");

        // Set optional scalar properties.
        config.dram_input = read_optional_property<bool>(dram_map, "dram_input");
        config.dram_output = read_optional_property<bool>(dram_map, "dram_output");
        config.dram_io = read_optional_property<bool>(dram_map, "dram_io");
        config.dram_ram = read_optional_property<bool>(dram_map, "dram_ram");
        config.dram_scatter_chunk_size_tiles = read_optional_property<uint32_t>(dram_map, "dram_scatter_chunk_size_tiles");
        config.dram_buf_read_chunk_size_tiles = read_optional_property<uint32_t>(dram_map, "dram_buf_read_chunk_size_tiles");
        config.dram_buf_write_chunk_size_tiles = read_optional_property<uint32_t>(dram_map, "dram_buf_write_chunk_size_tiles");
        config.dram_buf_write_row_chunk_size_bytes = read_optional_property<uint32_t>(dram_map, "dram_buf_write_row_chunk_size_bytes");
        config.dram_streaming = read_optional_property<bool>(dram_map, "dram_streaming");
        config.dram_streaming_dest = read_optional_property<std::string>(dram_map, "dram_streaming_dest");
        config.reader_index = read_optional_property<uint32_t>(dram_map, "reader_index");
        config.total_readers = read_optional_property<uint32_t>(dram_map, "total_readers");

        // Set optional sequence properties.
        config.dram_scatter_offsets = read_optional_vector_property<uint64_t>(dram_map, "dram_scatter_offsets");

        return config;
    }

    pipegen2::Phase create_phase(const YAML::Node& stream_map, const pipegen2::PhaseId phase_id)
    {
        const unsigned int tiles_count = read_property<unsigned int>(stream_map, "num_msgs");
        const bool next_phase_src_change = read_property<bool>(stream_map, "next_phase_src_change");
        const bool next_phase_dest_change = read_property<bool>(stream_map, "next_phase_dest_change");
        const bool next_phase_auto_configured = read_property<bool>(stream_map, "phase_auto_config");
        const unsigned int unrolled_iteration_index = read_property<unsigned int>(stream_map, "unroll_iter");

        return pipegen2::Phase {phase_id,
                                tiles_count,
                                next_phase_src_change,
                                next_phase_dest_change,
                                next_phase_auto_configured,
                                unrolled_iteration_index};
    }

    std::unique_ptr<pipegen2::BaseStreamSource> create_stream_source(const YAML::Node& stream_map)
    {
        // Determine stream source type.
        bool is_source_endpoint_set = stream_map["source_endpoint"].as<bool>(false);
        bool is_local_source_set = stream_map["local_sources_connected"].as<bool>(false);
        bool is_remote_source_set = stream_map["remote_source"].as<bool>(false);

        std::unique_ptr<pipegen2::BaseStreamSource> source {nullptr};

        if (is_source_endpoint_set && !is_local_source_set && !is_remote_source_set)
        {
            // Create a source endpoint stream type node.
            pipegen2::EndpointStreamSource* endpoint_source = new pipegen2::EndpointStreamSource();
            source.reset(endpoint_source);

            // Set optional properties.
            endpoint_source->set_ptrs_not_zero(read_optional_property<bool>(stream_map, "ptrs_not_zero"));
        }
        else if (is_local_source_set && !is_source_endpoint_set && !is_remote_source_set)
        {
            // Create a local source stream type node.
            source.reset(new pipegen2::LocalStreamSource());
        }
        else if (is_remote_source_set && !is_source_endpoint_set && !is_local_source_set)
        {
            // Create a remote source stream type node.
            source.reset(new pipegen2::RemoteStreamSource());
        }
        else
        {
            std::string err_msg = "Exactly one of the following three properties";
            err_msg += " must be set to 'true' for each stream in the blob.yaml file.";
            err_msg += " The read values are:";
            err_msg += "\n  'source_endpoint' = ";
            err_msg += stream_map["source_endpoint"].as<std::string>(std::string("unset"));
            err_msg += "\n  'local_sources_connected' = ";
            err_msg += stream_map["local_sources_connected"].as<std::string>(std::string("unset"));
            err_msg += "\n  'remote_source' = ";
            err_msg += stream_map["remote_source"].as<std::string>(std::string("unset"));
            err_msg += ".";
            throw std::runtime_error(err_msg);
        }

        return std::move(source);
    }

    std::unique_ptr<pipegen2::BaseStreamDestination> create_stream_destination(const YAML::Node& stream_map)
    {
        // Determine stream destination type.
        bool is_receiver_endpoint_set = stream_map["receiver_endpoint"].as<bool>(false);
        bool is_local_receiver_set = stream_map["local_receiver"].as<bool>(false);
        bool is_remote_receiver_set = stream_map["remote_receiver"].as<bool>(false);

        std::unique_ptr<pipegen2::BaseStreamDestination> destination {nullptr};

        if (is_receiver_endpoint_set && !is_local_receiver_set && !is_remote_receiver_set)
        {
            // Create a destination endpoint stream type node.
            destination.reset(new pipegen2::EndpointStreamDestination());
        }
        else if (is_local_receiver_set && !is_receiver_endpoint_set && !is_remote_receiver_set)
        {
            // Create a local destination stream type node.
            //TODO: Check if both source and receiver are local which is illegal combination.
            pipegen2::LocalStreamDestination* local_destination = new pipegen2::LocalStreamDestination();
            destination.reset(local_destination);

            // Set optional properties.
            local_destination->set_local_receiver_tile_clearing(
                read_optional_property<bool>(stream_map, "local_receiver_tile_clearing"));
        }
        else if (is_remote_receiver_set && !is_receiver_endpoint_set && !is_local_receiver_set)
        {
            // Create a remote destination stream type node.
            destination.reset(new pipegen2::RemoteStreamDestination());
        }
        else
        {
            std::string err_msg = "Exactly one of the following three properties";
            err_msg += " must be set to 'true' for each stream in the blob.yaml file.";
            err_msg += " The read values are:";
            err_msg += "\n  'receiver_endpoint' = ";
            err_msg += stream_map["receiver_endpoint"].as<std::string>(std::string("unset"));
            err_msg += "\n  'local_receiver' = ";
            err_msg += stream_map["local_receiver"].as<std::string>(std::string("unset"));
            err_msg += "\n  'remote_receiver' = ";
            err_msg += stream_map["remote_receiver"].as<std::string>(std::string("unset"));
            err_msg += ".";
            throw std::runtime_error(err_msg);
        }

        return std::move(destination);
    }

    std::optional<pipegen2::SendingStreamNode::TilizedDataConfig> read_optional_tilized_data_config(
        const YAML::Node& stream_map)
    {
        const YAML::Node batch_dim_size_node = stream_map["batch_dim_size"];
        const YAML::Node r_dim_size_node = stream_map["r_dim_size"];
        const YAML::Node c_dim_size_node = stream_map["c_dim_size"];
        const YAML::Node zr_dim_size_node = stream_map["zr_dim_size"];
        const YAML::Node zc_dim_size_node = stream_map["zc_dim_size"];
        const YAML::Node skip_col_bytes_node = stream_map["skip_col_bytes"];
        const YAML::Node skip_col_tile_row_bytes_node = stream_map["skip_col_tile_row_bytes"];
        const YAML::Node skip_col_row_bytes_node = stream_map["skip_col_row_bytes"];
        const YAML::Node skip_zcol_bytes_node = stream_map["skip_zcol_bytes"];
        const YAML::Node skip_col_zrow_bytes_node = stream_map["skip_col_zrow_bytes"];
        const YAML::Node stride_node = stream_map["stride"];
        const YAML::Node total_strides_node = stream_map["total_strides"];
        const YAML::Node stride_offset_size_bytes_node = stream_map["stride_offset_size_bytes"];

        bool is_batch_dim_size_node_defined = batch_dim_size_node.IsDefined();
        bool is_r_dim_size_node_defined = r_dim_size_node.IsDefined();
        bool is_c_dim_size_node_defined = c_dim_size_node.IsDefined();
        bool is_zr_dim_size_node_defined = zr_dim_size_node.IsDefined();
        bool is_zc_dim_size_node_defined = zc_dim_size_node.IsDefined();
        bool is_skip_col_bytes_node_defined = skip_col_bytes_node.IsDefined();
        bool is_skip_col_tile_row_bytes_node_defined = skip_col_tile_row_bytes_node.IsDefined();
        bool is_skip_col_row_bytes_node_defined = skip_col_row_bytes_node.IsDefined();
        bool is_skip_zcol_bytes_node_defined = skip_zcol_bytes_node.IsDefined();
        bool is_skip_col_zrow_bytes_node_defined = skip_col_zrow_bytes_node.IsDefined();
        bool is_stride_node_defined = stride_node.IsDefined();
        bool is_total_strides_node_defined = total_strides_node.IsDefined();
        bool is_stride_offset_size_bytes_nodes_defined = stride_offset_size_bytes_node.IsDefined();

        if (is_batch_dim_size_node_defined && is_r_dim_size_node_defined && is_c_dim_size_node_defined &&
            is_zr_dim_size_node_defined && is_zc_dim_size_node_defined &&
            is_skip_col_bytes_node_defined && is_skip_col_tile_row_bytes_node_defined &&
            is_skip_col_row_bytes_node_defined && is_skip_zcol_bytes_node_defined &&
            is_skip_col_zrow_bytes_node_defined && is_stride_node_defined &&
            is_total_strides_node_defined && is_stride_offset_size_bytes_nodes_defined)
        {
            pipegen2::SendingStreamNode::TilizedDataConfig tilized_data_config;

            tilized_data_config.batch_dim_size = batch_dim_size_node.as<unsigned int>();
            tilized_data_config.r_dim_size = r_dim_size_node.as<unsigned int>();
            tilized_data_config.c_dim_size = c_dim_size_node.as<unsigned int>();
            tilized_data_config.zr_dim_size = zr_dim_size_node.as<unsigned int>();
            tilized_data_config.zc_dim_size = zc_dim_size_node.as<unsigned int>();
            tilized_data_config.skip_col_bytes = skip_col_bytes_node.as<unsigned int>();
            tilized_data_config.skip_col_tile_row_bytes = skip_col_tile_row_bytes_node.as<unsigned int>();
            tilized_data_config.skip_col_row_bytes = skip_col_row_bytes_node.as<unsigned int>();
            tilized_data_config.skip_zcol_bytes = skip_zcol_bytes_node.as<unsigned int>();
            tilized_data_config.skip_col_zrow_bytes = skip_col_zrow_bytes_node.as<unsigned int>();
            tilized_data_config.stride = stride_node.as<unsigned int>();
            tilized_data_config.total_strides = total_strides_node.as<unsigned int>();
            tilized_data_config.stride_offset_size_bytes = stride_offset_size_bytes_node.as<unsigned int>();

            return tilized_data_config;
        }
        else if (is_batch_dim_size_node_defined || is_r_dim_size_node_defined || is_c_dim_size_node_defined ||
                 is_zr_dim_size_node_defined || is_zc_dim_size_node_defined ||
                 is_skip_col_bytes_node_defined || is_skip_col_tile_row_bytes_node_defined ||
                 is_skip_col_row_bytes_node_defined || is_skip_zcol_bytes_node_defined ||
                 is_skip_col_zrow_bytes_node_defined || is_stride_node_defined ||
                 is_total_strides_node_defined || is_stride_offset_size_bytes_nodes_defined)
        {
            //TODO Improve error message with the stream name.
            std::string err_msg = "Either none or all of the following properties ";
            err_msg += "must be set for the stream phase: ";
            err_msg += "is_batch_dim_size_node_defined, is_r_dim_size_node_defined, is_c_dim_size_node_defined, ";
            err_msg += "is_zr_dim_size_node_defined, is_zc_dim_size_node_defined, is_skip_col_bytes_node_defined, ";
            err_msg += "is_skip_col_tile_row_bytes_node_defined, is_skip_col_row_bytes_node_defined, ";
            err_msg += "is_skip_zcol_bytes_node_defined, is_skip_col_zrow_bytes_node_defined, is_stride_node_defined, ";
            err_msg += "is_total_strides_node_defined, is_stride_offset_size_bytes_nodes_defined";
            throw std::runtime_error(err_msg);
        }

        return {};
    }

    std::optional<pipegen2::SendingStreamNode::BufferDimensions> read_optional_buffer_dimensions(
        const YAML::Node& stream_map)
    {
        const YAML::Node mblock_k_node = stream_map["mblock_k"];
        const YAML::Node mblock_m_node = stream_map["mblock_m"];
        const YAML::Node mblock_n_node = stream_map["mblock_n"];
        const YAML::Node ublock_ct_node = stream_map["ublock_ct"];
        const YAML::Node ublock_rt_node = stream_map["ublock_rt"];

        bool is_mblock_k_defined = mblock_k_node.IsDefined();
        bool is_mblock_m_defined = mblock_m_node.IsDefined();
        bool is_mblock_n_defined = mblock_n_node.IsDefined();
        bool is_ublock_ct_defined = ublock_ct_node.IsDefined();
        bool is_ublock_rt_defined = ublock_rt_node.IsDefined();

        if (is_mblock_k_defined && is_mblock_m_defined && is_mblock_n_defined &&
            is_ublock_ct_defined && is_ublock_rt_defined)
        {
            pipegen2::SendingStreamNode::BufferDimensions buffer_dimensions;

            buffer_dimensions.mblock_k = mblock_k_node.as<unsigned int>();
            buffer_dimensions.mblock_m = mblock_m_node.as<unsigned int>();
            buffer_dimensions.mblock_n = mblock_n_node.as<unsigned int>();
            buffer_dimensions.ublock_ct = ublock_ct_node.as<unsigned int>();
            buffer_dimensions.ublock_rt = ublock_rt_node.as<unsigned int>();

            return buffer_dimensions;
        }
        else if (is_mblock_k_defined || is_mblock_m_defined || is_mblock_n_defined ||
                 is_ublock_ct_defined || is_ublock_rt_defined)
        {
            //TODO Improve error message with the stream name.
            std::string err_msg = "Either none or all of the following properties ";
            err_msg += "must be set for the stream phase: ";
            err_msg += "mblock_k, mblock_m, mblock_n, ublock_ct, ublock_rt.\n";
            throw std::runtime_error(err_msg);
        }

        return {};
    }

    unsigned int extract_chip_id(const std::string& system_wide_stream_id)
    {
        const std::size_t start = pipegen2::blob_yaml_chip_key.length();
        const std::size_t length = system_wide_stream_id.find(pipegen2::blob_yaml_tensix_y_key) - start;
        const std::string result_str = system_wide_stream_id.substr(start, length);
        const unsigned int result = std::stoul(result_str);

        return result;
    }

    unsigned int extract_tensix_coord_y(const std::string& system_wide_stream_id)
    {
        const std::size_t start = system_wide_stream_id.find(pipegen2::blob_yaml_tensix_y_key) + pipegen2::blob_yaml_tensix_y_key.length();
        const std::size_t length = system_wide_stream_id.find(pipegen2::blob_yaml_tensix_x_key) - start;
        const std::string result_str = system_wide_stream_id.substr(start, length);
        const unsigned int result = std::stoul(result_str);

        return result;
    }

    unsigned int extract_tensix_coord_x(const std::string& system_wide_stream_id)
    {
        const std::size_t start = system_wide_stream_id.find(pipegen2::blob_yaml_tensix_x_key) + pipegen2::blob_yaml_tensix_x_key.length();
        const std::size_t length = system_wide_stream_id.find(pipegen2::blob_yaml_stream_key) - start;
        const std::string result_str = system_wide_stream_id.substr(start, length);
        const unsigned int result = std::stoul(result_str);

        return result;
    }

    unsigned int extract_stream_id(const std::string& system_wide_stream_id)
    {
        const std::size_t start = system_wide_stream_id.find(pipegen2::blob_yaml_stream_key) + pipegen2::blob_yaml_stream_key.length();
        const std::size_t length = system_wide_stream_id.length() - start;
        const std::string result_str = system_wide_stream_id.substr(start, length);
        const unsigned int result = std::stoul(result_str);

        return result;
    }

    template <typename T>
    T read_property(const YAML::Node& sequence, const std::string& key)
    {
        const YAML::Node& node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsScalar())
            {
                return node.as<T>();
            }
            else
            {
                // TODO: Improve error message with string of the stream that contains the current node.
                std::string err_msg = "Property '";
                err_msg += key;
                err_msg += "' must be a ";
                err_msg += get_node_type_string(YAML::NodeType::Scalar);
                err_msg += ", whereas its type is: '";
                err_msg += get_node_type_string(node.Type());
                err_msg += "' and its value is: '";
                err_msg += node.as<std::string>();
                err_msg += "'.";
                throw std::runtime_error(err_msg);
            }
        }
        else
        {
            // TODO: Improve error message with string of the node hierarchy that contains the current node.
            std::string err_msg = "Property '";
            err_msg += key;
            err_msg += "' must be defined.";
            throw std::runtime_error(err_msg);
        }
    }

    template <typename T>
    std::vector<T> read_vector_property(const YAML::Node& sequence, const std::string& key)
    {
        const YAML::Node& node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsSequence())
            {
                std::vector<T> vec;
                for (YAML::const_iterator it=node.begin(); it!=node.end(); ++it)
                {
                    vec.push_back((*it).as<T>());
                }
                return std::move(vec);
            }
            else
            {
                // TODO: Improve error message with string of the stream that contains the current node.
                std::string err_msg = "Property '";
                err_msg += key;
                err_msg += "' must be a ";
                err_msg += get_node_type_string(YAML::NodeType::Sequence);
                err_msg += ", whereas its type is: '";
                err_msg += get_node_type_string(node.Type());
                err_msg += "' and its value is: '";
                err_msg += node.as<std::string>();
                err_msg += "'.";
                throw std::runtime_error(err_msg);
            }
        }
        else
        {
            // TODO: Improve error message with string of the node hierarchy that contains the current node.
            std::string err_msg = "Property '";
            err_msg += key;
            err_msg += "' must be defined.";
            throw std::runtime_error(err_msg);
        }
    }


    template <typename T>
    std::optional<T> read_optional_property(const YAML::Node& sequence, const std::string& key)
    {
        const YAML::Node& node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsScalar())
            {
                return std::optional{node.as<T>()};
            }
            else
            {
                // TODO: Improve error message with string of the stream that contains the current node.
                std::string err_msg = "Property '";
                err_msg += key;
                err_msg += "' must be a ";
                err_msg += get_node_type_string(YAML::NodeType::Scalar);
                err_msg += ", whereas its type is: '";
                err_msg += get_node_type_string(node.Type());
                err_msg += "' and its value is: '";
                err_msg += node.as<std::string>();
                err_msg += "'.";
                throw std::runtime_error(err_msg);
            }
        }

        return {};
    }

    template <typename T>
    std::optional<std::vector<T>> read_optional_vector_property(const YAML::Node& sequence, const std::string& key)
    {
        const YAML::Node& node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsSequence())
            {
                std::vector<T> vec;
                for (YAML::const_iterator it=node.begin(); it!=node.end(); ++it)
                {
                    vec.push_back((*it).as<T>());
                }
                return std::move(std::optional{vec});
            }
            else
            {
                // TODO: Improve error message with string of the stream that contains the current node.
                std::string err_msg = "Property '";
                err_msg += key;
                err_msg += "' must be a ";
                err_msg += get_node_type_string(YAML::NodeType::Sequence);
                err_msg += ", whereas its type is: '";
                err_msg += get_node_type_string(node.Type());
                err_msg += "' and its value is: '";
                err_msg += node.as<std::string>();
                err_msg += "'.";
                throw std::runtime_error(err_msg);
            }
        }

        return {};
    }

    pipegen2::PhaseId get_phase_id(const std::string& phase_id_str)
    {
        std::istringstream iss {phase_id_str};
        pipegen2::PhaseId phase_id;
        iss >> phase_id;
        return phase_id;
    }

    std::string get_node_type_string(const YAML::NodeType::value& node_type)
    {
        switch (node_type)
        {
            case YAML::NodeType::Undefined:
                return "undefined";
            case YAML::NodeType::Null:
                return "null";
            case YAML::NodeType::Scalar:
                return "scalar";
            case YAML::NodeType::Sequence:
                return "sequence";
            case YAML::NodeType::Map:
                return "map";
            default:
                return "unknown";
        }
    }

}