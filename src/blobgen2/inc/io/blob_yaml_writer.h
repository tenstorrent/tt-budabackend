// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "model/stream_graph/phase.h"
#include "model/stream_graph/stream_graph.h"

namespace pipegen2
{
    class EndpointStreamDestination;
    class EndpointStreamSource;
    class LocalStreamDestination;
    class LocalStreamSource;
    class ReceivingStreamNode;
    class RemoteStreamDestination;
    class RemoteStreamSource;
    class SendingStreamNode;

    class BlobYamlWriter
    {
    public:
        // Constructor.
        BlobYamlWriter(const std::string& blob_yaml_path);

        // Writes stream graphs to the blob yaml.
        void write_blob_yaml(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

    private:
        // Goes through all the streams in the stream graphs and collects phases with same id.
        // Returns a map, mapping phase id to vector of all the streams having a phase with that id
        // and index of phase with that id in the stream.
        static std::map<PhaseId, std::vector<std::pair<BaseStreamNode*, int>>> collect_stream_graphs_phases(
            const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

        // Returns string representation of a dram blob.
        static std::string get_string(const DramConfig& dram_config);

        // Returns string representation of a stream node and it's phase at a given index.
        static std::string get_string(const BaseStreamNode* stream_node, std::size_t phase_index);

        // Returns vector of a dram buffer config parameters turned into string in form "key: value".
        static std::vector<std::string> get_dram_config_params(const DramConfig& dram_config);

        // Returns vector of stream parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_params(const BaseStreamNode* stream_node);

        // Returns vector of sending stream parameters turned into string in form "key: value".
        static std::vector<std::string> get_sending_stream_params(const SendingStreamNode* stream_node);

        // Returns vector of receiving stream parameters turned into string in form "key: value".
        static std::vector<std::string> get_receiving_stream_params(const ReceivingStreamNode* stream_node);

        // Returns vector of stream source parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_source_params(const BaseStreamSource* stream_source);

        // Returns vector of stream endpoint source parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_endpoint_source_params(const EndpointStreamSource* stream_source);

        // Returns vector of stream local source parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_local_source_params(const LocalStreamSource* stream_source);

        // Returns vector of stream remote source parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_remote_source_params(const RemoteStreamSource* stream_source);

        // Returns vector of stream destination parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_destination_params(const BaseStreamDestination* stream_destination);

        // Returns vector of stream endpoint destination parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_endpoint_destination_params(const EndpointStreamDestination* stream_destination);

        // Returns vector of stream local destination parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_local_destination_params(const LocalStreamDestination* stream_destination);

        // Returns vector of stream remote destination parameters turned into string in form "key: value".
        static std::vector<std::string> get_stream_remote_destination_params(const RemoteStreamDestination* stream_destination);

        // Returns vector of phase parameters turned into string in form "key: value".
        static std::vector<std::string> get_phase_params(const Phase& phase);

        // Returns string representation of a system-wide unique stream id: chip id, tensix coordinates, and stream id.
        static std::string get_system_wide_stream_id(const pipegen2::BaseStreamNode *const stream);

        // Returns hexadecimal string representation of a given number.
        static std::string get_hex_string(int64_t number);

        // Returns string representation of a given boolean value.
        static std::string get_string(bool value);

        // Returns string representation of a given vector of T values.
        template <typename T> static std::string get_string(const std::vector<T>& values);

        // Returns hexadecimal string representation of a given vector of T values.
        template <typename T> static std::string get_hex_string(const std::vector<T>& values);

        // Starting line for dram blob in blob yaml file.
        static std::string s_dram_blob_yaml_line;

        // Starting line prefix for each phase in blob yaml file.
        static std::string s_phase_blob_yaml_prefix;

        // Starting line key for the overlay blob extra size property in blob yaml file.
        static std::string s_overlay_blob_extra_size_blob_yaml_key;

        // Output blob yaml path.
        std::string m_blob_yaml_path;
    };
}