// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "base_types.hpp"
#include "netlist_utils.hpp"
#include "router.hpp"
#include "common/size_lib.hpp"
#include "router/router_passes_common.h"
#include "net2pipe.h"

#include <limits>
#include <numeric>
#include <cstdio>

namespace router {

  static constexpr int REG_UPDATE_VC = 3;
  static constexpr int TENSIX_DATA_UNICAST_VC = 2;
  static constexpr int TENSIX_DATA_MULTICAST_VC = 4;
  
  static constexpr int DRAM_DATA_WR_VC = 0;

  enum class DRAMRoutingScheme {Proximity, Read_NOC0_Write_NOC1, WHA0_Disable_Relays};

  bool enable_wha0_noc1_q_reads = std::getenv("WHA0_ENABLE_RELAY_BUFS") != nullptr && (std::stoi(std::getenv("WHA0_ENABLE_RELAY_BUFS")) != 0);
  bool disable_wha0_noc1_q_reads = !enable_wha0_noc1_q_reads;

  struct op_input_pipe_info_t {
    unique_id_t pipe_id;
    bool row_mcast;
    bool col_mcast;
    bool direct_mcast;
    bool op_input_loopback;
  };

  inline DRAMRoutingScheme get_dram_routing_scheme(const Router &router) {
    switch (router.get_soc_descriptor().arch) {
      // FIXME imatosevic - revise this, esp. for GS
    case tt::ARCH::GRAYSKULL:
      return DRAMRoutingScheme::Proximity;
    case tt::ARCH::WORMHOLE:
      return disable_wha0_noc1_q_reads ? DRAMRoutingScheme::WHA0_Disable_Relays : DRAMRoutingScheme::Proximity;
    case tt::ARCH::WORMHOLE_B0:
      return DRAMRoutingScheme::Proximity;
    default:
      log_fatal("unrecognized architecture");
    }
  }
  
  bool is_ethernet_pipe(const Router &router, const pipe_t &pipe) {
    if (router.get_soc_descriptor().ethernet_cores.size() == 0) {
      return false;
    }
    bool is_unicast =
      pipe.input_buffer_ids.size() == 1 && (!pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1);
    if (is_unicast) {
      std::size_t source_chip = router.get_buffer(pipe.input_buffer_ids.at(0)).core_location().chip;
      std::size_t dest_chip = router.get_buffer(pipe.output_buffer_ids().at(0)).core_location().chip;
      bool dest_is_sys_dram =
        (router.get_buffer_map().at(pipe.output_buffer_ids().at(0)).core_location().x == 255) &&
        (router.get_buffer_map().at(pipe.output_buffer_ids().at(0)).core_location().y == 255);
      return !dest_is_sys_dram && (source_chip != dest_chip);
    } else {
      return false;
    }
  }


  bool op2op_pipe_all_inputs_same_core(const Router &router, const pipe_t &pipe) {
    bool start = true;
    tt_xy_pair first_input_core_location;
    for (unique_id_t curr_input_buffer_id : pipe.input_buffer_ids) {
      const router_buffer_info_t &curr_input_buffer = router.get_buffer(curr_input_buffer_id);
      TT_ASSERT(curr_input_buffer.core_location_assigned());    
      if (start) {
        start = false;
        first_input_core_location = curr_input_buffer.core_location();
      } else if (curr_input_buffer.core_location() != first_input_core_location) {
        return false;
      }
    }
    return true;
  }
    
  bool in_coord_interval(int start, int end, int n) {
    if (end < start) { // wrap
      return (n >= start) || (n <= end);
    } else {
      return (n >= start) && (n <= end);
    }
  }

  void check_op_xy_overlap(const Router& router, const tt_op_info &producer_op, const tt_op_info& consumer_op,
                           int& x_overlap_size, int &y_overlap_size,
                           int& consumer_pre_overlap_size, int& consumer_post_overlap_size) {
    
    int tensix_grid_size_x = router.get_soc_descriptor().worker_log_to_routing_x.size();
    int tensix_grid_size_y = router.get_soc_descriptor().worker_log_to_routing_y.size();

    int producer_x_start = producer_op.grid_loc_x();
    int producer_y_start = producer_op.grid_loc_y();
    int producer_x_end = producer_op.grid_end_x();
    int producer_y_end = producer_op.grid_end_y();

    consumer_pre_overlap_size = 0;
    consumer_post_overlap_size = 0;

    x_overlap_size = 0;
    y_overlap_size = 0;
    for (int cx = 0; cx < consumer_op.grid_size_x(); cx++) {
      int consumer_x = (consumer_op.grid_loc_x() + cx) % tensix_grid_size_x;
      if (in_coord_interval(producer_x_start, producer_x_end, consumer_x)) {
        if (x_overlap_size == 0) {
          consumer_pre_overlap_size = cx;
        }
        x_overlap_size++;
      } else if (x_overlap_size > 0) {
        consumer_post_overlap_size++;
      }
    }
    for (int cy = 0; cy < consumer_op.grid_size_y(); cy++) {
      int consumer_y = (consumer_op.grid_loc_y() + cy) % tensix_grid_size_y;
      if (in_coord_interval(producer_y_start, producer_y_end, consumer_y)) {
        if (y_overlap_size == 0) {
          consumer_pre_overlap_size = cy;
        }
        y_overlap_size++;
      } else if (y_overlap_size > 0) {
        consumer_post_overlap_size++;
      }
    }

    TT_ASSERT((x_overlap_size == 0) || (y_overlap_size == 0));    
  }
  
  bool check_noc_bbox_fit(int bbox_noc0_y_start, int bbox_noc0_x_start, int bbox_noc0_y_end, int bbox_noc0_x_end,
                          int pipe_noc_id,
                          int source_side_y, int source_side_x, int dest_side_y, int dest_side_x) {

    bool y_bbox_fit, x_bbox_fit;
    if (pipe_noc_id == 0) {
      y_bbox_fit =
        in_coord_interval(bbox_noc0_y_start, bbox_noc0_y_end, source_side_y) &&
        in_coord_interval(source_side_y, bbox_noc0_y_end, dest_side_y);
      x_bbox_fit = in_coord_interval(bbox_noc0_x_start, bbox_noc0_x_end, source_side_x) &&
        in_coord_interval(source_side_x, bbox_noc0_x_end, dest_side_x);
    } else {
      y_bbox_fit =
        in_coord_interval(bbox_noc0_y_start, bbox_noc0_y_end, source_side_y) &&
        in_coord_interval(bbox_noc0_y_start, source_side_y, dest_side_y);
      x_bbox_fit =
        in_coord_interval(bbox_noc0_x_start, bbox_noc0_x_end, source_side_x) &&
        in_coord_interval(bbox_noc0_x_start, source_side_x, dest_side_x);
    }
    return y_bbox_fit && x_bbox_fit;
  }
      
  void get_pipe_properties(const Router &router,
                           unique_id_t pipe_unique_id,
                           const pipe_t &pipe,
                           bool& ethernet_pipe,
                           bool& pcie_pipe,
                           bool& source_is_queue,
                           bool& source_is_prolog_queue,
                           bool& source_is_relay,
                           bool& source_is_prolog_inter,
                           bool& source_is_op,
                           std::string& source_name,
                           bool& dest_is_queue,
                           bool& dest_is_relay,
                           bool& dest_is_prolog_inter,
                           bool& dest_is_op,
                           int& dest_input_index,
                           std::string& dest_name,
                           bool& untilize_output,
                           bool& row_mcast,
                           bool& col_mcast,
                           bool& op_input_pipe_noc1_output,
                           bool& direct_mcast,
                           bool& op_input_loopback) {

    ethernet_pipe = false;
    pcie_pipe = false;
    dest_is_queue = false;
    dest_is_prolog_inter = false;
    dest_is_op = false;
    source_is_queue = false;
    source_is_prolog_queue = false;
    source_is_prolog_inter = false;
    source_is_relay = false;
    source_is_op = false;
    dest_input_index = 0;
    dest_name = "";
    source_name = "";
    std::string dest_type = "";
    std::string source_type = "";
    untilize_output = false;
    row_mcast = false;
    col_mcast = false;
    op_input_pipe_noc1_output = false;
    direct_mcast = false;
    op_input_loopback = false;

    // FIXME imatosevic: add assertions that input/output types are consistent
    router::unique_id_t first_input_buffer_id = pipe.input_buffer_ids.at(0);
    const router_buffer_info_t &first_input_buffer = router.get_buffer(first_input_buffer_id);

    unique_id_t first_output_buffer_id;
    if (pipe.has_multiple_timesteps()) {
      first_output_buffer_id = pipe.time_multiplexed_output_buffer_ids().at(0).at(0);
    } else {
      first_output_buffer_id = pipe.output_buffer_ids().at(0);
    }
    const router_buffer_info_t &first_output_buffer = router.get_buffer(first_output_buffer_id);

    std::size_t source_device = first_input_buffer.core_location().chip;
    std::size_t dest_device = first_output_buffer.core_location().chip;

    ethernet_pipe = is_ethernet_pipe(router, pipe);
    pcie_pipe = false;
    if (ethernet_pipe) {
      source_name = "<ETH>";
      dest_name = "<ETH>";
    }
    else {
      source_is_queue = first_input_buffer.is_queue();

      // We mark all ethernet buffers as relay so we avoid making any assumptions about op layout, even for ethernet
      // datacopy
      source_is_relay =
          !source_is_queue && ((first_input_buffer.info().type() == tt::RouterBufferType::Relay) ||
                               router.get_soc_descriptor().is_ethernet_core(first_input_buffer.core_location()));
      source_is_prolog_inter = !source_is_queue && (first_input_buffer.info().type() == tt::RouterBufferType::PrologInter);
      if (source_is_queue) {
        const buffer_queue_info_t& input_queue = first_input_buffer.queue_info();
        const tt_queue_info& input_queue_info = router.get_queue(input_queue.get_parent_queue_id());
        source_is_prolog_queue = input_queue.is_prolog();
        source_type = "queue ";
        source_name = input_queue_info.name;
      } else if (source_is_prolog_inter) {
        source_name = "<PROLOG INTER>";
      } else if (source_is_relay) {
        source_name = "<RELAY>";
      } else {
        const std::string& source_op_name = router.get_op_output_buffer_op_name_map().at(router.get_scatter_buffer_base_id(first_input_buffer_id));
        source_type = "op ";
        source_name = source_op_name;
        source_is_op = true;
        const tt_op_info &source_op_info = router.get_op_info_map().at(source_op_name);
        untilize_output = source_op_info.untilize_output;
      }
      dest_is_queue = first_output_buffer.is_queue();
      // We mark all ethernet buffers as relay so we avoid making any assumptions about op layout, even for ethernet
      // datacopy
      dest_is_relay =
          !dest_is_queue && ((first_output_buffer.info().type() == tt::RouterBufferType::Relay) ||
                             router.get_soc_descriptor().is_ethernet_core(first_output_buffer.core_location()));
      dest_is_prolog_inter = !dest_is_queue && (first_output_buffer.info().type() == tt::RouterBufferType::PrologInter);
      if (dest_is_queue) {
        dest_type = "queue ";
        const buffer_queue_info_t& output_queue = first_output_buffer.queue_info();
        const tt_queue_info& output_queue_info = router.get_queue(output_queue.get_parent_queue_id());
        dest_name = output_queue_info.name;
        if (output_queue_info.loc == QUEUE_LOCATION::HOST) {
          pcie_pipe = true;
        } else if (router.get_soc_descriptor().ethernet_cores.size() == 0) {
          // pcie pipes are used on WH only for sending to host memory
          pcie_pipe = (source_device != dest_device);
        } else {
          pcie_pipe = false;
        }
      } else if (dest_is_relay) {
        dest_name = "<RELAY>";
      } else if (dest_is_prolog_inter) {
        dest_name = "<PROLOG INTER>";
      } else {
        dest_type = "op ";
        dest_is_op = true;
        const std::string& dest_op_name = router.get_op_input_buffer_op_name_map().at(first_output_buffer_id);
        const tt_op_info &dest_op_info = router.get_op_info_map().at(dest_op_name);
        dest_name = dest_op_name;
        dest_input_index = router.get_op_input_buffer_index_map().at(first_output_buffer_id);
        bool noc1_mcast;
        n2p::get_op_input_mcast(dest_op_info, dest_input_index, router.get_fused_op_schedule_map(),
                                row_mcast, col_mcast, noc1_mcast);        
        op_input_pipe_noc1_output = 
          noc1_mcast || 
          (!row_mcast && !col_mcast && (dest_op_info.grid_transpose ^ (dest_input_index % 2))); // FIXME imatosevic - revise the second term that covers non-multicast relays

        int num_phased_outputs = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().size() : 1;
        bool pipe_outputs_mcast = false;          
        for (int p = 0; p < num_phased_outputs; p++) {
          const std::vector<router::unique_id_t>& curr_phased_outputs_ids =
            pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(p) : pipe.output_buffer_ids();
          bool output_mcast = (curr_phased_outputs_ids.size() > 1);
          if (p == 0) {
            pipe_outputs_mcast = output_mcast;
            if (output_mcast) {
              TT_ASSERT(row_mcast || col_mcast, std::string("Op ") + dest_op_name + " input " + std::to_string(dest_input_index)  + ": illegal pipe multicast output");
            }
          } else {
            TT_ASSERT(output_mcast == pipe_outputs_mcast, std::string("Op ") + dest_op_name + " input " + std::to_string(dest_input_index)  + ": phased pipe with inconsistent output multicast");
          }
        }
        row_mcast &= pipe_outputs_mcast;
        col_mcast &= pipe_outputs_mcast;

        if (router.get_soc_descriptor().ethernet_cores.size() == 0) {
          // pcie pipes are used on WH only for sending to host memory
          pcie_pipe = (source_device != dest_device);
        } else {
          pcie_pipe = false;
        }

        int direct_mcast_noc = 0;
        bool adjacent_mcast = false;
        bool op2op_pipe_no_gather =false;
        if (!pcie_pipe && source_is_op) {
          op2op_pipe_no_gather = op2op_pipe_all_inputs_same_core(router, pipe);
          const tt_op_info &source_op_info = router.get_op_info_map().at(source_name);
          adjacent_mcast = n2p::producer_size_and_placement_for_direct_mcast(router.get_soc_descriptor().arch,
                                                                            source_op_info, dest_op_info,
                                                                            row_mcast, col_mcast, noc1_mcast,
                                                                            router.get_soc_descriptor().worker_grid_size.x, router.get_soc_descriptor().worker_grid_size.y,
                                                                            direct_mcast_noc);
          direct_mcast = adjacent_mcast && (num_phased_outputs == 1) && op2op_pipe_no_gather;
          if (direct_mcast) {
            op_input_pipe_noc1_output = (direct_mcast_noc == 1);
          }
        }

        op_input_loopback = 
          ((row_mcast || col_mcast) && !direct_mcast) ||
          (num_phased_outputs != 1) ||
          (!op2op_pipe_no_gather);
      }
    }

    n2p::Log() << "Pipe info id=" << pipe_unique_id << ":\n"
              << "    source = " << source_type << source_name << "\n"
              << "    dest = " << dest_type << dest_name << "\n"
              << "    dest_input_index = " << dest_input_index << "\n"
              << "    pcie_pipe = " << pcie_pipe << "\n"
              << "    source_is_queue = " << source_is_queue << "\n"
              << "    source_is_prolog_queue = " << source_is_prolog_queue << "\n"
              << "    source_is_prolog_inter = " << source_is_prolog_inter << "\n"
              << "    dest_is_queue = " << dest_is_queue << "\n"
              << "    dest_is_prolog_inter = " << dest_is_prolog_inter << "\n"
              << "    untilize_output = " << untilize_output << "\n";
  }
  
  void pipe_route_nocs(Router &router, unique_id_t pipe_id, pipe_t& pipe, 
                       std::map<std::string, std::map<int, op_input_pipe_info_t>>& op_input_pipe_info_map) {

    pipe.incoming_noc_id = 0;
    pipe.outgoing_noc_id = 0;
    pipe.incoming_vc = 0;
    pipe.outgoing_vc = 0;

    bool ethernet_pipe;
    bool pcie_pipe;
    bool dest_is_queue;
    bool source_is_queue;
    bool source_is_prolog_queue;
    bool source_is_prolog_inter;
    int dest_input_index;
    bool source_is_relay;
    bool source_is_op;
    bool dest_is_relay;
    bool dest_is_prolog_inter;
    bool dest_is_op;
    std::string dest_name;
    std::string source_name;
    bool untilize_output;
    bool row_mcast;
    bool col_mcast;
    bool op_input_pipe_noc1_output;
    bool direct_mcast;
    bool op_input_loopback;
      
    get_pipe_properties(router, pipe_id, pipe,
                        ethernet_pipe, pcie_pipe, 
                        source_is_queue, source_is_prolog_queue,
                        source_is_relay, source_is_prolog_inter, source_is_op,
                        source_name,
                        dest_is_queue, dest_is_relay, dest_is_prolog_inter, dest_is_op,
                        dest_input_index, dest_name, 
                        untilize_output,
                        row_mcast, col_mcast, op_input_pipe_noc1_output, direct_mcast,
                        op_input_loopback);

    if (dest_is_op) {
      op_input_pipe_info_t op_input_pipe_info;
      op_input_pipe_info.pipe_id = pipe_id;
      op_input_pipe_info.row_mcast = row_mcast;
      op_input_pipe_info.col_mcast = col_mcast;
      op_input_pipe_info.direct_mcast = direct_mcast;
      op_input_pipe_info.op_input_loopback = op_input_loopback;
      op_input_pipe_info_map[dest_name][dest_input_index] = op_input_pipe_info;
    }

    pipe.direct_mcast = direct_mcast;

    router::unique_id_t first_input_buffer_id = pipe.input_buffer_ids.at(0);
    const router_buffer_info_t &first_input_buffer = router.get_buffer(first_input_buffer_id);

    unique_id_t first_output_buffer_id;
    if (pipe.has_multiple_timesteps()) {
      first_output_buffer_id = pipe.time_multiplexed_output_buffer_ids().at(0).at(0);
    } else {
      first_output_buffer_id = pipe.output_buffer_ids().at(0);
    }
    const router_buffer_info_t &first_output_buffer = router.get_buffer(first_output_buffer_id);

    if (ethernet_pipe) {
      // NOC allocation not needed for ethernet pipes
    } 
    else if (pcie_pipe) {
      // FIXME imatosevic - temporary solution
      // need to figure out how this core interacts with the subsequent pipe staging in pipegen
      pipe.incoming_noc_id = 0;
      pipe.incoming_vc = DRAM_DATA_WR_VC; // PCIE can issue static-VC writes only on VC 0
      if (dest_is_queue) {
        pipe.outgoing_noc_id = 0;
        pipe.outgoing_vc = DRAM_DATA_WR_VC;
      } else {
        pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;      
        pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;
      }
    }
    // Remaining cases - single-chip pipes
    else if (dest_is_queue) {
      pipe.incoming_vc = pipe.outgoing_vc = DRAM_DATA_WR_VC;
      // simple case: need only outgoing NOC ID, no reblock/TM pipes
      if (untilize_output) {
        // untilizing firmware works only with NOC0 (FIXME imatosevic - do we need to revise this?)
        pipe.incoming_noc_id = 0;        
        pipe.outgoing_noc_id = 0;
      } else {
        DRAMRoutingScheme routing_scheme = get_dram_routing_scheme(router);
        if ((routing_scheme == DRAMRoutingScheme::Proximity) || (routing_scheme == DRAMRoutingScheme::WHA0_Disable_Relays)) {
          int dest_x_noc0_routing = first_output_buffer.core_location().x;
          int dest_y_noc0_routing = first_output_buffer.core_location().y;
          int src_x_noc0_routing = first_input_buffer.core_location().x;
          int src_y_noc0_routing = first_input_buffer.core_location().y;
          pipe.incoming_noc_id = pipe.outgoing_noc_id = router.get_shortest_path_noc_id(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing);
        }
        else if (routing_scheme == DRAMRoutingScheme::Read_NOC0_Write_NOC1) {
          pipe.incoming_noc_id = 1;        
          pipe.outgoing_noc_id = 1;
        }
        else {
          TT_ASSERT(false);
        }
      }
    }
    else if (source_is_queue) {
      if (source_is_prolog_queue) {
        if (dest_is_prolog_inter) {
          pipe.incoming_noc_id = 0;        
          pipe.outgoing_noc_id = 0;
          pipe.incoming_vc = pipe.outgoing_vc = 0;
        } else {
          pipe.incoming_noc_id = op_input_pipe_noc1_output ? 0 : 1;
          pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;      
          pipe.incoming_vc = TENSIX_DATA_UNICAST_VC;
          pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;
        }
      }
      else {
        
        // Source is streaming DRAM queue.
        DRAMRoutingScheme routing_scheme = get_dram_routing_scheme(router);
        if (routing_scheme == DRAMRoutingScheme::Proximity) {
          int src_x_noc0_routing = first_input_buffer.core_location().x;
          int src_y_noc0_routing = first_input_buffer.core_location().y;
          pipe.incoming_noc_id = router.get_shortest_path_noc_id((int)src_y_noc0_routing, (int)src_x_noc0_routing, (int)pipe.core_locations().at(0).y, (int)pipe.core_locations().at(0).x);
        } else if (routing_scheme == DRAMRoutingScheme::WHA0_Disable_Relays) {
          pipe.incoming_noc_id = 0;
        } else if (routing_scheme == DRAMRoutingScheme::Read_NOC0_Write_NOC1) {
          pipe.incoming_noc_id = 0;
        }        
        if (!dest_is_relay) {
          pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;              
        }
        pipe.incoming_vc = TENSIX_DATA_UNICAST_VC;
        pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;
      }
    }
    else if (source_is_prolog_inter) {
      pipe.incoming_noc_id = op_input_pipe_noc1_output ? 0 : 1;
      pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;              
      pipe.incoming_vc = TENSIX_DATA_UNICAST_VC;
      pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;
    }
    else if (source_is_relay || dest_is_relay) {
      // FIXME imatosevic - need to consider different types of relays here?
      int dest_x_noc0_routing = first_output_buffer.core_location().x;
      int dest_y_noc0_routing = first_output_buffer.core_location().y;
      int src_x_noc0_routing = first_input_buffer.core_location().x;
      int src_y_noc0_routing = first_input_buffer.core_location().y;       
      int noc_id = router.get_shortest_path_noc_id(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing);
      pipe.incoming_noc_id = noc_id;
      pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;              
      pipe.incoming_vc = TENSIX_DATA_UNICAST_VC;
      pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;
    }
    else {
      // Same-chip op->op pipe

      pipe.incoming_vc = TENSIX_DATA_UNICAST_VC;
      pipe.outgoing_vc = (row_mcast || col_mcast) ? TENSIX_DATA_MULTICAST_VC : TENSIX_DATA_UNICAST_VC;

      tt_op_info source_op = router.get_op_info_map().at(source_name);
      tt_op_info dest_op = router.get_op_info_map().at(dest_name);

      int source_x_start = source_op.grid_loc_x();
      int source_y_start = source_op.grid_loc_y();
      int dest_x_start = dest_op.grid_loc_x();
      int dest_y_start = dest_op.grid_loc_y();
      int source_x_end = source_op.grid_end_x();
      int source_y_end = source_op.grid_end_y();
      int dest_x_end = dest_op.grid_end_x();
      int dest_y_end = dest_op.grid_end_y();

      int x_overlap_size, y_overlap_size;
      int dest_pre_overlap_size, dest_post_overlap_size;
      check_op_xy_overlap(router, source_op, dest_op,
                          x_overlap_size, y_overlap_size,
                          dest_pre_overlap_size, dest_post_overlap_size);          
        
      bool no_overlap = (x_overlap_size == 0) && (y_overlap_size == 0);
      bool x_overlap_source_subsumes = (x_overlap_size == dest_op.grid_size_x());
      bool x_overlap_dest_subsumes = (x_overlap_size == source_op.grid_size_x());
      bool y_overlap_source_subsumes = (y_overlap_size == dest_op.grid_size_y());
      bool y_overlap_dest_subsumes = (y_overlap_size == source_op.grid_size_y());
      bool x_overlap_partial = (x_overlap_size > 0) && !x_overlap_dest_subsumes && !x_overlap_source_subsumes;
      bool y_overlap_partial = (y_overlap_size > 0) && !y_overlap_source_subsumes && !y_overlap_dest_subsumes;

      if (no_overlap) {
        int noc0_grid_distance = router.get_noc0_distance(source_y_end, source_x_end, dest_y_start, dest_x_start);
        int noc1_grid_distance = router.get_noc1_distance(source_y_start, source_x_start, dest_y_end, dest_x_end);
        int noc_id = (noc0_grid_distance > noc1_grid_distance) ? 1 : 0;
        pipe.incoming_noc_id = noc_id;
        pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;          
        log_debug(tt::LogRouter, "Op->op pipe with no grid coordinate overlap -> use NOC {} between ops\n", noc_id);
      }
      else {
        int grid_to_grid_noc_id;
        // bounding-box specified in noc0 logical coordinates, with stard and end
        // relative to noc0 direction (even if we're using noc1)
        int bbox_noc0_x_start, bbox_noc0_y_start;
        int bbox_noc0_x_end, bbox_noc0_y_end; 
        if (x_overlap_source_subsumes || x_overlap_dest_subsumes) {
          int noc0_y_grid_distance = router.get_noc0_distance(source_y_end, 0, dest_y_start, 0);
          int noc1_y_grid_distance = router.get_noc1_distance(source_y_start, 0, dest_y_end, 0);
          grid_to_grid_noc_id = (noc0_y_grid_distance > noc1_y_grid_distance) ? 1 : 0;
          bbox_noc0_y_start = (grid_to_grid_noc_id == 1) ? dest_y_start : source_y_start;
          bbox_noc0_y_end = (grid_to_grid_noc_id == 1) ? source_y_end : dest_y_end;
          if (x_overlap_source_subsumes) {
            bbox_noc0_x_start = source_x_start;
            bbox_noc0_x_end = source_x_end;
          }
          else { // x_overlap_dest_subsumes
            bbox_noc0_x_start = dest_x_start;
            bbox_noc0_x_end = dest_x_end;              
          }
        }
        else if (x_overlap_partial) {
          TT_ASSERT((dest_pre_overlap_size == 0) || (dest_post_overlap_size == 0));
          TT_ASSERT((dest_pre_overlap_size > 0) || (dest_post_overlap_size > 0));
          if (dest_pre_overlap_size > 0) {
            bbox_noc0_x_start = dest_x_start;
            bbox_noc0_x_end = source_x_end;
            grid_to_grid_noc_id = 1;
            bbox_noc0_y_start = dest_y_start;
            bbox_noc0_y_end = source_y_end;            
          } else {
            bbox_noc0_x_start = source_x_start;
            bbox_noc0_x_end = dest_x_end;            
            grid_to_grid_noc_id = 0;
            bbox_noc0_y_start = source_y_start;
            bbox_noc0_y_end = dest_y_end;            
          }
        }
        else if (y_overlap_source_subsumes || y_overlap_dest_subsumes) {
          int noc0_x_grid_distance = router.get_noc0_distance(source_x_end, 0, dest_x_start, 0);
          int noc1_x_grid_distance = router.get_noc1_distance(source_x_start, 0, dest_x_end, 0);
          grid_to_grid_noc_id = (noc0_x_grid_distance > noc1_x_grid_distance) ? 1 : 0;
          bbox_noc0_x_start = (grid_to_grid_noc_id == 1) ? dest_x_start : source_x_start;
          bbox_noc0_x_end = (grid_to_grid_noc_id == 1) ? source_x_end : dest_x_end;
          if (y_overlap_source_subsumes) {
            bbox_noc0_y_start = source_y_start;
            bbox_noc0_y_end = source_y_end;
          }
          else if (y_overlap_dest_subsumes) {
            bbox_noc0_y_start = dest_y_start;
            bbox_noc0_y_end = dest_y_end;              
          }
        }
        else { // y_overlap_partial
          TT_ASSERT(y_overlap_partial);
          TT_ASSERT((dest_pre_overlap_size == 0) || (dest_post_overlap_size == 0));
          TT_ASSERT((dest_pre_overlap_size > 0) || (dest_post_overlap_size > 0));          
          if (dest_pre_overlap_size > 0) {
            bbox_noc0_y_start = dest_y_start;
            bbox_noc0_y_end = source_y_end;
            grid_to_grid_noc_id = 1;
            bbox_noc0_x_start = dest_x_start;
            bbox_noc0_x_end = source_x_end;            
          } else {
            bbox_noc0_y_start = source_y_start;
            bbox_noc0_y_end = dest_y_end;            
            grid_to_grid_noc_id = 0;
            bbox_noc0_x_start = source_x_start;
            bbox_noc0_x_end = dest_x_end;            
          }
        }
        
        log_debug(tt::LogRouter, "Pipe noc id = {}, bounding box: x={}, y={} -> x={}, y={}\n", grid_to_grid_noc_id,
                bbox_noc0_x_start, bbox_noc0_y_start, bbox_noc0_x_end, bbox_noc0_y_end);

        // check if each pipe data path can be placed into the bounding box
        int num_phased_outputs = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().size() : 1;
        // bool all_inputs_same_core = op2op_pipe_all_inputs_same_core(router, pipe);

        std::map<int, std::map<int, bool>> io_noc_bbox_fit;
        std::vector<bool> output_noc_bbox_fit;
        bool io_noc_bbox_all_fit = true;
        bool pipe_outputs_mcast = row_mcast || col_mcast;
          
        for (int p = 0; p < num_phased_outputs; p++) {
          
          output_noc_bbox_fit.push_back(true);            
          const std::vector<router::unique_id_t>& curr_phased_outputs_ids =
            pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(p) : pipe.output_buffer_ids();

          int curr_pipe_y = router.get_pipe_location_tensix_grid(pipe_id, p).y;
          int curr_pipe_x = router.get_pipe_location_tensix_grid(pipe_id, p).x;
            
          // At this point, the pipe will be co-located with the source core if all_inputs_same_core == true,
          // otherwise it will be co-located with the dest core if unicast, or the start of the multicast
          // row/column.
          int dest_side_x, dest_side_y;
          if (pipe_outputs_mcast) {
            dest_side_x = curr_pipe_x;
            dest_side_y = curr_pipe_y;
          } else {
            dest_side_x = router.get_buffer_location_tensix_grid(curr_phased_outputs_ids.at(0)).x;
            dest_side_y = router.get_buffer_location_tensix_grid(curr_phased_outputs_ids.at(0)).y;
          }

          int i = 0;
          for (unique_id_t curr_input_buffer_id : pipe.input_buffer_ids) {
            // const router_buffer_info_t &curr_input_buffer = router.get_buffer(curr_input_buffer_id);
            int source_side_x = router.get_buffer_location_tensix_grid(curr_input_buffer_id).x;
            int source_side_y = router.get_buffer_location_tensix_grid(curr_input_buffer_id).y;
            io_noc_bbox_fit[p][i] = check_noc_bbox_fit(bbox_noc0_y_start, bbox_noc0_x_start, bbox_noc0_y_end, bbox_noc0_x_end,
                                                       grid_to_grid_noc_id,
                                                       source_side_y, source_side_x, dest_side_y, dest_side_x);
            output_noc_bbox_fit[p] = output_noc_bbox_fit[p] & io_noc_bbox_fit[p][i];
            io_noc_bbox_all_fit = io_noc_bbox_all_fit & io_noc_bbox_fit[p][i];
            log_debug(tt::LogRouter, "Input {}: x={}, y={} -> output {}: x={}, y={} => noc_fit={}\n",
                    curr_input_buffer_id,
                    source_side_x, source_side_y, p, dest_side_x, dest_side_y,
                    io_noc_bbox_fit[i][p]);
                        
          }          
        }
        if (io_noc_bbox_all_fit) {
          pipe.incoming_noc_id = grid_to_grid_noc_id;
          pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;
        } else {
          // FIXME imatosevic - relay insertion goes here
          pipe.incoming_noc_id = grid_to_grid_noc_id;
          pipe.outgoing_noc_id = op_input_pipe_noc1_output ? 1 : 0;          
        }
      }            
    }
  }

  void optimize_op_input_loopback(Router& router, const std::string& op_name, 
                                  std::map<std::string, std::map<int, op_input_pipe_info_t>>& op_input_pipe_info_map) {

    const tt_op_info& op_info = router.get_op_info_map().at(op_name);
    int noc_num_inputs[2] = {0};
    int noc_num_loopbacks[2] = {0};
    int noc_num_unicast_loopbacks[2] = {0};
    for (int input_index = 0; input_index < op_info.input_names.size(); input_index++) {
      op_input_pipe_info_t pipe_info = op_input_pipe_info_map.at(op_info.name).at(input_index);
      pipe_t pipe = router.get_pipes_mutable().at(pipe_info.pipe_id);
      if (pipe_info.op_input_loopback) {
        noc_num_loopbacks[pipe.outgoing_noc_id]++;
        bool mcast = !pipe_info.direct_mcast && (pipe_info.row_mcast || pipe_info.col_mcast);
        if (!mcast) {
          noc_num_unicast_loopbacks[pipe.outgoing_noc_id]++;
        }
      }
      noc_num_inputs[pipe.incoming_noc_id]++;
    }
    int noc0_load = noc_num_inputs[0] + noc_num_loopbacks[0];
    int noc1_load = noc_num_inputs[1] + noc_num_loopbacks[1];
    int noc0_excess_load = noc0_load - noc1_load;
    int noc1_excess_load = noc1_load - noc0_load;
    int loopbacks_switch_01 = 0;
    int loopbacks_switch_10 = 0;
    if ((noc0_excess_load > 1) && (noc_num_unicast_loopbacks[0] > 0)) {
      loopbacks_switch_01 = noc0_excess_load/2;
    }
    else if ((noc1_excess_load > 1) && (noc_num_unicast_loopbacks[1] > 0)) {
      loopbacks_switch_10 = noc1_excess_load/2;
    }

    for (int input_index = 0; input_index < op_info.input_names.size(); input_index++) {
      if ((loopbacks_switch_01 == 0) && (loopbacks_switch_10 == 0)) {
        break;
      }
      op_input_pipe_info_t pipe_info = op_input_pipe_info_map.at(op_info.name).at(input_index);
      pipe_t& pipe = router.get_pipes_mutable().at(pipe_info.pipe_id);
      bool unicast_loopback = pipe_info.op_input_loopback && !(pipe_info.row_mcast || pipe_info.col_mcast);
      if (unicast_loopback) {
        if ((pipe.outgoing_noc_id == 0) && (loopbacks_switch_01 > 0)) {
          loopbacks_switch_01--;
          pipe.outgoing_noc_id = 1;
          log_debug(tt::LogRouter, "Pipe id = {}, switching loobpack NOC from 0 to 1\n", pipe_info.pipe_id);
        }
        else if ((pipe.outgoing_noc_id == 1) && (loopbacks_switch_10 > 0)) {
          loopbacks_switch_10--;
          pipe.outgoing_noc_id = 0;
          log_debug(tt::LogRouter, "Pipe id = {}, switching loobpack NOC from 1 to 0\n", pipe_info.pipe_id);
        }
      }
    }    
  }

  void op_queue_routing_pass(Router &router) {

    std::map<std::string, std::map<int, op_input_pipe_info_t>> op_input_pipe_info_map;

    n2p::Log() << " *** Assigning pipe NOC IDs:\n";
    for (auto &[pipe_id, pipe] : router.get_pipes_mutable()) {
      pipe_route_nocs(router, pipe_id, pipe, op_input_pipe_info_map);
    }

    if (router.get_soc_descriptor().arch == tt::ARCH::WORMHOLE_B0) {
      // FIXME imatosevic: Placeholder for optimizing DRAM read/write port assignment. 
      // 
      // To implement this, we need pipegen change to allow separate NOC IDs/VCs for 
      // each pipe gather input. Also pipegen needs to support separate aliased buffer
      // instances for readers and writers. 
      // 
      // This pass should:
      // 
      //   (1) For DRAM buffers that have read_port/write_port specified in the netlist,
      //       assign the specified port to the "subchannel" field in pipegen.yaml, and
      //       assign the shortest-path NOC to each reader/writer pipe. 
      // 
      //   (2) For DRAM buffers that don't have the read/write port specified in the netlist,
      //       assign the ones that provide for the shortest read/write paths, under the 
      //       constraint of avoiding two performance-reducing cases:
      //  
      //          (a) Avoid reading or writing different channels in the same GDDR tile (i.e., 
      //              different outputs of the 6:2 xbar, through the same NOC port. (This causes
      //              end-to-end serialization because the xbar treats the requests as ordered.)
      // 
      //          (b) Avoid (or at least minimize) accessing the same channel through multiple
      //              ports on the 6:2 xbar. (This may cause transactions interleaving at a small
      //              granularity that negatively impacts locality and thus bandwidth.)
    }

    n2p::Log() << " *** Optimizing NOC IDs for op input loopback:\n";
    for (auto op_name_info : op_input_pipe_info_map) {
      const std::string &op_name = op_name_info.first;
      optimize_op_input_loopback(router, op_name, op_input_pipe_info_map);
    }
  }

}

