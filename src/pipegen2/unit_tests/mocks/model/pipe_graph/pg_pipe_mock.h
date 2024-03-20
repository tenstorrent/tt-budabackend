// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/pipe_graph/pg_pipe.h"
#include "test_utils/unit_test_utils.h"

namespace pipegen2
{

class PGPipeMock : public PGPipe
{

public:
    PGPipeMock(const NodeId id)
    {
        set_id(id);
    }

    PGPipeMock(
        const NodeId id,
        const unsigned int periodic_repeat,
        const unsigned int consumer_repeat,
        const int ethernet_chan,
        const int incoming_noc_id,
        const int incoming_vc,
        const int outgoing_noc_id,
        const int outgoing_vc,
        const int mmio_pipe,
        const int mmio_pipe_downstream,
        const int ethernet_pipe,
        const int dis_gather_opt,
        const int direct_mcast,
        const unsigned int io_buf_size_tiles,
        const std::vector<NodeId>& mcast_core_rc,
        const std::vector<int>& dram_pipe_total_readers,
        const std::vector<int>& dram_pipe_reader_index,
        const std::vector<NodeId>& input_list,
        const std::vector<NodeId>& output_list,
        const std::vector<NodeId>& output_padding_list)    
    {
        set_id(id);
        set_pipe_periodic_repeat(periodic_repeat);
        set_consumer_repeat(consumer_repeat);
        set_ethernet_channel(ethernet_chan);
        set_incoming_noc_id(incoming_noc_id);
        set_incoming_noc_vc(incoming_vc);
        set_outgoing_noc_id(outgoing_noc_id);
        set_outgoing_noc_vc(outgoing_vc);
        set_is_mmio_pipe(mmio_pipe == 1);
        set_is_mmio_pipe_downstream(mmio_pipe_downstream == 1);
        set_is_ethernet_pipe(ethernet_pipe == 1);
        set_gather_optimization_disabled(dis_gather_opt == 1);
        set_packer_multicast_optimization_enabled(direct_mcast == 1);
        set_op_input_dram_io_buf_size_tiles(io_buf_size_tiles);
        set_dram_pipe_total_readers(dram_pipe_total_readers);
        set_dram_pipe_reader_index(dram_pipe_reader_index);
        set_input_buffers_ids(input_list);
        set_output_padding_buffers_ids(output_padding_list);
        set_output_buffers_ids({output_list});
        add_mcast_core_logical_location(tt_cxy_pair(mcast_core_rc[0], mcast_core_rc[1], mcast_core_rc[2]));
    }

    std::vector<std::string> to_json_list_of_strings_all_attributes()
    {
        return {
            "pipe_"+ std::to_string(get_id()) + ":",
            "id: " + std::to_string(get_id()),
            "pipe_periodic_repeat: " + std::to_string(get_pipe_periodic_repeat()),
            "pipe_consumer_repeat: " + std::to_string(get_consumer_repeat()),
            "ethernet_chan: " + std::to_string(get_ethernet_channel()),
            "incoming_noc_id: " + std::to_string(get_incoming_noc_id()),
            "incoming_vc: " + std::to_string(get_incoming_noc_vc()),
            "outgoing_noc_id: " + std::to_string(get_outgoing_noc_id()),
            "outgoing_vc: " + std::to_string(get_outgoing_noc_vc()),
            "mmio_pipe: " + std::to_string(is_mmio_pipe() ? 1 : 0),
            "mmio_pipe_downstream: " + std::to_string(is_mmio_pipe_downstream() ? 1 : 0),
            "ethernet_pipe: " + std::to_string(is_ethernet_pipe() ? 1 : 0),
            "dis_gather_opt: " + std::to_string(is_gather_optimization_disabled() ? 1 : 0),
            "direct_mcast: " + std::to_string(is_packer_multicast_optimization_enabled() ? 1 : 0),
            "op_input_dram_io_buf_size_tiles: " + std::to_string(get_op_input_dram_io_buf_size_tiles()),
            "mcast_core_rc: " + unit_test_utils::convert_to_string(
                flatten_vector_of_locations(get_mcast_core_logical_locations())), 
            "dram_pipe_total_readers: " + unit_test_utils::convert_to_string( 
                get_dram_pipe_total_readers()), 
            "dram_pipe_reader_index: " + unit_test_utils::convert_to_string( 
                get_dram_pipe_reader_index()), 
            "input_list: " + unit_test_utils::convert_to_string(get_input_buffers_ids()), 
            "output_list: " + convert_nested_lists_to_string(get_output_buffers_ids()), 
            "output_padding_list: " + unit_test_utils::convert_to_string(get_output_padding_buffers_ids()), 
        };
    }

    std::vector<std::string> to_json_list_of_strings_id_only()
    {
        return {
            "pipe_"+ std::to_string(get_id()) + ":",
            "id: " + std::to_string(get_id())
        };
    }
    
private:
    static std::vector<std::uint64_t> flatten_vector_of_locations(const std::vector<tt_cxy_pair>& vector_of_locations)
    {
        std::vector<std::uint64_t> flattened_vector;
        for (const tt_cxy_pair& location : vector_of_locations)
        {
            flattened_vector.push_back(location.chip);
            flattened_vector.push_back(location.y);
            flattened_vector.push_back(location.x);
        }
        return flattened_vector;
    }

    static std::string convert_nested_lists_to_string(const std::vector<std::vector<std::uint64_t>>& input_vector)
    {
        std::ostringstream oss;
        oss << "[";
        for (int i = 0; i < input_vector.size(); i++)
        {
            oss << unit_test_utils::convert_to_string(input_vector[i]);
            if (i != input_vector.size()-1) 
            {
                oss << ", ";
            }
        }

        oss << "]";
        return oss.str();
    }

};

} // namespace pipegen2