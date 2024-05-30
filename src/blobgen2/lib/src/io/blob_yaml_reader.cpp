// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_yaml_reader.h"

#include "model/stream_graph/ncrisc_config.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "overlay_blob/typedef.h"

namespace blobgen2
{

using pipegen2::NOC_ROUTE;
using pipegen2::NodeId;
using pipegen2::StreamGraph;
using pipegen2::StreamType;

std::pair<std::unique_ptr<StreamGraphCollection>, std::map<tt_cxy_pair, dram_perf_info_t>>
BlobYamlReader::read_blob_yaml(const std::string blob_yaml_path)
{
    YAML::Node blob_yaml = YAML::LoadFile(blob_yaml_path);

    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>> stream_nodes = read_all_stream_ids(blob_yaml);
    fill_dram_blob(blob_yaml, stream_nodes);
    fill_phase_configs(blob_yaml, stream_nodes);
    std::map<tt_cxy_pair, dram_perf_info_t> perf_info = fill_dram_perf_dump_blob(blob_yaml);
    std::map<tt_cxy_pair, L1BufferAllocationInfo> ncrisc_fallback_buffers = fill_ncrisc_fallback_buffers(blob_yaml);

    return {fill_stream_graph_collection(stream_nodes, ncrisc_fallback_buffers), perf_info};
}

std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>> BlobYamlReader::read_all_stream_ids(
    const YAML::Node blob_yaml)
{
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>> stream_nodes;

    foreach_dram_blob(
        blob_yaml,
        [&stream_nodes](const tt_cxys_pair& stream_location, const YAML::Node)
        { add_stream_node_if_not_exists(stream_nodes, stream_location); });

    foreach_phase_config(
        blob_yaml,
        [&stream_nodes](const tt_cxys_pair& stream_location, const PhaseId&, const YAML::Node)
        { add_stream_node_if_not_exists(stream_nodes, stream_location); });
    return stream_nodes;
}

void BlobYamlReader::fill_dram_blob(
    const YAML::Node blob_yaml, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    foreach_dram_blob(
        blob_yaml,
        [&stream_nodes](const tt_cxys_pair& stream_location, const YAML::Node buffers_map)
        {
            log_assert(
                stream_nodes.find(stream_location) != stream_nodes.end(),
                "Stream {} not found while filling dram_blob section",
                stream_location.str());

            std::vector<NcriscConfig> ncrisc_configs;

            for (YAML::const_iterator buffer_it = buffers_map.begin(); buffer_it != buffers_map.end(); ++buffer_it)
            {
                const unsigned int buffer_id = buffer_it->first.as<unsigned int>();
                const YAML::Node dram_config_map = buffer_it->second;
                log_assert(
                    buffer_id == ncrisc_configs.size(),
                    "Buffer id {} out of order, expected {}",
                    buffer_id,
                    ncrisc_configs.size());

                ncrisc_configs.emplace_back(extract_ncrisc_config(dram_config_map, stream_nodes));
            }

            stream_nodes.at(stream_location)->set_ncrisc_configs(std::move(ncrisc_configs));
        });
}

void BlobYamlReader::fill_phase_configs(
    const YAML::Node blob_yaml, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    foreach_phase_config(
        blob_yaml,
        [&stream_nodes](
            const tt_cxys_pair& stream_location, const PhaseId& phase_id, const YAML::Node phase_config_yaml)
        {
            log_assert(
                stream_nodes.find(stream_location) != stream_nodes.end(),
                "Stream {} not found while filling phase sections",
                stream_location.str());

            PhaseConfig phase = extract_phase_config(stream_location, phase_config_yaml, stream_nodes);
            log_assert(
                phase_id == phase.phase_id,
                "Phase id section {} and phase id in config {} do not match",
                phase_id,
                phase.phase_id);

            stream_nodes.at(stream_location)->add_phase_config(phase_id, std::move(phase.config));
        });
}

std::map<tt_cxy_pair, dram_perf_info_t> BlobYamlReader::fill_dram_perf_dump_blob(const YAML::Node blob_yaml)
{
    std::map<tt_cxy_pair, dram_perf_info_t> dram_perf_info;

    foreach_dram_perf_dump_blob(
        blob_yaml,
        [&dram_perf_info](
            const tt_cxy_pair& core_location,
            std::vector<uint64_t>& dram_noc_addr_vect,
            std::vector<uint16_t>& dram_max_req_vect)
        {
            dram_perf_buf_noc_addr_t dram_noc_addr_arr;
            log_assert(
                dram_noc_addr_vect.size() == dram_noc_addr_arr.size(),
                "Mismatch while reading dram_perf_dump_blob, {} != {}",
                dram_noc_addr_vect.size(),
                dram_noc_addr_arr.size());
            for (int i = 0; i < dram_noc_addr_vect.size(); i++)
            {
                dram_noc_addr_arr[i] = dram_noc_addr_vect[i];
            }

            dram_perf_buf_max_req_t dram_max_req_arr;
            log_assert(
                dram_max_req_vect.size() == dram_max_req_arr.size(),
                "Mismatch while reading dram_perf_dump_blob, {} != {}",
                dram_max_req_vect.size(),
                dram_max_req_arr.size());
            for (int i = 0; i < dram_max_req_vect.size(); i++)
            {
                dram_max_req_arr[i] = dram_max_req_vect[i];
            }

            dram_perf_info.emplace(core_location, dram_perf_info_t{dram_noc_addr_arr, dram_max_req_arr});
        });

    return dram_perf_info;
}

std::map<tt_cxy_pair, L1BufferAllocationInfo> BlobYamlReader::fill_ncrisc_fallback_buffers(const YAML::Node blob_yaml)
{
    std::map<tt_cxy_pair, L1BufferAllocationInfo> ncrisc_fallback_buffers;
    foreach_ncrisc_fallback_buffers(
        blob_yaml,
        [&ncrisc_fallback_buffers](
            const tt_cxy_pair& core_location, L1BufferAllocationInfo ncrisc_fallback_buffer_l1_info)
        { ncrisc_fallback_buffers.emplace(core_location, ncrisc_fallback_buffer_l1_info); });

    return ncrisc_fallback_buffers;
}

std::unique_ptr<StreamGraphCollection> BlobYamlReader::fill_stream_graph_collection(
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes,
    std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers)
{
    std::unique_ptr<StreamGraphCollection> stream_graphs = std::make_unique<StreamGraphCollection>();

    std::unique_ptr<StreamGraph> stream_graph_uptr = std::make_unique<StreamGraph>();
    for (auto& [stream_location, stream_node] : stream_nodes)
    {
        stream_graph_uptr->add_stream(std::move(stream_node));
    }
    stream_graphs->add_stream_graph(std::move(stream_graph_uptr));

    for (auto& [core_location, l1_buffer_info] : ncrisc_fallback_buffers)
    {
        stream_graphs->add_ncrisc_fallback_buffer_allocation(core_location, l1_buffer_info);
    }

    return stream_graphs;
}

void BlobYamlReader::add_stream_node_if_not_exists(
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes, const tt_cxys_pair& stream_location)
{
    if (stream_nodes.find(stream_location) == stream_nodes.end())
    {
        // TODO set to proper type.
        StreamType type = StreamType::Multicast;
        stream_nodes.emplace(stream_location, std::make_unique<StreamNode>(type, stream_location.to_cxy_pair()));
        stream_nodes.at(stream_location)->assign_stream_id(stream_location.stream_id);
    }
}

void BlobYamlReader::foreach_dram_blob(
    const YAML::Node blob_yaml, std::function<void(const tt_cxys_pair&, const YAML::Node)> func)
{
    const YAML::Node dram_blob_section = blob_yaml["dram_blob"];
    for (YAML::const_iterator stream_it = dram_blob_section.begin(); stream_it != dram_blob_section.end(); ++stream_it)
    {
        tt_cxys_pair stream_location = extract_stream_info(stream_it->first.as<std::string>());
        func(stream_location, stream_it->second);
    }
}

void BlobYamlReader::foreach_phase_config(
    const YAML::Node blob_yaml, std::function<void(const tt_cxys_pair&, const PhaseId&, const YAML::Node)> func)
{
    for (YAML::const_iterator phase_it = blob_yaml.begin(); phase_it != blob_yaml.end(); ++phase_it)
    {
        const std::string phase_key = phase_it->first.as<std::string>();

        if (phase_key.rfind("phase_", 0) == 0)
        {
            const std::string phase_id_str = phase_key.substr(6);
            const PhaseId phase_id = get_phase_id(phase_id_str);
            const YAML::Node phases = phase_it->second;

            for (YAML::const_iterator stream_it = phases.begin(); stream_it != phases.end(); ++stream_it)
            {
                tt_cxys_pair stream_location = extract_stream_info(stream_it->first.as<std::string>());
                func(stream_location, phase_id, stream_it->second);
            }
        }
    }
}

void BlobYamlReader::foreach_dram_perf_dump_blob(
    const YAML::Node blob_yaml,
    std::function<void(const tt_cxy_pair&, std::vector<uint64_t>&, std::vector<uint16_t>&)> func)
{
    const YAML::Node dram_perf_dump_blob = blob_yaml["dram_perf_dump_blob"];
    for (YAML::const_iterator core_it = dram_perf_dump_blob.begin(); core_it != dram_perf_dump_blob.end(); ++core_it)
    {
        tt_cxy_pair core_location = extract_core_info(core_it->first.as<std::string>());
        std::vector<uint64_t> dram_noc_addr_vect =
            YamlReaderHelper::read_vector_property<uint64_t>(core_it->second, "dram_perf_buf_noc_addr");
        std::vector<uint16_t> dram_max_req_vect =
            YamlReaderHelper::read_vector_property<uint16_t>(core_it->second, "dram_perf_buf_max_req");

        func(core_location, dram_noc_addr_vect, dram_max_req_vect);
    }
}

void BlobYamlReader::foreach_ncrisc_fallback_buffers(
    const YAML::Node blob_yaml, std::function<void(const tt_cxy_pair&, L1BufferAllocationInfo)> func)
{
    const YAML::Node dram_perf_dump_blob = blob_yaml["global_info_blob"];
    for (YAML::const_iterator core_it = dram_perf_dump_blob.begin(); core_it != dram_perf_dump_blob.end(); ++core_it)
    {
        tt_cxy_pair core_location = extract_core_info(core_it->first.as<std::string>());
        L1BufferAllocationInfo l1_buffer_info;
        l1_buffer_info.address =
            YamlReaderHelper::read_property<uint64_t>(core_it->second, "ncrisc_fallback_buffer_l1_address");
        l1_buffer_info.size = YamlReaderHelper::read_property<uint64_t>(core_it->second, "ncrisc_fallback_buffer_size");

        func(core_location, l1_buffer_info);
    }
}

tt_cxys_pair BlobYamlReader::extract_stream_info(const std::string& system_wide_stream_id)
{
    std::regex re("chip_(\\d+)__y_(\\d+)__x_(\\d+)__stream_id_(\\d+)");
    std::smatch match;
    if (std::regex_search(system_wide_stream_id, match, re))
    {
        const int chip = std::stoi(match[1].str());
        const int y = std::stoi(match[2].str());
        const int x = std::stoi(match[3].str());
        const int stream_id = std::stoi(match[4].str());
        return tt_cxys_pair(stream_id, chip, x, y);
    }
    throw std::runtime_error("Failed to extract stream info from system_wide_stream_id");
}

tt_cxy_pair BlobYamlReader::extract_core_info(const std::string& system_wide_core_id)
{
    std::regex re("chip_(\\d+)__y_(\\d+)__x_(\\d+)");
    std::smatch match;
    if (std::regex_search(system_wide_core_id, match, re))
    {
        const int chip = std::stoi(match[1].str());
        const int y = std::stoi(match[2].str());
        const int x = std::stoi(match[3].str());
        return tt_cxy_pair(chip, x, y);
    }
    throw std::runtime_error("Failed to extract core info from system_wide_core_id");
}

PhaseId BlobYamlReader::get_phase_id(const std::string& phase_id_str)
{
    std::istringstream iss{phase_id_str};
    PhaseId phase_id;
    iss >> phase_id;
    return phase_id;
}

std::optional<StreamNode*> BlobYamlReader::get_stream_node_opt(
    const std::optional<std::string> stream_id,
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    if (!stream_id)
    {
        return std::nullopt;
    }
    tt_cxys_pair stream_location = extract_stream_info(stream_id.value());
    log_assert(stream_nodes.find(stream_location) != stream_nodes.end(), "Stream {} not found", stream_location.str());
    return stream_nodes.at(stream_location).get();
}

std::optional<StreamNode*> BlobYamlReader::get_stream_node_opt(
    const tt_cxys_pair& original_stream_location,
    const std::optional<int>& stream_id,
    const std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    if (!stream_id)
    {
        return std::nullopt;
    }
    tt_cxys_pair stream_location = original_stream_location;
    stream_location.stream_id = stream_id.value();
    log_assert(stream_nodes.find(stream_location) != stream_nodes.end(), "Stream {} not found", stream_location.str());
    return stream_nodes.at(stream_location).get();
}

std::optional<std::vector<StreamNode*>> BlobYamlReader::get_stream_nodes_opt(
    const std::optional<std::vector<std::string>>& stream_ids,
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    if (!stream_ids)
    {
        return std::nullopt;
    }

    std::vector<StreamNode*> stream_node_vector;
    for (const std::string stream_id : stream_ids.value())
    {
        stream_node_vector.push_back(get_stream_node_opt(stream_id, stream_nodes).value());
    }
    return stream_node_vector;
}

std::optional<std::vector<StreamNode*>> BlobYamlReader::get_stream_nodes_opt(
    const tt_cxys_pair& original_stream_location,
    const std::optional<std::vector<int>>& stream_ids,
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    if (!stream_ids)
    {
        return std::nullopt;
    }

    std::vector<StreamNode*> stream_node_vector;
    for (int stream_id : stream_ids.value())
    {
        stream_node_vector.push_back(get_stream_node_opt(original_stream_location, stream_id, stream_nodes).value());
    }
    return stream_node_vector;
}

NcriscConfig BlobYamlReader::extract_ncrisc_config(
    const YAML::Node dram_config_map, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    NcriscConfig ncrisc_config;

    ncrisc_config.dram_buf_noc_addr = YamlReaderHelper::read_property<uint64_t>(dram_config_map, "dram_buf_noc_addr");
    ncrisc_config.dram_buf_read_chunk_size_tiles =
        YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "dram_buf_read_chunk_size_tiles");
    ncrisc_config.dram_buf_size_bytes =
        YamlReaderHelper::read_property<uint32_t>(dram_config_map, "dram_buf_size_bytes");
    ncrisc_config.dram_buf_size_q_slots =
        YamlReaderHelper::read_property<uint32_t>(dram_config_map, "dram_buf_size_q_slots");
    ncrisc_config.dram_buf_size_tiles =
        YamlReaderHelper::read_property<uint32_t>(dram_config_map, "dram_buf_size_tiles");
    ncrisc_config.dram_buf_write_chunk_size_tiles =
        YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "dram_buf_write_chunk_size_tiles");
    ncrisc_config.dram_buf_write_row_chunk_size_bytes =
        YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "dram_buf_write_row_chunk_size_bytes");
    ncrisc_config.dram_input = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_input");
    ncrisc_config.dram_io = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_io");
    ncrisc_config.dram_output = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_output");
    ncrisc_config.dram_padding = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_padding");
    ncrisc_config.dram_ram = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_ram");
    ncrisc_config.dram_scatter_chunk_size_tiles =
        YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "dram_scatter_chunk_size_tiles");
    ncrisc_config.dram_scatter_offsets =
        YamlReaderHelper::read_optional_vector_property<uint64_t>(dram_config_map, "dram_scatter_offsets");
    ncrisc_config.dram_scatter_offsets_full_size =
        YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "dram_scatter_offsets_full_size");
    ncrisc_config.dram_streaming = YamlReaderHelper::read_optional_property<bool>(dram_config_map, "dram_streaming");
    ncrisc_config.dram_streaming_dest = get_stream_node_opt(
        YamlReaderHelper::read_optional_property<std::string>(dram_config_map, "dram_streaming_dest"), stream_nodes);
    ncrisc_config.msg_size = YamlReaderHelper::read_property<uint32_t>(dram_config_map, "msg_size");
    ncrisc_config.num_msgs = YamlReaderHelper::read_property<uint32_t>(dram_config_map, "num_msgs");
    ncrisc_config.reader_index = YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "reader_index");
    ncrisc_config.total_readers = YamlReaderHelper::read_optional_property<uint32_t>(dram_config_map, "total_readers");

    return ncrisc_config;
}

PhaseConfig BlobYamlReader::extract_phase_config(
    const tt_cxys_pair& stream_location,
    const YAML::Node phase_config_map,
    std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes)
{
    PhaseConfig phase_config;

    phase_config.phase_id = YamlReaderHelper::read_property<PhaseId>(phase_config_map, "phase_id");

    phase_config.config.opt_set_arb_group_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "arb_group_size"));
    phase_config.config.opt_set_batch_dim_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "batch_dim_size"));
    phase_config.config.opt_set_buf_addr(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "buf_addr"));
    phase_config.config.opt_set_buf_base_addr(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "buf_base_addr"));
    phase_config.config.opt_set_buf_full_size_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "buf_full_size_bytes"));
    phase_config.config.opt_set_buf_id(YamlReaderHelper::read_optional_property<NodeId>(phase_config_map, "buf_id"));
    phase_config.config.opt_set_buffer_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "buf_size"));
    phase_config.config.opt_set_buf_space_available_ack_thr(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "buf_space_available_ack_thr"));
    phase_config.config.opt_set_c_dim_loop_num_rows(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "c_dim_loop_num_rows"));
    phase_config.config.opt_set_c_dim_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "c_dim_size"));
    phase_config.config.opt_set_data_auto_send(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "data_auto_send"));
    phase_config.config.opt_set_data_buf_no_flow_ctrl(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "data_buf_no_flow_ctrl"));
    phase_config.config.opt_set_dest_data_buf_no_flow_ctrl(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dest_data_buf_no_flow_ctrl"));

    phase_config.config.opt_set_dest(get_stream_nodes_opt(
        YamlReaderHelper::read_optional_vector_property<std::string>(phase_config_map, "dest"), stream_nodes));

    phase_config.config.opt_set_dram_buf_noc_addr(
        YamlReaderHelper::read_optional_property<uint64_t>(phase_config_map, "dram_buf_noc_addr"));
    phase_config.config.opt_set_dram_embeddings_row_shift(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "dram_embeddings_row_shift"));
    phase_config.config.opt_set_dram_embeddings_row_tiles(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "dram_embeddings_row_tiles"));
    phase_config.config.opt_set_dram_input(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_input"));
    phase_config.config.opt_set_dram_input_no_push(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_input_no_push"));
    phase_config.config.opt_set_dram_io(YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_io"));
    phase_config.config.opt_set_dram_output(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_output"));
    phase_config.config.opt_set_dram_output_no_push(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_output_no_push"));
    phase_config.config.opt_set_dram_ram(YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_ram"));
    phase_config.config.opt_set_dram_streaming(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_streaming"));
    phase_config.config.opt_set_dram_writes_with_cmd_buf(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "dram_writes_with_cmd_buf"));
    phase_config.config.opt_set_eth_sender(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "eth_sender"));
    phase_config.config.opt_set_eth_receiver(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "eth_receiver"));
    phase_config.config.opt_set_follow_by_receiver_dummy_phase(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "follow_by_receiver_dummy_phase"));
    phase_config.config.opt_set_follow_by_sender_dummy_phase(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "follow_by_sender_dummy_phase"));
    phase_config.config.opt_set_fork_streams(get_stream_nodes_opt(
        stream_location,
        YamlReaderHelper::read_optional_vector_property<int>(phase_config_map, "fork_stream_ids"),
        stream_nodes));
    phase_config.config.opt_set_has_packer_mcast_opt(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "has_packer_mcast_opt"));
    phase_config.config.opt_set_hw_tilize(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "hw_tilize"));
    phase_config.config.opt_set_incoming_data_noc(
        (YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "incoming_data_noc") == 1)
            ? NOC_ROUTE::NOC1
            : NOC_ROUTE::NOC0);
    phase_config.config.opt_set_input_index(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "input_index"));
    phase_config.config.opt_set_buffer_intermediate(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "intermediate"));
    phase_config.config.opt_set_is_scatter_pack(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "is_scatter_pack"));
    phase_config.config.opt_set_legacy_pack(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "legacy_pack"));
    phase_config.config.opt_set_linked(YamlReaderHelper::read_optional_property<bool>(phase_config_map, "linked"));
    phase_config.config.opt_set_local_receiver(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "local_receiver"));
    phase_config.config.opt_set_local_receiver_tile_clearing(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "local_receiver_tile_clearing"));
    phase_config.config.opt_set_local_sources_connected(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "local_sources_connected"));
    phase_config.config.opt_set_local_stream_clear_num(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "local_stream_clear_num"));
    phase_config.config.opt_set_mblock_k(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "mblock_k"));
    phase_config.config.opt_set_mblock_m(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "mblock_m"));
    phase_config.config.opt_set_mblock_n(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "mblock_n"));
    phase_config.config.opt_set_moves_raw_data(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "moves_raw_data"));
    phase_config.config.opt_set_msg_info_buf_addr(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "msg_info_buf_addr"));
    phase_config.config.opt_set_msg_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "msg_size"));
    phase_config.config.opt_set_ncrisc_clear(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "ncrisc_clear"));
    phase_config.config.opt_set_next_phase_dest_change(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "next_phase_dest_change"));
    phase_config.config.opt_set_next_phase_src_change(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "next_phase_src_change"));
    phase_config.config.opt_set_num_mcast_dests(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_mcast_dests"));
    phase_config.config.opt_set_num_fork_streams(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_fork_streams"));
    phase_config.config.opt_set_num_iters_in_epoch(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_iters_in_epoch"));
    phase_config.config.opt_set_num_mblock_buffering(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_mblock_buffering"));
    phase_config.config.opt_set_num_msgs(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_msgs"));
    phase_config.config.opt_set_num_msgs_in_block(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_msgs_in_block"));
    phase_config.config.opt_set_num_scatter_inner_loop(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_scatter_inner_loop"));
    phase_config.config.opt_set_num_unroll_iter(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "num_unroll_iter"));
    phase_config.config.opt_set_outgoing_data_noc(
        (YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "outgoing_data_noc") == 1)
            ? NOC_ROUTE::NOC1
            : NOC_ROUTE::NOC0);
    phase_config.config.opt_set_output_index(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "output_index"));
    phase_config.config.opt_set_overlay_blob_extra_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "overlay_blob_extra_size"));
    phase_config.config.opt_set_padding_scatter_order_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "padding_scatter_order_size"));
    phase_config.config.opt_set_phase_auto_advance(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "phase_auto_advance"));
    phase_config.config.opt_set_phase_auto_config(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "phase_auto_config"));
    phase_config.config.opt_set_pipe_id(
        YamlReaderHelper::read_optional_property<uint64_t>(phase_config_map, "pipe_id"));
    phase_config.config.opt_set_pipe_scatter_output_loop_count(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "pipe_scatter_output_loop_count"));
    phase_config.config.opt_set_producer_epoch_id(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "producer_epoch_id"));
    phase_config.config.opt_set_ptrs_not_zero(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "ptrs_not_zero"));
    phase_config.config.opt_set_r_dim_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "r_dim_size"));
    phase_config.config.opt_set_receiver_endpoint(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "receiver_endpoint"));
    phase_config.config.opt_set_reg_update_vc(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "reg_update_vc"));
    phase_config.config.opt_set_remote_receiver(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "remote_receiver"));
    phase_config.config.opt_set_remote_source(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "remote_source"));
    phase_config.config.opt_set_remote_src_is_mcast(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "remote_src_is_mcast"));
    phase_config.config.opt_set_resend(YamlReaderHelper::read_optional_property<bool>(phase_config_map, "resend"));
    phase_config.config.opt_set_scatter_idx(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "scatter_idx"));
    phase_config.config.opt_set_scatter_list_indicies_per_input(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "scatter_list_indicies_per_input"));
    phase_config.config.opt_set_scatter_list_indicies_per_tile(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "scatter_list_indicies_per_tile"));
    phase_config.config.opt_set_scatter_list_stream_id(get_stream_node_opt(
        stream_location,
        YamlReaderHelper::read_optional_property<int>(phase_config_map, "scatter_list_stream_id"),
        stream_nodes));
    phase_config.config.opt_set_scatter_order_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "scatter_order_size"));
    phase_config.config.opt_set_skip_col_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "skip_col_bytes"));
    phase_config.config.opt_set_skip_col_row_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "skip_col_row_bytes"));
    phase_config.config.opt_set_skip_col_tile_row_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "skip_col_tile_row_bytes"));
    phase_config.config.opt_set_skip_col_zrow_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "skip_col_zrow_bytes"));
    phase_config.config.opt_set_skip_zcol_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "skip_zcol_bytes"));
    phase_config.config.opt_set_source_endpoint(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "source_endpoint"));
    phase_config.config.opt_set_space_shared_with_operand(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "space_shared_with_operand"));
    phase_config.config.opt_set_space_shared_with_stream(get_stream_node_opt(
        stream_location,
        YamlReaderHelper::read_optional_property<int>(phase_config_map, "space_shared_with_stream"),
        stream_nodes));
    phase_config.config.opt_set_source(get_stream_node_opt(
        YamlReaderHelper::read_optional_property<std::string>(phase_config_map, "src"), stream_nodes));
    phase_config.config.opt_set_src_dest_index(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "src_dest_index"));
    phase_config.config.opt_set_stride(YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "stride"));
    phase_config.config.opt_set_stride_offset_size_bytes(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "stride_offset_size_bytes"));
    phase_config.config.opt_set_tile_dim_c(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "tile_dim_c"));
    phase_config.config.opt_set_tile_dim_r(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "tile_dim_r"));
    phase_config.config.opt_set_tilize_row_col_offset(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "tilize_row_col_offset"));
    phase_config.config.opt_set_total_strides(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "total_strides"));
    phase_config.config.opt_set_ublock_ct(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "ublock_ct"));
    phase_config.config.opt_set_ublock_rt(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "ublock_rt"));
    phase_config.config.opt_set_unroll_iter(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "unroll_iter"));
    phase_config.config.opt_set_use_ethernet_fw_stream(
        YamlReaderHelper::read_optional_property<bool>(phase_config_map, "use_ethernet_fw_stream"));
    phase_config.config.opt_set_vc(YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "vc"));
    phase_config.config.opt_set_zc_dim_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "zc_dim_size"));
    phase_config.config.opt_set_zr_dim_size(
        YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "zr_dim_size"));
    phase_config.config.opt_set_remote_src_update_noc(
        (YamlReaderHelper::read_optional_property<uint32_t>(phase_config_map, "remote_src_update_noc") == 1)
            ? NOC_ROUTE::NOC1
            : NOC_ROUTE::NOC0);

    return phase_config;
}

}  // namespace blobgen2