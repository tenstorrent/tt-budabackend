// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router_types.h"
#include "core_resource_tracker.h"

#include <ostream>

namespace router {
BufferAttributes merge(const BufferAttributes &a, const BufferAttributes &b) {
  auto merged = BufferAttributes();
  merged.set_outputs_to_dram(a.get_num_outputs_to_dram() + b.get_num_outputs_to_dram());
  if (a.has_input_from_dram() || b.has_input_from_dram()) {
    merged.set_input_from_dram();
  }
  merged.set_multicast(a.get_num_multicasts() + b.get_num_multicasts());
  merged.set_ethernet_io_streams(a.get_num_ethernet_io_streams() + b.get_num_ethernet_io_streams());

  return merged;
}

bool operator==(buffer_queue_info_t const& lhs, buffer_queue_info_t const& rhs) {
    return lhs.get_parent_queue_id() == rhs.get_parent_queue_id() && lhs.get_allocation_info() == rhs.get_allocation_info();
}

bool router_buffer_info_t::operator==(router_buffer_info_t const& rhs) const {
    bool same_type = this->is_queue() == rhs.is_queue();
    bool same_info = same_type && (this->is_queue() ? this->queue_info() == rhs.queue_info() : this->info() == rhs.info());
    bool same_core = this->_core_location == rhs._core_location;
    return same_info && same_core;
}


bool is_gather_pipe(const pipe_t &p) {
    return p.input_buffer_ids.size() > 1 && !p.is_scatter() && p.output_buffer_ids().size() == 1;
}
bool is_multicast_pipe(const pipe_t &p) {
    return p.input_buffer_ids.size() == 1 && !p.is_scatter() && p.output_buffer_ids().size() > 1 && !p.is_scatter();
}
bool is_scatter_pipe(const pipe_t &p) {
    return p.is_scatter();
}
bool is_gather_multicast_pipe(const pipe_t &p) {
    return p.input_buffer_ids.size() > 1 && !p.is_scatter() && p.output_buffer_ids().size() > 1;
}
bool is_unicast_pipe(const pipe_t &p) {
    return p.input_buffer_ids.size() == 1 && !p.is_scatter() && p.output_buffer_ids().size() == 1;
}

std::ostream &operator<<(std::ostream &os, const BufferAllocationFailureReason& reason) {
    switch (reason) {
        break; case BufferAllocationFailureReason::Insufficient_L1:  { os << "Insufficient_L1"; }; 
        break; case BufferAllocationFailureReason::Insufficient_DRAM_Input_Streams:  { os << "Insufficient_DRAM_Input_Streams"; }; 
        break; case BufferAllocationFailureReason::Insufficient_DRAM_Output_Streams:  { os << "Insufficient_DRAM_Output_Streams"; }; 
        break; case BufferAllocationFailureReason::Insufficient_Multicast_Streams:  { os << "Insufficient_Multicast_Streams"; }; 
        break; case BufferAllocationFailureReason::Insufficient_Relay_Streams:  { os << "Insufficient_Relay_Streams"; }; 
        break; case BufferAllocationFailureReason::Insufficient_Ethernet_Streams:  { os << "Insufficient_Ethernet_Streams"; }; 
        break; case BufferAllocationFailureReason::Insufficient_Extra_Streams:  { os << "Insufficient_Extra_Streams"; };
        default: log_fatal("Case not implemented");
    };

    return os;
}

void log_buffer_allocation_failure_reasons(HwCoreAttributes const& core_attributes, const std::vector<BufferAllocationFailureReason> &reasons) {
    auto ss = std::stringstream();
    for (const auto &reason : reasons) {
        ss << "| " << reason << " |";
        if (reason == BufferAllocationFailureReason::Insufficient_Relay_Streams) {
            auto const& relay_buffer_ids = core_attributes.get_relay_buffers();
            ss << "\n\tRelay buffer ids: ";
            for (unique_id_t buffer_id : relay_buffer_ids) {
                ss << " " << buffer_id << ", ";
            }
            ss << "\n";
        }
    }
    log_debug(tt::LogRouter, "Buffer allocation failure reasons: {}", ss.str());
}

}; // namespace router
