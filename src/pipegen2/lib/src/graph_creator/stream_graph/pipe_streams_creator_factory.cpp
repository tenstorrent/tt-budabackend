// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creator_factory.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dormant_relay_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_gather_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_embedding_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_multicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_output_intermed_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_parallel_fork_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_post_tm_gather_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_post_tm_unicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_pre_tm_parallel_fork_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_tilizer_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/dram_unicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/ethernet_fw_relay_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/ethernet_relay_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/gather_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/gather_to_dram_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/gather_to_pcie_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/multicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/non_shared_intermed_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/padding_serial_fork_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/parallel_fork_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/pcie_multicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/pcie_unicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/relay_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/serial_fork_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/shared_packer_intermed_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_to_pcie_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_to_dram_streams_creator.h"
#include "graph_creator/stream_graph/pipe_streams_creators/union_streams_creator.h"
#include "model/rational_graph/pipes/direct/dram_output_intermed_pipe.h"
#include "model/rational_graph/pipes/direct/dram_tilizer_pipe.h"
#include "model/rational_graph/pipes/direct/dram_unicast_pipe.h"
#include "model/rational_graph/pipes/direct/ethernet_fw_relay_pipe.h"
#include "model/rational_graph/pipes/direct/ethernet_relay_pipe.h"
#include "model/rational_graph/pipes/direct/non_shared_intermed_pipe.h"
#include "model/rational_graph/pipes/direct/pcie_unicast_pipe.h"
#include "model/rational_graph/pipes/direct/relay_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_to_dram_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_to_pcie_pipe.h"
#include "model/rational_graph/pipes/fork/dram_multicast_pipe.h"
#include "model/rational_graph/pipes/fork/dram_parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/dram_prefetch_pre_tm_parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/multicast_pipe.h"
#include "model/rational_graph/pipes/fork/padding_serial_fork_pipe.h"
#include "model/rational_graph/pipes/fork/parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/pcie_multicast_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"
#include "model/rational_graph/pipes/join/dormant_relay_pipe.h"
#include "model/rational_graph/pipes/join/dram_gather_pipe.h"
#include "model/rational_graph/pipes/join/dram_embedding_pipe.h"
#include "model/rational_graph/pipes/join/gather_pipe.h"
#include "model/rational_graph/pipes/join/gather_to_dram_pipe.h"
#include "model/rational_graph/pipes/join/gather_to_pcie_pipe.h"
#include "model/rational_graph/pipes/join/shared_packer_intermed_pipe.h"
#include "model/rational_graph/pipes/join/union_pipe.h"

namespace pipegen2
{
    PipeStreamsCreatorFactory::PipeStreamsCreatorFactory(tt::ARCH device_arch) :
        m_device_arch(device_arch)
    {
    }

    PipeStreamsCreator* PipeStreamsCreatorFactory::get_pipe_streams_creator(
        const RGBasePipe* pipe,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node)
    {
        return get_pipe_streams_creator(pipe->get_pipe_type(), resource_manager, virt_node_to_stream_node);
    }

    PipeStreamsCreator* PipeStreamsCreatorFactory::get_pipe_streams_creator(
        RGPipeType pipe_type,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node)
    {
        auto const& creator_it = m_creators_map.find(pipe_type);

        return creator_it == m_creators_map.end() ?
            m_creators_map.emplace(
                pipe_type,
                create_pipe_streams_creator(pipe_type, resource_manager, virt_node_to_stream_node)).first->second.get() :
            creator_it->second.get();
    }

    std::unique_ptr<PipeStreamsCreator> PipeStreamsCreatorFactory::create_pipe_streams_creator(
        RGPipeType pipe_type,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node)
    {
        if (pipe_type == RGPipeType::DramUnicast)
        {
            return create_generic_pipe_streams_creator<DramUnicastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramTilizer)
        {
            return create_generic_pipe_streams_creator<DramTilizerStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::Unicast)
        {
            return create_generic_pipe_streams_creator<UnicastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::UnicastToDram)
        {
            return create_generic_pipe_streams_creator<UnicastToDramStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::NonSharedIntermed)
        {
            return create_generic_pipe_streams_creator<NonSharedIntermedStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramOutputIntermed)
        {
            return create_generic_pipe_streams_creator<DramOutputIntermedStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::SharedPackerIntermed)
        {
            return create_generic_pipe_streams_creator<SharedPackerIntermedStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramMulticast)
        {
            return create_generic_pipe_streams_creator<DramMulticastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::Multicast)
        {
            return create_generic_pipe_streams_creator<MulticastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramParallelFork)
        {
            return create_generic_pipe_streams_creator<DramParallelForkStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramPrefetchPreTMParallelFork)
        {
            return create_generic_pipe_streams_creator<DramPrefetchPreTMParallelForkStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramPrefetchPostTMUnicast)
        {
            return create_generic_pipe_streams_creator<DramPrefetchPostTMUnicastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::ParallelFork)
        {
            return create_generic_pipe_streams_creator<ParallelForkStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::SerialFork)
        {
            return create_generic_pipe_streams_creator<SerialForkStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::PaddingSerialFork)
        {
            return create_generic_pipe_streams_creator<PaddingSerialForkStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramGather)
        {
            return create_generic_pipe_streams_creator<DramGatherStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramPrefetchPostTMGather)
        {
            return create_generic_pipe_streams_creator<DramPrefetchPostTMGatherStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DramEmbedding)
        {
            return create_generic_pipe_streams_creator<DramEmbeddingStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::Gather)
        {
            return create_generic_pipe_streams_creator<GatherStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::GatherToDram)
        {
            return create_generic_pipe_streams_creator<GatherToDramStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::Relay)
        {
            return create_generic_pipe_streams_creator<RelayStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::PCIeUnicast)
        {
            check_for_supported_architecture({tt::ARCH::GRAYSKULL});

            return create_generic_pipe_streams_creator<PCIeUnicastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::UnicastToPCIe)
        {
            check_for_supported_architecture({tt::ARCH::GRAYSKULL});

            return create_generic_pipe_streams_creator<UnicastToPCIeStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::PCIeMulticast)
        {
            check_for_supported_architecture({tt::ARCH::GRAYSKULL});

            return create_generic_pipe_streams_creator<PCIeMulticastStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::GatherToPCIe)
        {
            check_for_supported_architecture({tt::ARCH::GRAYSKULL});

            return create_generic_pipe_streams_creator<GatherToPCIeStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::EthernetRelay)
        {
            return create_generic_pipe_streams_creator<EthernetRelayStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::EthernetFWRelay)
        {
            return create_generic_pipe_streams_creator<EthernetFWRelayStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::DormantRelay)
        {
            return create_generic_pipe_streams_creator<DormantRelayStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else if (pipe_type == RGPipeType::Union)
        {
            return create_generic_pipe_streams_creator<UnionStreamsCreator>(
                resource_manager, virt_node_to_stream_node);
        }
        else
        {
            throw std::logic_error("PipeStreamsCreatorFactory: Unsupported rational graph pipe type");
        }
    }

    void PipeStreamsCreatorFactory::check_for_supported_architecture(
        const std::unordered_set<tt::ARCH>& supported_architectures)
    {
        if (supported_architectures.find(m_device_arch) == supported_architectures.end())
        {
            throw std::logic_error(
                "PipeStreamsCreatorFactory: Unsupported rational graph pipe type for this device architecture");
        }
    }
}