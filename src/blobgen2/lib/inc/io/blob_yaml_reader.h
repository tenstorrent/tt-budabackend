// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io/yaml_reader_helper.h"
#include "model/typedefs.h"
#include "overlay_blob/typedef.h"

namespace pipegen2
{
struct L1BufferAllocationInfo;
class StreamGraphCollection;
class NcriscConfig;
class PhaseConfig;
class StreamNode;
}  // namespace pipegen2

namespace blobgen2
{

using pipegen2::L1BufferAllocationInfo;
using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;
using pipegen2::PhaseId;
using pipegen2::StreamGraphCollection;
using pipegen2::StreamNode;
using pipegen2::tt_cxys_pair;

// Class used for reading a blob yaml file into a StreamGraphCollection.
// This is somewhat of an inverse of pipegen2::BlobYamlWriter
class BlobYamlReader
{
public:
    // Reads a StreamGraphCollection from the blob yaml file.
    static std::pair<std::unique_ptr<StreamGraphCollection>, std::map<tt_cxy_pair, dram_perf_info_t>> read_blob_yaml(
        const std::string blob_yaml_path);

private:
    // Creates empty StreamNodes for all streams that are found in the blob yaml file.
    // It's important to do this first, so that when we read config fields which point to another StreamNode, we can
    // reference it.
    static std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>> read_all_stream_ids(
        const YAML::Node blob_yaml);

    // Fills NcriscConfigs from dram_blob section of the blob yaml file.
    static void fill_dram_blob(
        const YAML::Node blob_yaml, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Fills PhaseConfigs from phase_* section of the blob yaml file.
    static void fill_phase_configs(
        const YAML::Node blob_yaml, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Fills Perf dump info from dram_perf_dump_blob of the blob yaml file.
    static std::map<tt_cxy_pair, dram_perf_info_t> fill_dram_perf_dump_blob(const YAML::Node blob_yaml);

    // Fills Ncrisc fallback buffers info from global_info_blob of the blob yaml file.
    static std::map<tt_cxy_pair, L1BufferAllocationInfo> fill_ncrisc_fallback_buffers(const YAML::Node blob_yaml);

    // Fills all the read configs into a StreamGraphCollection which is returned from this class.
    static std::unique_ptr<StreamGraphCollection> fill_stream_graph_collection(
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes,
        std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers);

    // Helper function which adds a StreamNode to the stream_nodes map if it doesn't already exist.
    static void add_stream_node_if_not_exists(
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes,
        const tt_cxys_pair& system_wide_stream_id);

    // Helper function which executes any function for each item in dram_blob section of the blob yaml file.
    // This section consists of stream ids as keys, and vector of NcriscConfig as values.
    static void foreach_dram_blob(
        const YAML::Node blob_yaml, std::function<void(const tt_cxys_pair&, const YAML::Node)> func);

    // Helper function which executes any function for each item found in phase_ sections of the blob yaml file.
    // This section consists of stream ids nested under phase ids, holding PhaseConfig as values.
    static void foreach_phase_config(
        const YAML::Node blob_yaml, std::function<void(const tt_cxys_pair&, const PhaseId&, const YAML::Node)> func);

    // Helper function which executes any function for each item found in dram_perf_dump_blob section of the blob yaml
    // file. This section consists of core ides as keys, and perf info arrays as values.
    static void foreach_dram_perf_dump_blob(
        const YAML::Node blob_yaml,
        std::function<void(const tt_cxy_pair&, std::vector<uint64_t>&, std::vector<uint16_t>&)> func);

    // Helper function which executes any function for each item found in global_info_blob section of the blob yaml
    // file. This section consists of core ides as keys, and ncrisc fallback buffer info as values.
    static void foreach_ncrisc_fallback_buffers(
        const YAML::Node blob_yaml, std::function<void(const tt_cxy_pair&, L1BufferAllocationInfo)> func);

    // Helper function to extracts chip, x, y, and stream id from string in commonly found format in blob yaml.
    // Expected format is chip_1__y_2__x_7__stream_id_40
    static tt_cxys_pair extract_stream_info(const std::string& system_wide_stream_id);

    // Helper function to extracts chip, x, and y from string in commonly found format in blob yaml.
    // Expected format is chip_1__y_2__x_7
    static tt_cxy_pair extract_core_info(const std::string& system_wide_core_id);

    // Helper function to extract phase id from "phase_*" string.
    static PhaseId get_phase_id(const std::string& phase_id_str);

    // Helper function to obtain an optional StreamNode pointer from the optional string with stream system wide string.
    // Expects the same format as extract_stream_info.
    static std::optional<StreamNode*> get_stream_node_opt(
        const std::optional<std::string> stream_id,
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Helper function to obtain an optional StreamNode pointer from the optional stream_id and using provided system
    // wide stream id location of the originating stream.
    static std::optional<StreamNode*> get_stream_node_opt(
        const tt_cxys_pair& original_stream_location,
        const std::optional<int>& stream_id,
        const std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Helper function to get an optional vector of StreamNode pointers from the optional vector of stream system wide
    // strings.
    static std::optional<std::vector<StreamNode*>> get_stream_nodes_opt(
        const std::optional<std::vector<std::string>>& stream_ids,
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Helper function to get an optional vector of StreamNode pointers from the optional vector stream ids and using
    // provided system wide stream id location of the originating stream.
    static std::optional<std::vector<StreamNode*>> get_stream_nodes_opt(
        const tt_cxys_pair& original_stream_location,
        const std::optional<std::vector<int>>& stream_ids,
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Extract a single NcriscConfig from the blob yaml node.
    static NcriscConfig extract_ncrisc_config(
        const YAML::Node dram_config_map, std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);

    // Extract a single PhaseConfig from the blob yaml node.
    static PhaseConfig extract_phase_config(
        const tt_cxys_pair& stream_location,
        const YAML::Node phase_config_map,
        std::unordered_map<tt_cxys_pair, std::unique_ptr<StreamNode>>& stream_nodes);
};

}  // namespace blobgen2
