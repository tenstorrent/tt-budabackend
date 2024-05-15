// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "device/perf_info_manager.h"
#include "model/stream_graph/phase.h"
#include "model/stream_graph/stream_graph.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "model/typedefs.h"

namespace pipegen2
{
    class BlobYamlWriter
    {
    public:
        // Constructor. Opens output file stream.
        BlobYamlWriter(const std::string& blob_yaml_path);

        // Destructor. Releases output file.
        ~BlobYamlWriter();

        // Writes stream graphs to the blob yaml.
        void write_blob_yaml(const StreamGraphCollection* stream_graph_collection,
                             const PerfInfoManager& perf_info_manager);

    private:
        // Writes NCRISC configs to the blob yaml.
        void write_ncrisc_configs(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

        // Writes stream phase configs to the blob yaml.
        void write_phase_configs(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

        // Collects stream configs mapped by phase id.
        std::map<PhaseId, std::map<tt_cxys_pair, StreamConfig>> collect_phase_map(
            const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

        // Writes phase map to the blob yaml.
        void write_phase_map(const std::map<PhaseId, std::map<tt_cxys_pair, StreamConfig>>& phase_map);

        // Writes DRAM performance info to the blob yaml.
        void write_dram_perf_info(const PerfInfoManager& perf_info_manager);

        // Goes through all the streams in the stream graphs and fills phases with same id.
        // Returns a map, mapping phase id to a map mapping stream ids having a phase with that id
        // to their respective StreamConfigs.
        void collect_stream_graph_phases(
            const StreamGraph* stream_graph,
            std::map<PhaseId, std::map<tt_cxys_pair, StreamConfig>>& phase_map);

        // Returns list of ncrisc configs grouped by stream id key. Map (instead of unordered_map) needs to be used
        // to keep deterministic output.
        std::map<tt_cxys_pair, const std::vector<NcriscConfig>*>
        collect_ncrisc_configs(const StreamGraph* stream_graph);

        // Writes string representation of a dram blob into blob yaml.
        void write_ncrisc_params(const NcriscConfig& dram_blob);

        // Writes string representation of stream config into blob yaml.
        void write_stream_params(const StreamConfig& stream_config, const PhaseId phase_id);

        // Returns string representation of core's (chip, x, y) location.
        static std::string get_core_location_string(const tt_cxy_pair& core_location);

        // Returns string representation of stream node.
        static std::string get_stream_node_string(const tt_cxys_pair& stream_location);

        // Writes parameter string to the blob yaml if parameter has a value, with an optional converter function
        // for the value.
        template <typename T>
        void write_optionally(std::string&& name,
                              const std::optional<T>& opt,
                              std::string (*converter_f)(const T&) = nullptr);

        // Writes parameter string to the blob yaml, with an optional converter function for the value.
        template <typename T>
        void write_param(std::string&& name,
                         const T& value,
                         std::string (*converter_f)(const T&) = nullptr);

        // Returns string representation of stream node.
        static std::string get_string(const StreamNode* stream_node);

        // Returns string representation of a given boolean value.
        static std::string get_string(bool value);

        // Returns string representation of noc route.
        static std::string get_string(NOC_ROUTE value);

        // Overloaded function to return string from string argument. It does nothing smart, but it is needed to
        // compile.
        static std::string get_string(const std::string& value);

        // Returns string representation of a given value. It's enabled if std::to_string conversion works for the type.
        // For the types not supported by std::to_string, we need to overload the function.
        template<typename T>
        static typename std::enable_if<
            std::is_constructible<
                std::string,
                decltype(std::to_string(std::declval<T>()))
            >::value,
            std::string
        >::type get_string(T value);

        // Returns [elem_1, elem_2, ...] string representation of a vector. If converter function is provided then it
        // will be invoked on every element, otherwise raw element value will be used.
        template<typename T>
        static std::string get_string(const std::vector<T>& values,
                                      std::string (*converter_f)(const T&) = nullptr);

        // Returns hexadecimal string representation of a number.
        static std::string get_hex_string(const unsigned int& number);

        // Returns hexadecimal string representation of a number.
        static std::string get_hex_string(const std::uint64_t& number);

        // Returns hexadecimal string representation of a vector of numbers.
        static std::string get_hex_string(const std::vector<std::uint64_t>& numbers);

        // Returns string representation of a stream ID.
        static std::string get_stream_id_string(StreamNode* const & stream_node);

        // Returns string representation of a stream IDs list.
        static std::string get_stream_ids_string(const std::vector<StreamNode*>& stream_nodes);

        // Returns string representation of a stream destination list.
        static std::string get_stream_dest_string(const std::vector<StreamNode*>& stream_dest);

        // Starting line for dram blob in blob yaml file.
        static std::string s_dram_blob_yaml_line;

        // Starting line for perf dump section in blob yaml file.
        static std::string s_dram_perf_dump_line;

        // Perf buf NOC addres line start.
        static std::string s_dram_perf_buf_noc_addr;

        // Perf buf max required mem size line start.
        static std::string s_dram_perf_buf_max_req;

        // Starting line prefix for each phase in blob yaml file.
        static std::string s_phase_blob_yaml_prefix;

        // Output blob yaml file stream;
        std::ofstream m_blob_yaml_file_stream;

        // Current indentation level for writing in blob yaml.
        unsigned int m_current_indentation_level;

        // Current indentation string for writing in blob yaml.
        std::string m_current_indentation;

        // Adds indentation to blob yaml output lines.
        class IndentYaml
        {
        public:
            IndentYaml(BlobYamlWriter* blob_yaml_writer);

            ~IndentYaml();

        private:
            void update_indentation();

            static constexpr unsigned int c_indentation_increment = 2;

            BlobYamlWriter* m_blob_yaml_writer;
        };
    };
}