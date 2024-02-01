// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <memory>

#include "netlist/netlist.hpp"
#include "tile_maps.h"
#include "common/buda_soc_descriptor.h"
#include "buffer_info.h"
#include "router.hpp"
#include "net2pipe_common.h"
#include "net2pipe_types.h"
#include "common/tt_parallel_for.h"
#include "unique_id_generator.h"

#define SET_KEY_VAL(key, val) YAML::Key << key << YAML::Value << val

#define SET_COORD2(x, y) YAML::Flow << YAML::BeginSeq << (x) << (y) << YAML::EndSeq

#define SET_COORD3(c, x, y) YAML::Flow << YAML::BeginSeq << (c) << (x) << (y) << YAML::EndSeq



using router::pipe_t;
using router::time_multiplexed_outputs_t;
using router::pipe_output_list_t;


namespace n2p {

  constexpr std::uint32_t PROLOG_CHUNK_OPERAND_ID = 32;
  
  int get_op_input_total_bcast_factor(tt_op_info op_info, int index);
  void get_op_input_mcast(const tt_op_info& op_info, int input_num,
                          const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> &fused_op_schedule_map,
                          bool& row_mcast, bool& col_mcast, bool& noc1_mcast);
  bool producer_size_and_placement_for_direct_mcast(
                tt::ARCH device_arch,
                const tt_op_info& producer_op_info, const tt_op_info& consumer_op_info, 
                bool consumer_input_row_mcast, bool consumer_input_col_mcast,
                bool consumer_input_noc1_mcast,
                int worker_grid_size_x, int worker_grid_size_y,
                int& adjacent_noc_id);
                
  };


struct GraphExecVars{
  tt_instruction_info instrn; 
  int input_count;
  std::vector<QueueSettings> queue_settings;
} ;
  
struct dram_fork_pipe_metadata {
  std::vector<uint64_t> inputs;
  std::vector<int> total_readers;
  std::vector<int> reader_indexes;
  int num_forks;
  int fork_index;
};
struct temporal_epoch_context {
  int input_count;
  std::map<std::string, std::vector<std::string>> op_queue_output_map; // need to make sure output list of each queue/op is always traversed in the same order

  // buffer_map and pipes neet to use ordered map (std::map) to ensure deterministic output
  // since routing algorithm is iterating over these structs and is order dependant.
  std::map<std::uint64_t, router::router_buffer_info_t> buffer_map; // router_buffer_info stores routing coordinates
  std::map<std::uint64_t, pipe_t> pipes;

  std::unordered_map<std::uint64_t, pipe_t> grad_op_pipes;
  std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> buffer_output_pipes;
  std::unordered_map<std::uint64_t, std::uint64_t> buffer_input_pipes;
  std::map<std::string, bool> op_queue_output_scatter;
  std::map<std::string, int> op_queue_output_buf_granularity;
  std::unordered_map<router::unique_id_t, unordered_set<tt_cxy_pair>> input_queue_id_to_consumer_cores;
  std::unordered_map<router::unique_id_t, unordered_set<tt_cxy_pair>> output_queue_id_to_producer_cores;
  std::map<std::string, std::map<int, int> > op_input_kernel_tile_clear_granularity;

  std::map<std::string, std::map<std::string, n2p::prolog_layout>> prolog_layout_per_op;
  std::unordered_map<std::string, std::vector<n2p::prolog_buffer>> prolog_buffers_per_op;
  std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>> prolog_buffer_map;
  std::map<std::string, std::map<std::string, consumer_to_producer_tile_map>> prolog_tm_pipes_map;
  std::map<std::string, std::map<std::string, consumer_to_producer_tile_map>> prolog_input_pipes_map;
  std::map<std::string, std::map<std::string, bool>> prolog_post_tm_operand;

  std::map<std::string, std::map<int, std::string>> op_input_name_map;
  std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>> op_input_buf_map;
  std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>> op_intermediate_buf_map;
  std::map<std::string, std::map<int, std::map<int, std::uint64_t>>> op_output_buf_map;
  std::map<std::string, std::map<std::string, consumer_to_producer_tile_map>> op_input_tm_pipes_map;

  std::map<std::string, consumer_to_producer_tile_map> queue_input_tm_pipes_map;

  std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> fused_op_schedule_map;

  std::map<std::string, QueueSettings> queue_setting_map;
  std::unordered_map<n2p::padding_db_key, n2p::padding_buffer> pad_buffers_db;
  std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>> prolog_queue_name_fork_buf_unique_id_map;

  std::map<std::uint64_t, std::tuple<std::string, tt_xy_pair, int>> prologue_buf_unique_id_to_queue_info;

  std::unordered_map<uint64_t, dram_fork_pipe_metadata> dram_fork_pipe_metadata_by_pipe;

  std::map<std::string, tt_op_info> op_info_map;

  std::unordered_map<std::uint64_t, tt_queue_info> queue_unique_id_info_map; // these queue IDs are per-queue, not per-buffer, not used in pipegen.yaml
  std::unordered_map<std::string, std::uint64_t> queue_name_unique_id_map;

  std::vector<std::uint64_t> horonological_unique_keys;

  std::unordered_map<std::uint64_t, std::string> buffer_op_or_queue_name_map;
};

class Net2Pipe {
public:
  
  Net2Pipe(const std::string &netlist_file, const std::string &output_dir, const std::string &epoch_start, const std::string &soc_descriptor_file, const std::string &cluster_description_file="");
  void output_pipes();
  void get_graph_names(std::vector<std::string> & names_vec);
  
protected:
  const std::uint64_t PEER_REGION_SIZE = (1024 * 1024 * 1024);
  const std::uint64_t DRAM_REGION_SIZE = (256 * 1024 * 1024);
  const std::uint64_t PCIE_BAR_OFFSET_REMOTE_QUEUE_DRAM = (192 * 1024 * 1024); // Offset to DRAM for single ATU entry (L1 + DRAM)
  static constexpr int SEQUENTIAL_DRAM_IO_SCATTER_THRESHOLD = (64 * 1024); // We want this to be a power of 2 rather than exactly divisible by tile_size, the code that computes scatter_granularity depends on this

  // currently estimated based on 1GHz clock and 100Gbps (=12.5GBps) then rounded up to nearest power of 2
  static constexpr int ETH_BW_BYTES_PER_CYCLE = 16; /// TODO(snijjar): generalize based on target frequency and eth link BW and total eth link utilization
  static constexpr int NOC_BW_BYTES_PER_CYCLE = 32;

  mutable n2p::UniqueIdGenerator unique_id_gen;
  std::uint64_t start_epoch_id;

  std::uint64_t get_dram_buf_addr(std::string queue_name, int r, int c) const;
  int get_dram_buf_chan(std::string queue_name, int r, int c) const;

  std::string netlist_file;
  std::string output_dir;
  int starting_device_id;
  int ending_device_id;
  std::unique_ptr<buda_SocDescriptor> soc_descriptor;
  std::unique_ptr<ClusterDescription> cluster_description;

  std::map<std::string, std::map<int, std::map<int, std::uint64_t>>> queue_name_buf_unique_id_map;

  std::uint64_t get_next_unique_id(std::vector<std::uint64_t>& per_epoch_hronological_unique_ids, int align, int id_block = 1) const;
  int get_next_epoch_id();

  // epoch program execution variables and data structures
  bool program_done;
  std::vector<int> loop_pc_stack;
  std::vector<int> loop_count_stack;
  int program_counter;
  std::vector<int> variables = std::vector<int>(128);

  netlist_parser parsed_netlist;

  std::set<chip_id_t> workload_target_device_ids;

  bool var_trace_mode;
  typedef struct {
    tt_instruction_info instrn; 
    int input_count;
    std::vector<QueueSettings> queue_settings;
  } GraphExecVars;
  
  std::unordered_map<int, std::vector<GraphExecVars>> temporal_epoch_graph_exec_vars;
  std::vector<GraphExecVars> graph_exec_trace;
  std::vector<GraphExecVars> graph_exec_uniquified;

  std::map<std::string, tt_graph_info> op_graph_map;

  // Padding members
  static constexpr uint32_t MAX_PADDING_SCATTER_TILES = 13;

  uint64_t get_pad_buffer_id(const tt_op_info& op_info, temporal_epoch_context& epoch_context,const int input_index, const int chip, const int core_y, const int core_x) const;
  uint32_t get_dram_pad_addr(DataFormat df, float pad_val) const;
  void build_padding_table();
  void emit_padding_table();
  std::unordered_map<DataFormat, std::unordered_map<float, uint32_t>> dram_pad_addr_table;
  void emit_padding_buffers(YAML::Emitter &out_yaml, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const;

  void run_router(int temporal_epoch, temporal_epoch_context& epoch_context, ClusterDescription cluster_description) const;
  void populate_buffer_info_map();

  bool is_ethernet_pipe(const pipe_t &pipe, const temporal_epoch_context& epoch_context) const;
  bool is_mmio_pipe(const pipe_t &pipe, bool& is_downstream, const temporal_epoch_context& epoch_context) const;

  bool is_name_prolog_queue(const std::string& name, const temporal_epoch_context& epoch_context) const;

  bool is_output_scatter(std::string producer_name, int& scatter_granularity, const temporal_epoch_context& epoch_context) const;

  bool is_name_hw_tilize(const std::string& name, const temporal_epoch_context& epoch_context) const;

  bool is_name_embedding_table_queue(const std::string& name, const temporal_epoch_context& epoch_context) const;
  bool is_name_embedding_index_queue(const std::string& name, const temporal_epoch_context& epoch_context) const;

  bool is_input_adjacent_mcast(std::string producer_name, std::string consumer_name, int& adjacent_noc_id, const temporal_epoch_context& epoch_context) const;

  std::string uint_to_string(std::uint64_t num) const {
    std::stringstream out_str;
    out_str << "0x" << std::hex << num;
    std::string result(out_str.str());
    return result;
  }

  bool find_matching_graph_exec(GraphExecVars& graph_exec);

  void get_program_trace();
  void run_instruction(netlist_program &program);

  std::string create_temporal_epoch_output_directory(int temporal_epoch) const;
  void dump_yaml_to_file(YAML::Emitter const& out_yaml, const std::string &out_file_dir) const;

  void emit_queues_yaml(const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map);
  void connect_outputs_to_inputs(const tt_graph_info &graph_info, int input_count, temporal_epoch_context& epoch_context) const;
  void compute_consumer_input_tile_mappings(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const;
  void get_queue_consumer_map(std::map<router::unique_id_t, std::vector<router::unique_id_t>> inputs_to_output_pipes_map, temporal_epoch_context& epoch_context) const;
  void get_queue_producer_map(std::map<router::unique_id_t, std::vector<router::unique_id_t>> inputs_to_output_pipes_map, temporal_epoch_context& epoch_context) const;
  void dump_queue_to_core_map_to_file(const std::string& output_dir, bool queue_to_producer, const temporal_epoch_context& epoch_context) const;
  void emit_pipes(YAML::Emitter &out_yaml, temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const;
  void read_epoch_queue_info(const std::string &queue_name, int input_count, const QueueSettings& queue_setting, temporal_epoch_context& epoch_context) const;
  void collect_epoch_info(const std::vector<GraphExecVars> &graph_exec_vars, temporal_epoch_context& epoch_context) const;
  void register_pipe_as_output_of_buffers(std::uint64_t buffer_id, const std::vector<std::uint64_t> &buffer_ids, temporal_epoch_context& epoch_context) const;
  void register_pipe_as_input_of_buffers(std::uint64_t buffer_id, const pipe_output_list_t &buffer_ids, temporal_epoch_context& epoch_context) const;
  void register_pipe_as_input_of_buffers(std::uint64_t buffer_id, const time_multiplexed_outputs_t &timestep_buffer_ids, temporal_epoch_context& epoch_context) const;
  void collect_eltwise_op_input_pipes(const std::string &op_name);
  void collect_matmul_op_input_pipes(const std::string &op_name);
  void collect_queue_input_pipes(const std::string &queue_name, temporal_epoch_context& epoch_context) const;
  void collect_op_input_pipes(const std::string &op_name, int input_count, temporal_epoch_context& epoch_context) const;
  void collect_pipe_info(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const;
  void emit_epoch(const GraphExecVars &graph_exec, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, int temporal_epoch, std::map<std::string, bool> &op_queue_emitted, YAML::Emitter &out_yaml) const;
  int get_queue_dram_subchannel(std::uint64_t q_buf_id, const temporal_epoch_context& epoch_context) const;
  bool is_target_device_downstream(int starting_device_id, int ending_device_id, int epoch_device, int target_device) const;
  void emit_queue(YAML::Emitter& out, std::string queue_name, std::string graph_name, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, int epoch_device, int input_count, const QueueSettings& queue_settings) const;
  int compute_intermediate_buffer_size_tiles(const tt_op_info &op_info, int input_count, int int_id, int output_is_scatter, int output_size_tiles, int output_replicate) const;

  void create_prolog_buffers(const std::string &op_name, int input_index, temporal_epoch_context& epoch_context) const;
  void collect_kernel_buf_info(const std::string &op_name, int input_count, temporal_epoch_context& epoch_context) const;

  /*
   * A temporary pass that is in place to streamline transition for front-end to explicitly
   * insert ethernet datacopy ops. This pass assigns ethernet channels to unplaced ethernet
    * datacopy ops. This let's pybuda trivially insert ethernet datacopy ops to optimize stream
    * usage and bandwidth without requiring it to implement a full-blown router up front.
    *
    * After pybuda implements a full router, this pass will be acting as a nop and then
    * can be removed.
    *
    * To save complications of supporting unplaced ethernet ops (+buffers, pipes) we implement
    * this pass in an adhoc fashion. It will not make optimal routing decisions, but it will
    * make an informed guess about which ethernet channels to map the ethernet datacopy ops to.
   */
  void naive_place_unplaced_ethernet_datacopy_ops(const std::string &op_name, const temporal_epoch_context& epoch_context) const;
  void collect_epoch_buffer_info(const tt_graph_info &graph_info, int input_count, temporal_epoch_context& epoch_context) const;
  void emit_buffers(const tt_graph_info &graph_info, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, int input_count, std::map<std::string, bool> &op_queue_emitted, YAML::Emitter &out_yaml) const;
  void emit_relay_buffers(int runtime_input_count, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, YAML::Emitter& out) const;
  void emit_kernel_bufs(YAML::Emitter& out,const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, const std::string &op_name, int input_count) const;
  void emit_untilize_output(YAML::Emitter& out, const tt_op_info* op_info = NULL) const;

  bool name_is_queue(std::string name) const;
  bool name_is_op(std::string name, const temporal_epoch_context& epoch_context) const;

  bool producer_output_row_major_ublock_scan_order(std::string producer_name, const temporal_epoch_context& epoch_context) const;

  void get_op_core_routing_cxy(const temporal_epoch_context& epoch_context, const std::string &op_name, int r, int c, int &y_coord, int &x_coord) const;
  void get_op_input(const tt_op_info& op_info,
                    const temporal_epoch_context& epoch_context,
                    int core_r,
                    int core_c,
                    int input_count,
                    int index,
                    int &id,
                    int &overlay_blob_size,
                    int &tile_size_bytes,
                    int &epoch_tiles,
                    int &size_tiles,
                    int &scatter_gather_num_tiles,
                    int &tile_clear_granularity) const;

  int get_op_kernel_input_size_tiles(tt_op_info op_info, int index) const;
  int get_op_kernel_input_tile_clear_granularity(tt_op_info op_info, int index) const;

  void get_op_output(const tt_op_info& op_info, int input_count,
                     const temporal_epoch_context& epoch_context,
                     int core_r, int core_c,
                     int& id, int& is_scatter, int& replicate,
                     int& tile_size_bytes, int& epoch_tiles,
                     int& size_tiles, int& scatter_gather_num_tiles,
                     int &tiles_per_input) const;
  
  void get_op_output(const router::router_buffer_info_t &buffer, 
                     bool is_scatter,
                     int input_count,
                     int core_r, int core_c, int& replicate) const;

  int get_consumer_index(const std::string& producer_name, const std::string consumer_name);
  
  void get_tm_pipe_inputs(std::string op_name, int core_r, int core_c,
                          int input_index, std::vector<std::uint64_t>& pipe_inputs);

  void get_op_input_pipe_mcast_core_rc(
                                      const tt_op_info& op_info,
                                      const temporal_epoch_context& epoch_context,
                                      int input_idx,
                                      const phase_pipe_tile_map& pipe,
                                      const std::vector<std::vector<std::uint64_t>> &pipe_outputs,
                                      std::vector<std::tuple<chip_id_t, int, int>>& pipe_coords,
                                      bool &pipe_coords_are_ethernet_channels) const;
  
  void compute_op_tms(std::string op_name, int input_count, temporal_epoch_context& epoch_context) const;
  void compute_queue_tms(std::string queue_name, int input_count, temporal_epoch_context& epoch_context) const;

  void get_queue_attributes(const tt_queue_info &queue_info,
                            int input_count,
                            bool prolog,
                            const temporal_epoch_context& epoch_context,
                            int& q_slot_size_tiles,
                            int& tiles_per_input,
                            int& tile_size_bytes,
                            int& buf_epoch_tiles, 
                            int& replicate,
                            bool& is_scatter) const;

  int check_pipe_consumer_repeat(const pipe_t &pipe) const;
  int pipe_scatter_outputs_consumer_repeat(const std::vector<std::vector<std::uint64_t>> &pipe_outputs, const std::vector<bool>& pipe_output_padding) const;
  int pipe_scatter_outputs_consumer_repeat(const std::vector<std::vector<std::uint64_t>> &pipe_outputs, const std::vector<std::uint64_t>& output_id_padding) const;
  int get_dram_prefetch_noc_id(const std::string& queue_name, const std::string& op_name) const;
  int get_queue_input_noc_id(const std::string& producer_op_name, const std::string& queue_name, int r, int c);

  std::vector<std::uint64_t> get_queue_pipe_input(tt_queue_info queue_info, int r, int c, const temporal_epoch_context& epoch_context) const;

  void post_router_validate_all_pipes_placed_on_same_chip_as_inputs_and_outputs(const temporal_epoch_context& epoch_context) const;

  bool check_pipe_inputs_periodic(const pipe_t& pipe, int& period, int& periodic_repeat, const temporal_epoch_context& epoch_context) const;

  void check_op_resource_usage(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const;
  void set_ring_start_end_device_ids();

  void process_dram_fork_pipes(const std::unordered_map<string, tt_op_info>& temporal_epoch_op_map, temporal_epoch_context& epoch_context) const;

  std::tuple<
      std::unordered_map<std::uint64_t, tt_queue_info>,
      std::map<std::uint64_t, router::router_buffer_info_t>,
      std::map<std::uint64_t, pipe_t>,
      std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>>,
      std::unordered_map<std::uint64_t, std::uint64_t>,
      std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>>,
      std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>>,
      std::map<std::string, std::map<int, std::map<int, std::uint64_t>>>>

  export_router_to_net2pipe(const router::Router &router) const;

  void emit_operand_and_pipe_info(const std::unordered_map<string, tt_op_info>& temporal_epoch_op_map, int epoch_id, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const;
};
