// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "netlist/netlist_info_types.hpp"
#include "buffer_info.h"
#include "netlist_basic_info_types.hpp"

#include "common/base_types.hpp"

#include <cstdint>
#include <vector>
#include <optional>
#include <unordered_map>


class Net2Pipe;

namespace router {
using unique_id_t = std::uint64_t;

class Router;


/* Reserved constant unique ID value for scenarios where a queue buffer may not have an associated
 * tt_queue_info object (such as for padding buffers). This is the reserved value used to indicate
 * those scenarios */
constexpr unique_id_t NO_ASSOCIATED_QUEUE_OBJECT_ID = 0;

using time_multiplexed_outputs_t = std::vector<std::vector<router::unique_id_t>>;
using pipe_output_list_t = std::vector<router::unique_id_t>;
using pipe_input_buffers_t = std::vector<router::unique_id_t>;

struct router_queue_allocation_info : public tt_queue_allocation_info {
  bool is_host_mapped;
};

/* Holds buffer queue information for router_buffer_info_t
 */
class buffer_queue_info_t {
  public:
    buffer_queue_info_t(unique_id_t _parent_queue_id, const router_queue_allocation_info &allocation, bool queue_prolog_setting) :
      parent_queue_id(_parent_queue_id),
      dram(allocation),
      prolog(queue_prolog_setting)
    {}

  unique_id_t get_parent_queue_id() const { return parent_queue_id; }
  const router_queue_allocation_info &get_allocation_info() const { return dram; }
  void set_prolog_core_location(tt_xy_pair loc) {
    _prolog_core_location = loc;
  }
  tt_xy_pair get_prolog_core_location() const {
    return _prolog_core_location.value();
  }
  bool is_prolog() const { return prolog; }

  private:
    // has a the id of the `tt_queue_info` parent queue of this buffer - denoted by `parent_queue_id`
    unique_id_t parent_queue_id;

    // holds the dram allocation info for this queue buffer (channel, address, etc.)
    router_queue_allocation_info dram;

    bool prolog;
    std::optional<tt_xy_pair> _prolog_core_location;
};
bool operator==(buffer_queue_info_t const&, buffer_queue_info_t const&);

/* Holds for buffer attributes used by router - includes buffer shape information.
 * - This class can hold both buffer and queue buffer info
 */
class router_buffer_info_t {
  private:
    chip_id_t _chip_location;
    std::optional<tt_xy_pair> _core_location;
    std::variant<tt::buffer_info, buffer_queue_info_t> _info;

    // only for DRAM buffers
    int _read_port = -1;
    int _write_port = -1;
    bool _has_readers = false;
    bool _has_writers = false;
    
    router_buffer_info_t(const tt::buffer_info &info, chip_id_t chip_id, bool is_mutable=false) : _chip_location(chip_id), _core_location(std::nullopt), _info(info), _is_mutable(is_mutable) {}
    router_buffer_info_t(const tt_cxy_pair &location, const tt::buffer_info &info, bool is_mutable=false) : _chip_location(location.chip), _core_location(tt_xy_pair(location.x, location.y)), _info(info), _is_mutable(is_mutable) {}
    router_buffer_info_t(const buffer_queue_info_t &info, chip_id_t chip_id, int read_port=-1, int write_port=-1, bool is_mutable=false) : _chip_location(chip_id), _core_location(std::nullopt), _info(info), _read_port(read_port), _write_port(write_port), _is_mutable(is_mutable) {}
    router_buffer_info_t(const tt_cxy_pair &location, const buffer_queue_info_t &info, int read_port=-1, int write_port=-1, bool is_mutable=false) : _chip_location(location.chip), _core_location(tt_xy_pair(location.x, location.y)), _info(info), _read_port(read_port), _write_port(write_port), _is_mutable(is_mutable) {}

  protected:
    friend Router;
    friend Net2Pipe;

    tt::buffer_info &info() { TT_ASSERT(std::holds_alternative<tt::buffer_info>(_info), "Only non-queue buffers can call info()"); return std::get<tt::buffer_info>(_info); }
    buffer_queue_info_t &queue_info() { TT_ASSERT(this->is_queue(), "Only queue buffers have queue_info and can call queue_info()"); return std::get<buffer_queue_info_t>(_info); }

    bool _is_mutable; // the only buffers that are mutable are relay buffers and ethernet_datacopy_io buffers that weren't pybuda specified
   
    /*
     * Assign or update the core location of a buffer
     * Marked as protected because we don't want to allow passes to directly update this field as router shall only place buffers on
     * cores that have sufficient resources to hold them
     */
    void update_core_location(const tt_cxy_pair &new_core_location) {
        this->_core_location = tt_xy_pair(new_core_location.x, new_core_location.y);
        this->_chip_location = new_core_location.chip;
    }

    // Below update functions are called by the final routing pass
    void update_readers(bool has_readers, int read_port) {
      TT_ASSERT(this->is_queue()); 
      this->_has_readers = has_readers;
      this->_read_port = read_port;
    }

    void update_writers(bool has_writers, int write_port) {
      TT_ASSERT(this->is_queue()); 
      this->_has_writers = has_writers;
      this->_write_port = write_port;
    }

    bool has_readers() const { TT_ASSERT(this->is_queue()); return this->_has_readers; }
    bool has_writers() const { TT_ASSERT(this->is_queue()); return this->_has_writers; }
    int get_read_port() const { TT_ASSERT(this->is_queue()); return this->_read_port; }
    int get_write_port () const { TT_ASSERT(this->is_queue()); return this->_write_port; }
    bool read_port_allocated() const { TT_ASSERT(this->is_queue()); return this->_read_port != -1; }
    bool write_port_allocated() const { TT_ASSERT(this->is_queue()); return this->_write_port != -1; }

  public:
    bool operator==(router_buffer_info_t const&) const; 
    
    /*
     * Mutable router_buffer_info_t constructors
     * Immutable buffers may *not* have their fields modified after construction
     * Attempting to modify an immutable buffer will result in an error at runtime
     */
    static router_buffer_info_t create_immutable(chip_id_t chip_id, const tt::buffer_info &info) { return router_buffer_info_t(info, chip_id); }
    static router_buffer_info_t create_immutable(const tt_cxy_pair &location, const tt::buffer_info &info) { return router_buffer_info_t(location, info); }
    static router_buffer_info_t create_immutable(chip_id_t chip_id, const buffer_queue_info_t &info, int read_port=-1, int write_port=-1) { return router_buffer_info_t(info, chip_id, read_port, write_port); }
    static router_buffer_info_t create_immutable(const tt_cxy_pair &location, const buffer_queue_info_t &info, int read_port=-1, int write_port=-1) { return router_buffer_info_t(location, info, read_port, write_port); }

    /*
     * Mutable router_buffer_info_t constructors
     * Mutable buffers may have their fields modified after construction
     */
    static router_buffer_info_t create_mutable(chip_id_t chip_id, const tt::buffer_info &info) { return router_buffer_info_t(info, chip_id, true); }
    static router_buffer_info_t create_mutable(const tt_cxy_pair &location, const tt::buffer_info &info) { return router_buffer_info_t(location, info, true); }
    static router_buffer_info_t create_mutable(chip_id_t chip_id, const buffer_queue_info_t &info, int read_port=-1, int write_port=-1) { return router_buffer_info_t(info, chip_id, read_port, write_port, true); }
    static router_buffer_info_t create_mutable(const tt_cxy_pair &location, const buffer_queue_info_t &info, int read_port=-1, int write_port=-1) { return router_buffer_info_t(location, info, read_port, write_port, true); }

    /*
     * For mutable buffers, return a mutable reference to the underlying buffer info
     * Only valid for non-queue buffers. If called on queue buffers, an error will be generated.
     * Will error if called on an immutable buffer
     */
    tt::buffer_info &mutable_info() { TT_ASSERT(std::holds_alternative<tt::buffer_info>(_info)); TT_ASSERT(_is_mutable); return std::get<tt::buffer_info>(_info); }

    /*
     * Return an const reference to  the underlying buffer info
     * Only valid for non-queue buffers. If called on queue buffers, an error will be generated.
     * Will error if called on an immutable buffer
     */
    const tt::buffer_info &info() const { TT_ASSERT(std::holds_alternative<tt::buffer_info>(_info), "Only non-queue buffers can call info()"); return std::get<tt::buffer_info>(_info); }
    
    /*
     * Returns the underlying queue buffer information for queue buffers. If called for non-queue buffers, an error will be generated.
     */
    const buffer_queue_info_t &queue_info() const { TT_ASSERT(this->is_queue(), "Only queue buffers have queue_info and can call queue_info()"); return std::get<buffer_queue_info_t>(_info); }
    
    /*
     * If true, indicates this is a queue buffer and has underlying `queue_info()`
     */
    bool is_queue() const { return std::holds_alternative<buffer_queue_info_t>(_info); }

    /*
     * If true, indicates this is a prolog buffer. It doesn't have associated queue information.
     */
    bool is_prolog() const { return std::holds_alternative<buffer_queue_info_t>(_info) and std::get<buffer_queue_info_t>(_info).is_prolog();}

    /*
     * If true, indicates this is an intermediate prolog buffer. It doesn't have associated queue information.
     */
    bool is_prolog_inter() const { return std::holds_alternative<tt::buffer_info>(_info) && (std::get<tt::buffer_info>(_info).type() == RouterBufferType::PrologInter); }
    
    /*
     * Returns true if this buffer was assigned to a valid chip,x,y location
     */
    bool core_location_assigned() const { return _core_location.has_value(); }

    /*
     * Returns this chip that this buffer was assigned to. Always legal to call.
     */
    chip_id_t chip_location() const { return _chip_location; }
    
    /*
     * Returns this {chip,x,y} that this buffer was assigned to. This isn't always legal to call as all buffers don't always
     * have a core assignment (e.g. if you create the buffer but are still looking for a core to assign to)
     *
     * `core_location_assigned()` can be called if the caller is unsure if the core_location is valid.
     *
     * Will generate an error if `!core_location_assigned()`
     */
    tt_cxy_pair core_location() const { 
      TT_ASSERT(this->core_location_assigned(), "Tried to get core location of buffer that was not assigned to a core location");
      return tt_cxy_pair(_chip_location, _core_location.value()); 
    }
};



struct BufferAttributes {
    BufferAttributes()=default;

    void set_multicast(int num) { num_multicasts = num; }
    int get_num_multicasts() const { return num_multicasts; }
    bool has_multicast() const { return num_multicasts > 0; }
    void remove_attribute_multicast() { num_multicasts = 0; }
    
    void set_input_from_dram() { input_from_dram = true; }
    bool has_input_from_dram() const { return input_from_dram; }
    void remove_input_from_dram() { input_from_dram = false; }

    void set_outputs_to_dram(int num_streams) { outputs_to_dram = num_streams; }
    int get_num_outputs_to_dram() const { return outputs_to_dram; }
    int has_outputs_to_dram() const { return outputs_to_dram > 0; }
    void remove_outputs_to_dram() { outputs_to_dram = 0; }

    void set_ethernet_io_streams(int num_streams) { num_ethernet_io_link_streams = num_streams; }
    int get_num_ethernet_io_streams() const { return num_ethernet_io_link_streams; }
    int has_ethernet_io_streams() const { return num_ethernet_io_link_streams > 0; }
    void remove_ethernet_io_streams() { num_ethernet_io_link_streams = 0; }

    void set_forking_factor(int factor) { forking_factor = factor; }
    int get_forking_factor() const { return forking_factor; }
    
    int outputs_to_dram = 0;
    int num_multicasts = 0;
    int num_ethernet_io_link_streams = 0;
    int forking_factor = 1;
    bool input_from_dram = false;
    
    static BufferAttributes requires_ethernet_io() { auto attrs = BufferAttributes(); attrs.set_ethernet_io_streams(1); return attrs; }
};

BufferAttributes merge(const BufferAttributes &a, const BufferAttributes &b);

enum class BufferAllocationFailureReason : uint8_t {
  Insufficient_L1 = 0,
  Insufficient_DRAM_Input_Streams = 1,
  Insufficient_DRAM_Output_Streams = 2,
  Insufficient_Multicast_Streams = 3,
  Insufficient_Relay_Streams = 4,
  Insufficient_Ethernet_Streams = 5,
  Insufficient_Extra_Streams = 6
};
std::ostream &operator<<(std::ostream &os, const BufferAllocationFailureReason& reason);

class HwCoreAttributes;
void log_buffer_allocation_failure_reasons(HwCoreAttributes const& core_attributes, const std::vector<BufferAllocationFailureReason> &reasons);

class pipe_t {
  public:
    int incoming_noc_id;
    int outgoing_noc_id;
    int incoming_vc;
    int outgoing_vc;
    bool direct_mcast;
    std::vector<router::unique_id_t> input_buffer_ids;
    // Vector of IDs or 0 to include padding for outputscatter pipes. Must be the same length as the output_list
    std::vector<router::unique_id_t> output_padding_buffer_list;
    int consumer_tile_granularity;

    pipe_t(const std::vector<router::unique_id_t> &_input_buffer_ids, const pipe_output_list_t &_output_buffer_ids, const std::string &_consumer_name = "", int _input_index_of_consumer = -1, int _consumer_tile_granularity = 1) :
      locations({}),
      input_buffer_ids(_input_buffer_ids),
      output_buffer_ids_container(_output_buffer_ids),
      name_of_consumer(_consumer_name),
      input_index_of_consumer(_input_index_of_consumer),
      consumer_tile_granularity(_consumer_tile_granularity),
      direct_mcast(false),
      incoming_noc_id(0),
      outgoing_noc_id(0),
      incoming_vc(0),
      outgoing_vc(0)
    {}

    pipe_t(const tt_cxy_pair _location, const std::vector<router::unique_id_t> &_input_buffer_ids, const pipe_output_list_t &_output_buffer_ids, const std::string &_consumer_name = "", int _input_index_of_consumer = -1, int _consumer_tile_granularity = 1) :
      locations({_location}),
      input_buffer_ids(_input_buffer_ids),
      output_buffer_ids_container(_output_buffer_ids),
      name_of_consumer(_consumer_name),
      input_index_of_consumer(_input_index_of_consumer),
      consumer_tile_granularity(_consumer_tile_granularity),
      direct_mcast(false),
      incoming_noc_id(0),
      outgoing_noc_id(0),
      incoming_vc(0),
      outgoing_vc(0)
    {}

    pipe_t(const std::vector<tt_cxy_pair> &_locations, const std::vector<router::unique_id_t> &_input_buffer_ids, const time_multiplexed_outputs_t &_output_buffer_ids, const std::string &_consumer_name = "", int _input_index_of_consumer = -1, int _consumer_tile_granularity = 1) :
      locations(_locations),
      input_buffer_ids(_input_buffer_ids),
      output_buffer_ids_container(_output_buffer_ids),
      name_of_consumer(_consumer_name),
      input_index_of_consumer(_input_index_of_consumer),
      consumer_tile_granularity(_consumer_tile_granularity),
      direct_mcast(false),
      incoming_noc_id(0),
      outgoing_noc_id(0),
      incoming_vc(0),
      outgoing_vc(0)    
    {
    }

    pipe_t(const std::vector<router::unique_id_t> &_input_buffer_ids, const time_multiplexed_outputs_t &_output_buffer_ids, const std::string &_consumer_name = "", int _input_index_of_consumer = -1, int _consumer_tile_granularity = 1) :
      locations({}),
      input_buffer_ids(_input_buffer_ids),
      output_buffer_ids_container(_output_buffer_ids),
      name_of_consumer(_consumer_name),
      input_index_of_consumer(_input_index_of_consumer),
      consumer_tile_granularity(_consumer_tile_granularity),
      incoming_noc_id(0),
      outgoing_noc_id(0)
    {
    }

    /*
     * Returns: `true` if the pipe has multiple output timesteps/is a scatter pipe (i.e. a list of lists of output
     * buffer IDs). `false` otherwise (e.g. flat list of output buffer IDs)
     *
     * When `true` the output buffers for the pipe in the pipegen YAML is a nested buffer list:
     *   [ [buf1, buf2, ...], ..., [bufk, ...] ]
     * When`false`,  the output buffers for the pipe in the pipegen YAML is a flatt buffer list:
     *   [buf1, buf2, ...]
     */
    bool has_multiple_timesteps() const { return std::holds_alternative<time_multiplexed_outputs_t>(output_buffer_ids_container);}

    /*
     * Returns the location this pipe is implemented on, for non-scatter pipes.
     * Calling this function on a scatter pipe will result in an error.
     */
    const tt_cxy_pair &core_location() const { TT_ASSERT(locations.size() == 1, "Pipe expected to have exactly one core_location when calling `core_location()` but ", locations.size(), " were found"); return locations.at(0); }

    /*
     * Returns the location this pipe is implemented on for timestep/scatter segment `i`
     * Callable for non-scatter pipes as well (with `i` == 0, always)
     */
    const tt_cxy_pair &scatter_segment_core_location(int i) const {  TT_ASSERT(locations.size() > i, "Index error when trying to access scatter pipe output scatter index ", i, " but there are only ", locations.size(), " scatter indices"); return locations.at(i); }

    /*
     * Returns the locations this pipe is implemented on for all timestep/scatter segments
     * Calling this function on a non-scatter pipe will result in an error.
     */
    const std::vector<tt_cxy_pair> &core_locations() const { return locations; }; // To preserve old raw behaviour during transition

    /*
     * Returns true if this pipe was assigned to a valid chip,x,y location
     */
    bool core_location_assigned() const { return locations.size() > 0; }

    /*
     * Returns the output buffer list for non-scatter pipes. Will result in an error if called on scatter pipes
     */
    pipe_output_list_t &output_buffer_ids() { return std::get<pipe_output_list_t>(output_buffer_ids_container); }
    const pipe_output_list_t &output_buffer_ids() const { return std::get<pipe_output_list_t>(output_buffer_ids_container); }

    /*
     * Returns the nested output buffer list for scatter pipes. Will result in an error if called on non-scatter pipes
     */
    time_multiplexed_outputs_t &time_multiplexed_output_buffer_ids() { return std::get<time_multiplexed_outputs_t>(output_buffer_ids_container); }
    const time_multiplexed_outputs_t &time_multiplexed_output_buffer_ids() const { return std::get<time_multiplexed_outputs_t>(output_buffer_ids_container); }
    
    /*
     * For non-scatter pipes, if segment 0 is provided, returns the output buffer list. 
     * For scatter pipes, returns the output buffers for the specified `segment`
     */
    pipe_output_list_t &output_segment_output_buffer_ids(int segment) { TT_ASSERT(this->is_scatter() || segment == 0); return this->is_scatter() ? this->time_multiplexed_output_buffer_ids().at(segment) : this->output_buffer_ids(); }
    pipe_output_list_t const& output_segment_output_buffer_ids(int segment) const { TT_ASSERT(this->is_scatter() || segment == 0);  return this->is_scatter() ? this->time_multiplexed_output_buffer_ids().at(segment) : this->output_buffer_ids(); }

    /*
     * Returns `true` if this is a scatter pipe (has_multiple timesteps)
     */
    bool is_scatter() const {
      return this->has_multiple_timesteps();
    }

    /*
     * Returns `true` if this is a pipe with optimized direct multicast for a matmul consumer with an adjacent producer
     */
    bool is_direct_mcast() const {
      return this->direct_mcast;
    }

    /*
     * Returns: The number of scatter segments if this is a scatter pipe.
     *          Otherwise will return 1 for non-scatter pipes
     */
    int number_of_output_segments() const {
      return this->is_scatter() ? this->time_multiplexed_output_buffer_ids().size() : 1;
    }

    void clear_consumer_metadata() {
      name_of_consumer = "";
      input_index_of_consumer = -1;
    }

    /*
     * Return true if we have associated op information - used for debug only
     */
    bool has_consumer() const { return name_of_consumer.size() > 0; }    

    /* 
     * Debug Information that can be optionally dumped into the pipegen yaml
     */
    const std::string &consumer_name() const { return name_of_consumer; }

    /*
     * Debug Information that can be optionally dumped into the pipegen yaml
     */
    int consumer_input_index() const { return input_index_of_consumer; }

    void set_consumer_name(const std::string& s) { name_of_consumer = s; }
    void set_consumer_input_index(int index) { input_index_of_consumer = index; }

    bool operator==(const pipe_t &other) const {
      return locations == other.locations &&
             input_buffer_ids == other.input_buffer_ids &&
             output_buffer_ids_container == other.output_buffer_ids_container &&
             consumer_tile_granularity == other.consumer_tile_granularity &&
             incoming_noc_id == other.incoming_noc_id &&
             outgoing_noc_id == other.outgoing_noc_id;
    }

  protected:
    friend Net2Pipe;
    friend class Router;
    std::vector<tt_cxy_pair> locations;
    std::variant<pipe_output_list_t, time_multiplexed_outputs_t> output_buffer_ids_container;

  private:
    // Debug information
    std::string name_of_consumer;
    int input_index_of_consumer;
};
bool is_gather_pipe(const pipe_t &p);
bool is_multicast_pipe(const pipe_t &p);
bool is_scatter_pipe(const pipe_t &p);
bool is_gather_multicast_pipe(const pipe_t &p);
bool is_unicast_pipe(const pipe_t &p);

struct pipe_segment_id_t {
  unique_id_t pipe_id;
  int segment;

  bool operator==(pipe_segment_id_t const& rhs) const {
    return (this->pipe_id == rhs.pipe_id) && (this->segment == rhs.segment);
  }
};

struct pipe_segment_hash_t {
  unique_id_t pipe_id;
  pipe_output_list_t outputs;
  bool operator==(pipe_segment_hash_t const& rhs) const {
    return (this->pipe_id == rhs.pipe_id) && (this->outputs == rhs.outputs);
  }
};

}; // namespace router

namespace std {
template <>
struct hash<router::pipe_segment_id_t> {
  std::size_t operator()(router::pipe_segment_id_t const &o) const {
    std::size_t seed = 0;
    // Pipe ID will be above UNIQUE_BLOCK_ALIGN and segment will be below UNIQUE_BLOCK_ALIGN
    // so there shouldn't be any collision so we can just xor them
    seed = std::hash<std::size_t>()(o.pipe_id) | std::hash<std::size_t>()(o.segment);
    return seed;
  }
};
template <>
struct hash<router::pipe_segment_hash_t> {
  std::size_t operator()(router::pipe_segment_hash_t const &o) const {
    std::size_t seed = 0;
    // Pipe ID will be above UNIQUE_BLOCK_ALIGN and segment will be below UNIQUE_BLOCK_ALIGN
    // so there shouldn't be any collision so we can just xor them
    seed = std::hash<std::size_t>()(o.pipe_id);
    for (auto oid : o.outputs) {
      seed ^= std::hash<std::size_t>()(oid);
    }
    return seed;
  }
};


}