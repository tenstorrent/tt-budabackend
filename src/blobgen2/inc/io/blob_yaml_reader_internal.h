// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>

#include "yaml-cpp/yaml.h"

#include "model/stream_graph/base_stream_node.h"
#include "model/stream_graph/base_stream_destination.h"
#include "model/stream_graph/base_stream_source.h"
#include "model/stream_graph/dram_config.h"
#include "model/stream_graph/sending_stream_node.h"
#include "model/stream_graph/stream_graph.h"

namespace blobgen2::internal
{

    // Read phases from the yaml map into the stream graph structure.
    void read_phases(const YAML::Node& blob_yaml,
                     std::unordered_map<std::string, pipegen2::BaseStreamNode*>& streams_cache,
                     pipegen2::StreamGraph *const stream_graph);

    // Read dram blobs from the yaml map into the stream graph structure.
    void read_dram_blob(const YAML::Node& blob_yaml,
                        std::unordered_map<std::string, pipegen2::BaseStreamNode*>& streams_cache,
                        pipegen2::StreamGraph *const stream_graph);

    // Read overlay blob extra size property from the yaml map into the stream graph structure.
    void read_overlay_blob_extra_size(const YAML::Node& blob_yaml, pipegen2::StreamGraph *const stream_graph);

    // Read all the stream properties, create and return the stream node of a type determined by the properties.
    std::unique_ptr<pipegen2::BaseStreamNode> create_stream(const YAML::Node& stream_map, const std::string& id_str);

    // Read all dram buffer config properties from the yaml stream map, create and return the dram config object. 
    pipegen2::DramConfig create_dram_config(const YAML::Node& dram_map, unsigned int buffer_id);

    // Read phase properties from the yaml stream map, create and return the phase object. 
    pipegen2::Phase create_phase(const YAML::Node& stream_map, const pipegen2::PhaseId phase_id);

    // Read stream source properties and return the object based on the source type (endpoint, local, remote).
    std::unique_ptr<pipegen2::BaseStreamSource> create_stream_source(const YAML::Node& stream_map);

    // Read stream destination properties and return the object based on the destination type (endpoint, local, remote).
    std::unique_ptr<pipegen2::BaseStreamDestination> create_stream_destination(const YAML::Node& stream_map);

    //TODO: Fill in.
    std::optional<pipegen2::SendingStreamNode::TilizedDataConfig> read_optional_tilized_data_config(
        const YAML::Node& stream_map);

    //TODO: Fill in.
    std::optional<pipegen2::SendingStreamNode::BufferDimensions> read_optional_buffer_dimensions(
        const YAML::Node& stream_map);

    // Extract chip identificator from a system wide stream id such as: 'chip_1__y_7__x_6__stream_id_24'.
    unsigned int extract_chip_id(const std::string& system_wide_stream_id);

    // Extract column coordinate of the tensix core from a stream id string such as: 'chip_1__y_7__x_6__stream_id_24'.
    unsigned int extract_tensix_coord_y(const std::string& system_wide_stream_id);

    // Extract row coordinate of the tensix core from a stream id string such as: 'chip_1__y_7__x_6__stream_id_24'.
    unsigned int extract_tensix_coord_x(const std::string& system_wide_stream_id);

    // Extract id of the stream within a tensix core from a stream id string such as: 'chip_1__y_7__x_6__stream_id_24'.
    unsigned int extract_stream_id(const std::string& system_wide_stream_id);

    // Read and return a mandatory scalar property of the given type, from the yaml::node map using the given key.
    template <typename T>
    T read_property(const YAML::Node& sequence, const std::string& key);

    // Read and return a mandatory vector property of the given type, from the yaml::node map using the given key.
    template <typename T>
    std::vector<T> read_vector_property(const YAML::Node& sequence, const std::string& key);

    // Read and return an optional scalar property of the given type, from the yaml::node map using the given key.
    template <typename T>
    std::optional<T> read_optional_property(const YAML::Node& sequence, const std::string& key);

    // Read and return an optional vector property of the given type, from the yaml::node map using the given key.
    template <typename T>
    std::optional<std::vector<T>> read_optional_vector_property(const YAML::Node& sequence, const std::string& key);

    // Convert phase id string into the datatype representing the phase id.
    pipegen2::PhaseId get_phase_id(const std::string& phase_id_str);

    // Convert yaml::node type into a string.
    std::string get_node_type_string(const YAML::NodeType::value& node_type);

}