// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/l1/tile_header_buffers_allocation.h"
#include "device/soc_info.h"
#include "model/rational_graph/rational_graph.h"
#include "model/typedefs.h"
// clang-format on

namespace pipegen2
{

class CoreResources;
class WorkerCoreResources;
class EthernetCoreResources;
class StreamNode;
class L1Buffer;

class ResourceManager
{
public:
    // Constructs resource manager based on SoC info.
    ResourceManager(std::unique_ptr<SoCInfo> soc_info);

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    ~ResourceManager();

    // Allocates stream on a given worker core for transfer from the packer and returns its ID.
    StreamId allocate_packer_stream(const tt_cxy_pair& core_physical_location, int operand_id) const;

    // Allocates stream on a given worker core for transfer to the unpacker and returns its ID.
    StreamId allocate_unpacker_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id) const;

    // Allocates intermediate stream on a given worker core and returns its ID.
    StreamId allocate_intermed_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id) const;

    // Allocates stream on a given core with capability of gathering and returns its ID.
    StreamId allocate_gather_stream(const tt_cxy_pair& core_physical_location) const;

    // Allocates stream on a given core with capability of multicasting and returns its ID.
    StreamId allocate_multicast_stream(const tt_cxy_pair& core_physical_location) const;

    // Allocates packer-multicast stream on a given worker core and returns its ID.
    StreamId allocate_packer_multicast_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id) const;

    // Allocates extra stream on a given core with general purpose capabilities and returns its ID.
    StreamId allocate_general_purpose_stream(const tt_cxy_pair& core_physical_location) const;

    // Allocates stream on a given ethernet core with capability of transferring data over ethernet and returns its
    // ID.
    StreamId allocate_ethernet_stream(const tt_cxy_pair& eth_core_physical_location) const;

    // Allocates space in L1 memory of each core on each chip, reserved for message info buffer tile headers.
    // By default each core gets a predefined segment in L1 memory for tile headers buffer, but if there are multiple
    // different tile sizes on a core, then we need to allocate extra space in L1's data buffers segment for all of
    // them. `thb_allocation_info` contains information about which cores need tile header buffers and how many.
    void allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) const;

    // Allocates stream buffer in core's L1. Returns memory allocation object.
    const L1Buffer* allocate_l1_stream_buffer(const StreamNode* stream_node, const unsigned int buffer_size) const;

    // Allocates fallback buffer in core's L1 for NCRISC FW to use when it runs out of L0 space. Returns memory
    // allocation object.
    const L1Buffer* allocate_l1_ncrisc_fallback_buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_size) const;

    // If given blob size is greater than minimum blob size required for a given core allocates extra space and returns
    // allocation object. Extra space for overlay blob can be allocated only once per core.
    const L1Buffer* allocate_l1_extra_overlay_blob_space(
        const tt_cxy_pair& core_physical_location, const unsigned int total_blob_size) const;

    // Allocates kernel input on a given core and returns its index.
    unsigned int allocate_kernel_input(const tt_cxy_pair& core_physical_location) const;

    // Allocates kernel output on a given core and returns its index.
    unsigned int allocate_kernel_output(const tt_cxy_pair& core_physical_location) const;

    // Returns SoC info.
    const SoCInfo* get_soc_info() const { return m_soc_info.get(); }

    // Returns whether a given core is an Ethernet core.
    bool is_ethernet_core(const tt_cxy_pair& core_physical_location) const
    {
        return m_soc_info->is_ethernet_core(core_physical_location);
    }

    // Returns total number of multicast streams available on a core with given location.
    unsigned int get_multicast_streams_count(const tt_cxy_pair& core_physical_location) const;

    // Check if L1 memory is overflowing on a particular core.
    void check_if_out_of_l1_data_buffers_memory(const tt_cxy_pair& core_physical_location) const;

    // Checks if rational graph satisfies various resource constraints.
    void validate_rational_graph_resources(const RationalGraph* rational_graph) const;

    // Returns mapping from physical location to WorkerCoreResources object allocated on that location.
    const std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>>&
    get_worker_core_resources_per_physical_location() const
    {
        return m_worker_cores_resources;
    }

    // Sets OP name for core resource located on core_location.
    void set_core_resource_op_name(const tt_cxy_pair& core_location, const std::string op_name) const;

    // Returns tile header buffer address for a given core and tile size.
    unsigned int get_tile_header_buffer_address(
        const tt_cxy_pair& core_physical_location, unsigned int tile_size) const;

private:
    // Gets worker core resources for a given worker core location.
    WorkerCoreResources* get_worker_core_resources(const tt_cxy_pair& core_physical_location) const;

    // Gets ethernet core resources for a given ethernet core location.
    EthernetCoreResources* get_ethernet_core_resources(const tt_cxy_pair& core_physical_location) const;

    // Gets core resources for a given core location.
    CoreResources* get_core_resources(const tt_cxy_pair& core_physical_location) const;

    // Holds all chip info.
    std::unique_ptr<SoCInfo> m_soc_info;

    // Map of resources for each worker core on each chip, mapped by their physical coordinates.
    std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>> m_worker_cores_resources;

    // Map of resources for each eth core on each chip, mapped by their physical coordinates.
    std::unordered_map<tt_cxy_pair, std::unique_ptr<EthernetCoreResources>> m_ethernet_cores_resources;

    // Allocation strategy chosen in constructor for tile header buffer allocation.
    std::unique_ptr<THBAllocationStrategy> m_thb_allocation_strategy;
};

}  // namespace pipegen2
