// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources.h"

#include "l1_address_map.h"
#include "noc/noc_overlay_parameters.h"
#include "stream_io_map.h"

#include "device/operand_stream_map.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    
WorkerCoreResources::WorkerCoreResources(const tt_cxy_pair& core_physical_location) :
    CoreResources(core_physical_location,
                  static_cast<StreamId>(END_IO_STREAM + 1),
                  static_cast<StreamId>(NOC_NUM_STREAMS - 1),
                  l1_mem::address_map::DATA_BUFFER_SPACE_BASE,
                  l1_mem::address_map::MAX_SIZE)
{
}

StreamId WorkerCoreResources::allocate_packer_stream(int operand_id)
{
    if (!OperandStreamMap::is_output_operand(operand_id))
    {
        ERROR("WorkerCoreResources: Trying to allocate packer stream on a worker core " <<
              get_physical_location().str() << " with invalid operand ID: " << operand_id);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        ERROR("WorkerCoreResources: Trying to allocate packer stream on a worker core " <<
              get_physical_location().str() << ", with operand ID: " << operand_id <<
              ", which was already allocated");
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_unpacker_stream(int operand_id)
{
    if (!OperandStreamMap::is_input_operand(operand_id))
    {
        ERROR("WorkerCoreResources: Trying to allocate unpacker stream on a worker core " <<
              get_physical_location().str() << " with invalid operand ID: " << operand_id);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        ERROR("WorkerCoreResources: Trying to allocate unpacker stream on a worker core " <<
              get_physical_location().str() << ", with operand ID: " << operand_id <<
              ", which was already allocated");
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_intermed_stream(int operand_id)
{
    if (!OperandStreamMap::is_intermediate_operand(operand_id))
    {
        ERROR("WorkerCoreResources: Trying to allocate intermed stream on a worker core " <<
              get_physical_location().str() << " with invalid operand ID: " << operand_id);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        ERROR("WorkerCoreResources: Trying to allocate intermed stream on a worker core " <<
              get_physical_location().str() << ", with operand ID: " << operand_id <<
              ", which was already allocated");
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_packer_multicast_stream(int operand_id)
{
    StreamId stream_id = get_packer_multicast_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        ERROR("WorkerCoreResources: Trying to allocate packer-multicast stream on a worker core " <<
              get_physical_location().str() << ", with operand ID: " << operand_id <<
              ", which was already allocated");
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::get_next_available_general_purpose_stream_id()
{
    if (m_next_available_extra_stream_id > get_extra_streams_id_range_end())
    {
        ERROR("WorkerCoreResources: Out of available extra streams on a worker core " <<
              get_physical_location().str());
    }

    StreamId stream_id = m_next_available_extra_stream_id++;

    return stream_id;
}

} // namespace pipegen2