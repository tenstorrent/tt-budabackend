// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "netlist/tt_backend_api_types.hpp"

#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator_bh.h"
#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator_gs.h"
#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator_wh.h"
#include "graph_creator/stream_graph/stream_creators/stream_creator_bh.h"
#include "graph_creator/stream_graph/stream_creators/stream_creator_gs.h"
#include "graph_creator/stream_graph/stream_creators/stream_creator_wh.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipe_streams_creators/pipe_streams_creator.h"

namespace pipegen2
{
    class PipeStreamsCreatorFactory
    {
    public:
        // Constructs factory based on specific device architecture.
        explicit PipeStreamsCreatorFactory(tt::ARCH device_arch);

        // Returns appropriate pipe streams creator instance for specific rational graph pipe.
        // Creates pipe streams creator instance if it wasn't already previously created.
        PipeStreamsCreator* get_pipe_streams_creator(
            const RGBasePipe* pipe,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

    private:
        // Returns appropriate pipe streams creator instance for specific rational graph pipe type.
        // Creates pipe streams creator instance if it wasn't already previously created.
        PipeStreamsCreator* get_pipe_streams_creator(
            RGPipeType pipe_type,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

        // Creates pipe streams creator for specific rational graph pipe type.
        std::unique_ptr<PipeStreamsCreator> create_pipe_streams_creator(
            RGPipeType pipe_type,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

        // Creates pipe stream creators which are generic (same class used for each device architecture).
        template <class T>
        std::unique_ptr<PipeStreamsCreator> create_generic_pipe_streams_creator(
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node)
        {
            switch (m_device_arch)
            {
            case tt::ARCH::GRAYSKULL:
                return std::make_unique<T>(std::make_unique<NcriscCreatorGS>(),
                                           std::make_unique<StreamCreatorGS>(),
                                           resource_manager,
                                           virt_node_to_stream_node);
            case tt::ARCH::WORMHOLE:
            case tt::ARCH::WORMHOLE_B0:
                return std::make_unique<T>(std::make_unique<NcriscCreatorWH>(),
                                           std::make_unique<StreamCreatorWH>(),
                                           resource_manager,
                                           virt_node_to_stream_node);
            case tt::ARCH::BLACKHOLE:
                return std::make_unique<T>(std::make_unique<NcriscCreatorBH>(),
                                           std::make_unique<StreamCreatorBH>(),
                                           resource_manager,
                                           virt_node_to_stream_node);
            default:
                throw std::logic_error("PipeStreamsCreatorFactory: Unsupported device architecture");
            }
        }

        // Checks if device architecture is among given supported architectures and throws error if it isn't.
        // Used for instantiating pipe streams creators supported only for specific set of architectures.
        void check_for_supported_architecture(const std::unordered_set<tt::ARCH>& supported_architectures);

        // Map of created pipe streams creators for each rational graph pipe type.
        std::unordered_map<RGPipeType, std::unique_ptr<PipeStreamsCreator>> m_creators_map;

        // Device architecture.
        tt::ARCH m_device_arch;
    };
}