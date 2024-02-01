// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
#include "common/buda_soc_descriptor.h"
#include "core_resource_tracker.h"
#include "buffer_info.h"
#include "address_map_adaptors/l1/l1_address_maps.h"
#include "common/base.hpp" // for tt_core_coord
#include "netlist/netlist_info_types.hpp" // for buffer_queue_info_t
#include "netlist/netlist_parser.hpp"

#include "net2pipe_types.h"
#include "unique_id_generator.h"

#include "router_types.h"
#include "core_resource_tracker.h"
#include "common/tt_cluster_graph.hpp"

#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <string>
#include <optional>
#include <variant>
#include <optional>


namespace router {

// TODO(snijjar): move to types header
struct pipe_resource_attributes_t {
  pipe_resource_attributes_t() :
    input_chips({}),
    has_output_to_dram({}),
    output_chips({}),
    forked_input_buffers({}),
    active_dram_queues(0),
    has_input_from_dram(false),
    has_gather(false),
    has_multicast(false)
  {}
    std::unordered_set<chip_id_t> input_chips;

    std::vector<bool> has_output_to_dram; // per timestep
    std::vector<std::unordered_set<chip_id_t>> output_chips; // per timestep

    std::unordered_set<unique_id_t> forked_input_buffers;
    int active_dram_queues;
    bool has_input_from_dram;
    bool has_gather;
    bool has_multicast;
};

/* Exposed to unit tests */
std::vector<std::tuple<int, int>> compute_relay_buffer_count_for_each_selected_physical_dram_row(
    const std::set<int> &all_dram_rows,
    const std::vector<tt_cxy_pair> &input_queue_buffer_locations, 
    const std::unordered_set<int> &consumer_buffer_rows
);

/* dram_channel_t is used as a type to cache dram channel info in the router. It's not strictly required for functionality,
 * but it's used to speedup dram channel lookup rather than iterating through the soc descriptor dram cores
 */
struct dram_channel_t {
  int channel;
  int subchannel;
};

struct RouterConfig {
  bool is_feature_ethernet_multichip_compile_enabled;
  tt::ARCH arch;
};

/* The router is the central component that is responsible for updating and optimizing routes between l1 buffers.
 * Currently, the router is responsible for the following:
 * - (Wormhole A0 only) insert relay buffers between dram queue buffers and L1 buffers for L1 buffers that aren't 
 *   on the same noc row as the dram core to avoid a silicon bug
 * - (Not implemented) Choose chip-to-chip routes between producers and consumers that are on different chips
 *   - choose chip-to-chip paths and ethernet links
 *   - allocate ethernet buffers
 *   - optimize data transfers between chips
 * - (Not implemented) optimize noc usage
 */
class Router {
  public:
   Router(
       std::unordered_map<chip_id_t, buda_SocDescriptor> const& _soc_descriptors,
       const std::vector<chip_id_t> &_chip_ids,
       const ClusterGraph &_cluster_graph,

       int unique_id_align,
       int unique_id_block,
       n2p::UniqueIdGenerator &_unique_id_generator,

       const std::unordered_map<unique_id_t, tt_queue_info> &_queue_map,
       std::map<unique_id_t, router_buffer_info_t> &_buffer_map,
       std::map<unique_id_t, pipe_t> &_pipes,
       std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &_buffer_output_pipes,
       std::unordered_map<unique_id_t, unique_id_t> &_buffer_input_pipes,
       std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &_op_input_buf_map,
       std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &_op_intermediate_buf_map,
       std::map<std::string, std::map<int, std::map<int, unique_id_t>>> &_op_output_buf_map,
       const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> &_fused_op_schedule_map,

       const std::map<std::string, bool> &_op_queue_output_scatter,
       const std::map<std::string, int> &op_queue_output_buf_granularity,
       const std::map<std::string, std::map<int, int>> &op_input_kernel_tile_clear_granularity,
       std::map<std::string, tt_op_info> &op_info_map,


       const std::map<std::string, QueueSettings> &queue_settings_map,
       const std::map<std::string, std::map<int, std::string>> &op_input_name_map,
       const std::unordered_map<std::string, std::vector<n2p::prolog_buffer>> &prolog_buffers_per_op,
       const std::map<std::string, std::map<std::string, bool>> &prolog_post_tm_operand,
       const std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>>
           &prolog_queue_name_fork_buf_unique_id_map,
       const std::map<std::string, std::vector<std::string>> &op_queue_output_map,
       std::vector<std::uint64_t> & _hronological_unique_id_list
    );

    const std::map<unique_id_t, router_buffer_info_t> &get_buffer_map() const { return buffer_map; };
    const std::map<unique_id_t, pipe_t> &get_pipes() const { return pipes; };
    const pipe_t &get_pipe(unique_id_t id) const { return pipes.at(id); };
    const tt_queue_info &get_queue(unique_id_t id) const { TT_ASSERT(id != router::NO_ASSOCIATED_QUEUE_OBJECT_ID); return queue_map.at(id); };
    const router_buffer_info_t &get_buffer(unique_id_t id) const { return buffer_map.at(id); };
    const std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &get_buffer_output_pipes() const { return buffer_output_pipes; };
    const std::unordered_map<unique_id_t, unique_id_t> &get_buffer_input_pipes() const { return buffer_input_pipes; };
    const std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &get_op_input_buf_map() const { return op_input_buf_map; };
    const std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &get_op_intermediate_buf_map() const { return op_intermediate_buf_map; };
    /*
     * <op_name, <row, <col, buf>>>
     */
    const std::map<std::string, std::map<int, std::map<int, unique_id_t>>> &get_op_output_buf_map() const { return op_output_buf_map; };
    const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> &get_fused_op_schedule_map() const { return fused_op_schedule_map; }
    const buda_SocDescriptor &get_soc_descriptor(chip_id_t chip) const { return soc_descriptors.at(chip); }
    const ClusterGraph &get_cluster_graph() const { return cluster_graph; }
    const RouterClusterResourceModel &get_cluster_resource_model() const { return cluster_resource_model; }
    RouterClusterResourceModel &get_cluster_resource_model() { return cluster_resource_model; }
    const std::unordered_map<tt_xy_pair, dram_channel_t> &get_dram_channels() const { return dram_channels; };
    const std::map<std::string, tt_op_info> &get_op_info_map() const { return op_info_map; }
    const std::unordered_map<router::unique_id_t, std::string>& get_op_input_buffer_op_name_map() const { return op_input_buffer_op_name_map; }
    const std::unordered_map<router::unique_id_t, std::string>& get_op_output_buffer_op_name_map() const { return op_output_buffer_op_name_map; }
    const std::unordered_map<router::unique_id_t, int>& get_op_input_buffer_index_map() const {  return op_input_buffer_index_map; }

    std::map<unique_id_t, pipe_t> &get_pipes_mutable() { return pipes; };

    int get_noc_distance(int noc_id, int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const;
    int get_noc0_distance(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const;
    int get_noc1_distance(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const;
    int get_shortest_path_noc_id(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const;

    bool can_add_buffer_to_core(unique_id_t buffer_id, const tt_cxy_pair &core_location);

    /* Run all router passes */
    void run(const netlist_parser &parsed_netlist);

    RouterConfig const& get_router_config() const { return this->config; }
  public:
    int get_max_gather_pipes_before_phase_based_gather() const { return this->address_map.arch_constants.MAX_MCAST_STREAMS_PER_CORE; }

    std::unordered_map<unique_id_t, std::vector<unique_id_t>> get_scatter_buffers_of_all_base_ids() const;
    bool is_pipe_scatter_single_core(unique_id_t pipe_id) const;
    bool is_ethernet_pipe(unique_id_t pipe_id) const;
    bool is_buffer_forked(unique_id_t buffer_id) const { return this->forked_buffer_lookup.at(buffer_id); }
    bool buffer_is_on_ethernet_core(unique_id_t buf_id, chip_id_t target_chip, ethernet_channel_t target_channel) const;
    bool is_buffer_scatter(unique_id_t buffer_id) const;
    bool is_queue_buffer(unique_id_t input_buffer_id) const;
    RouterBufferType get_buffer_type(unique_id_t buffer_id) const;
    bool is_buffer_tilized(unique_id_t buffer_id) const;
    tt_cxy_pair get_buffer_location(unique_id_t buffer_id) const;
    tt_cxy_pair get_buffer_location_tensix_grid(unique_id_t buffer_id) const;
    tt_cxy_pair get_pipe_location_tensix_grid(unique_id_t pipe_id, int output_index) const;
    tt_xy_pair get_buffer_tile_dims(unique_id_t buffer_id) const;
    int get_buffer_total_epoch_tiles(unique_id_t id) const;
    int get_queue_output_buffer_granularity(unique_id_t queue_id) const { return this->op_queue_output_buf_granularity.at(this->get_queue(queue_id).name); }
    int get_scatter_gather_granularity(unique_id_t buf_id) const;
    int get_op_kernel_input_tile_clear_granularity(const std::string& op_name, int operand_index) const;
    DataFormat get_buffer_data_format(unique_id_t id) const;
    int get_buffer_total_epoch_tiles(const router_buffer_info_t &buf) const;
    std::optional<int> get_available_relay_buffer_stream_at_core(const tt_cxy_pair &relay_buffer_core) const;
    unique_id_t add_new_buffer_to_core(const tt_cxy_pair &relay_buffer_core, const tt::buffer_info &buffer_info);
    unique_id_t add_new_buffer(const tt::buffer_info &buffer_info, chip_id_t chip);
    void change_buffer_size(unique_id_t buffer_id, int new_size_in_bytes);
    unique_id_t add_new_pipe(const pipe_t &pipe);
    bool is_l1_buffer(unique_id_t buffer_id) const;
    bool is_dram_core(const tt_xy_pair &core) const { return this->dram_channels.find(core) != this->dram_channels.end(); }
    void update_dram_queue_core_location(unique_id_t buffer_id, tt_cxy_pair queue_location);
    void update_dram_buffer_readers(unique_id_t buffer_id, bool has_readers, int read_port);
    void update_dram_buffer_writers(unique_id_t buffer_id, bool has_writers, int write_port);
    void assign_buffer_to_core(unique_id_t buffer_id, const tt_cxy_pair &core_location);
    void assign_pipe_to_core(unique_id_t pipe_id, const tt_cxy_pair &core_location);
    void assign_pipe_scatter_segment_to_core(unique_id_t pipe_id, int segment, const tt_cxy_pair &core_location);
    unique_id_t get_scatter_buffer_base_id(unique_id_t scatter_buffer_id) const;
    bool pipe_implements_gather(pipe_segment_id_t const &pipe_segment_id) const;
    pipe_resource_attributes_t collect_pipe_segment_attributes(pipe_segment_id_t const& pipe_segment_id) const;
    pipe_resource_attributes_t collect_pipe_attributes(unique_id_t pipe_id) const;

    bool can_assign_connected_pipe_to_core(pipe_segment_id_t const& pipe_segment_id, const tt_cxy_pair &core_location) const;
    bool can_assign_connected_buffer_to_core(unique_id_t pipe_id, const tt_cxy_pair &core_location) const;
    bool can_assign_connected_pipe_and_buffer_to_core(pipe_segment_id_t const& pipe_segment_id, unique_id_t buffer_id, const tt_cxy_pair &core_location) const;
    void assign_buffer_stream_id(unique_id_t buffer_id, int stream_id) { auto &buffer = this->buffer_map.at(buffer_id).info(); TT_ASSERT(!buffer.is_assigned_to_operand_id()); buffer._stream_id = stream_id; }
    std::unordered_set<unique_id_t> get_op_output_pipes(const Router &router, const std::string &op_name) const;

    ///////////////////////////////////////////////////////////////////////////////
    // ROUTER DATAFLOW MANIPULATION FUNCTIONS                                    //
    ///////////////////////////////////////////////////////////////////////////////
    void replace_pipe_input(unique_id_t pipe_id, unique_id_t old_buffer_id, unique_id_t new_buffer_id);
    
    /*
     * Replaces all instances of `old_buffer_id` in the output buffer list(s) of pipe `pipe_id`
     * If the pipe is a scatter pipe and `old_buffer_id` exists in multiple scatter segment output buffer lists,
     * then it will be replaced for all of those scatter segments. To replace the buffer
     */
    void replace_pipe_output(unique_id_t pipe_id, unique_id_t old_buffer_id, unique_id_t new_buffer_id);
    void replace_scatter_pipe_outputs_at_segment(pipe_segment_id_t const& pipe_segment_id, const pipe_output_list_t &new_outputs);
    void replace_scatter_pipe_outputs(unique_id_t pipe_id, const time_multiplexed_outputs_t &new_outputs);
    unique_id_t create_unicast_pipe_connection(unique_id_t producer_buffer_id, unique_id_t consumer_buffer_id, int consumer_scatter_granularity);
    unique_id_t create_gather_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, unique_id_t consumer_buffer_id, int consumer_scatter_granularity);
    unique_id_t create_multicast_pipe_connection(unique_id_t producer_buffer_id, const pipe_output_list_t &consumer_buffer_ids, int consumer_scatter_granularity);
    unique_id_t create_gather_multicast_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, const pipe_output_list_t &consumer_buffer_ids, int consumer_scatter_granularity);
    unique_id_t create_gather_scatter_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, const pipe_output_list_t &consumer_buffer_ids, int consumer_scatter_granularity);
    unique_id_t create_unicast_pipe_connection_at_core(unique_id_t producer_buffer_id, unique_id_t consumer_buffer_id, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_gather_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, unique_id_t consumer_buffer_id, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_multicast_pipe_connection_at_core(unique_id_t producer_buffer_id, const pipe_output_list_t &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_scatter_pipe_connection_at_core(unique_id_t producer_buffer_id, const pipe_output_list_t &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_gather_multicast_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, const pipe_output_list_t &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_gather_scatter_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, const pipe_output_list_t &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_scatter_granularity);
    unique_id_t create_scatter_pipe_connection_at_cores(const std::vector<unique_id_t> &producer_buffer_ids, const time_multiplexed_outputs_t &timestep_consumer_buffer_ids, const std::vector<tt_cxy_pair> &locations);

    BufferAttributes identify_buffer_attributes(unique_id_t buffer_id) const;

    /* Disconnects a pipe from all input and output buffers */
    void disconnect_pipe(unique_id_t pipe_id);

    /* Removes the pipe from the router */
    void remove_pipe(unique_id_t pipe_id);

    void remove_buffer(unique_id_t);
    void erase_op(const std::string op_name);

    AddressMap const& get_address_map() const { return address_map; }

  protected:
    friend Net2Pipe;
    const std::unordered_map<unique_id_t, tt_queue_info> &get_queue_map() const { return queue_map; }

  private:
    std::unordered_map<unique_id_t, tt_cxy_pair> get_prolog_buffer_resident_core_map();
    void get_prolog_buffer_allocation_sizes(std::unordered_map<tt_cxy_pair, int> &l1_usage_per_core);
    void add_pipe_resource_usage_to_core(pipe_segment_id_t const& pipe_segment_id, std::optional<tt_cxy_pair> const& core_location = std::nullopt);
    void remove_pipe_resource_usage_from_core(pipe_segment_id_t const& pipe_segment_id);
    void remove_buffer_attributes(const tt_cxy_pair &core, const BufferAttributes &attrs);
    void apply_buffer_attributes(const tt_cxy_pair &core, const BufferAttributes &attrs);
    void apply_buffer_attribute_diffs(const std::unordered_map<tt_cxy_pair, BufferAttributes> &old_attrs, const std::unordered_map<tt_cxy_pair, BufferAttributes> &new_attrs);


    int UNIQUE_ID_ALIGN;
    int ID_BLOCK;
    n2p::UniqueIdGenerator &unique_id_generator;

    unique_id_t get_new_pipe_unique_id();
    unique_id_t get_new_buffer_unique_id();
    static std::unordered_map<tt_xy_pair, dram_channel_t> initialize_dram_core_map(const std::unordered_map<chip_id_t, buda_SocDescriptor> &soc_descriptors);
    static std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> initialize_hw_cores(const std::unordered_map<chip_id_t, buda_SocDescriptor> &soc_descriptors, const AddressMap &address_map, const std::vector<chip_id_t> &chip_ids);

    // const buda_SocDescriptor &soc_descriptor;
    const std::unordered_map<chip_id_t, buda_SocDescriptor> soc_descriptors;
    ClusterGraph cluster_graph;
    const AddressMap address_map;
    const std::unordered_map<tt_xy_pair, dram_channel_t> dram_channels;
    RouterClusterResourceModel cluster_resource_model;
    // std::unordered_map<tt_cxy_pair, HwCoreAttributes> chip_hw_cores;
    const std::vector<chip_id_t> chip_ids;

    std::unordered_map<unique_id_t, tt_queue_info> queue_map;
    std::map<unique_id_t, router_buffer_info_t> buffer_map;
    std::map<unique_id_t, pipe_t> pipes;
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes;
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes;

    //      <name       ,         <r  ,         <c  ,     <operand, buffer_id  >>>>
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &op_input_buf_map;
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &op_intermediate_buf_map;
    //      <name       ,         <r  ,         <c  , buffer_id  >>>
    std::map<std::string, std::map<int, std::map<int, unique_id_t>>> &op_output_buf_map;
    const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> & fused_op_schedule_map;
    const std::map<std::string, bool> &op_queue_output_scatter;
    const std::map<std::string, int> &op_queue_output_buf_granularity;
    const std::map<std::string, std::map<int, int> > &op_input_kernel_tile_clear_granularity;
    std::map<std::string, tt_op_info> &op_info_map;

    const std::map<std::string, QueueSettings> &queue_settings_map;

    std::unordered_map<unique_id_t, std::pair<tt_cxy_pair, int>> buffer_core_operand_map;
    const std::unordered_map<std::string, std::vector<n2p::prolog_buffer>> &prolog_buffers_per_op;
    const std::map<std::string, std::map<int, std::string>> &op_input_name_map;
    const std::map<std::string, std::map<std::string, bool>> &prolog_post_tm_operand;

    // reverse lookup structures, useful for identifying consumer ops of pipes 
    std::unordered_map<router::unique_id_t, std::string> op_input_buffer_op_name_map;
    std::unordered_map<router::unique_id_t, std::string> op_output_buffer_op_name_map;
    std::unordered_map<router::unique_id_t, int> op_input_buffer_index_map;
  
    const std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &prolog_queue_name_fork_buf_unique_id_map;
    const std::map<std::string, std::vector<std::string>> &op_queue_output_map;

    const std::unordered_map<unique_id_t, bool> forked_buffer_lookup;
    std::vector<std::uint64_t> & hronological_unique_id_list;

    RouterConfig config;

    std::tuple<
    std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>,
    std::unordered_map<tt_cxy_pair, std::unordered_set<int>>> constructor_initialize_pipe_resource_usage();
    void constructor_allocate_extra_tile_headers_for_tensix_cores();
    void constructor_initialize_forked_kernel_output_buffer_extra_streams(const std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &buffer_scatter_children);
    void constructor_initialize_prolog_extra_stream_counts();
    void constructor_disable_core_resource_tracking_asserts();
    void constructor_enable_core_resource_tracking_asserts();
    void constructor_report_exceeded_core_resources() const;
};

bool is_chip_to_chip_pipe(const pipe_t &pipe, const Router &router);
bool requires_extra_input_streams(const Router &router, unique_id_t pipe_id);

int compute_pipe_segment_extra_stream_use(const Router &router, pipe_segment_id_t const& pipe_segment_id, const tt_cxy_pair &core_location);

int compute_pipe_segment_gather_extra_stream_use(const Router &router, pipe_segment_id_t const& pipe_segment_id);

int compute_pipe_segment_gather_extra_input_buffering_size(const Router &router, pipe_segment_id_t const& pipe_segment_id);

int get_number_of_active_dram_queues(const Router &router, unique_id_t pipe_id);
bool op_forks_to_multiple_consumers(router::Router &router, const std::string &op_name);
bool is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(router::Router &router, const pipe_t &pipe);

pipe_segment_hash_t create_pipe_segment_hash(Router const& router, pipe_segment_id_t const& pipe_segment_id);

};  // namespace router
