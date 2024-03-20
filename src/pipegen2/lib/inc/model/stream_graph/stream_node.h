// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "device/tt_xy_pair.h"

#include "model/data_flow/data_flow_info.h"
#include "model/typedefs.h"
#include "ncrisc_config.h"
#include "pipegen2_utils.h"
#include "phase_config.h"
#include "stream_config.h"

namespace pipegen2
{
    enum class StreamType
    {
        Multicast = 0,
        PackerMulticast,
        Unpacker,
        Packer,
        Relay,
        GatherRelay,
        Gather,
        Intermed
    };

#ifdef TT_DEBUG
    // Returns appropriate color string for each stream type, used for debug visualizer to visualize stream graph.
    std::string stream_type_to_color(StreamType stream_type);
#endif

    // Returns appropriate name string for each stream type.
    std::string stream_type_to_string(StreamType stream_type);

    // Structure that binds a stream node to the common config which will be used to create its phases. Used during pipe
    // streams creation, where each pipe can add some properties to that common config, and then eventually some pipe
    // streams creator will use it to create the phases for the stream.
    class StreamPhasesCommonConfig
    {
    public:
        StreamPhasesCommonConfig(StreamNode* stream_node) :
            m_stream_node(stream_node),
            m_common_config()
        {
        }

        StreamPhasesCommonConfig(StreamNode* stream_node, StreamConfig&& common_config) :
            m_stream_node(stream_node),
            m_common_config(std::move(common_config))
        {
        }

        StreamNode* get_stream_node() const { return m_stream_node; }

        const StreamConfig& get_common_config() const { return m_common_config; }

        void set_common_config(StreamConfig&& common_config) { m_common_config = std::move(common_config); }

    private:
        StreamNode* m_stream_node;

        StreamConfig m_common_config;
    };

    class StreamNode
    {
    public:
        StreamNode(StreamType stream_type,
                   const tt_cxy_pair& physical_location,
                   unsigned int operand_id = 0);

        StreamNode(StreamType stream_type,
                   const tt_cxy_pair& physical_location,
                   const std::vector<PhaseInfo>& phases_info,
                   unsigned int num_iterations_in_epoch,
                   unsigned int max_num_tiles_per_phase,
                   unsigned int operand_id = 0);

        void set_op_name(const std::string& op_name) { m_op_name = op_name; }

        std::string get_op_name() const { return m_op_name; }

        bool has_stream_id() const { return m_stream_id != -1; }

        StreamId get_stream_id() const { return m_stream_id; }

        StreamType get_stream_type() const { return m_stream_type; }

        void set_stream_type(StreamType stream_type) { m_stream_type = stream_type; }

        void set_physical_location(const tt_cxy_pair& loc) { m_physical_location = loc; };

        void set_operand_id(unsigned int id) { m_operand_id = id; }

        unsigned int get_operand_id() const { return m_operand_id; }

        const tt_cxy_pair& get_physical_location() const { return m_physical_location; }

        // Creates phase config for each phase info and adds them to the stream.
        void add_phase_configs(const std::vector<PhaseInfo>& phases_info, const unsigned int max_num_tiles_per_phase);

        // Adds given phase configs to the stream.
        void add_phase_configs(std::vector<PhaseConfig>&& phase_configs,
                               const unsigned int max_num_tiles_per_phase);

        // Moves phase config into stream.
        void add_phase_config(PhaseId phase_id, StreamConfig&& config);

        // Sorts phases by phase id.
        void sort_phases();

        // Updates configured phases of the stream. Existing phases are erased and new phases are configured. The base
        // config remains unchanged. Caller is responsible for providing new max num tiles per phase (it may be the same
        // as the old one).
        void update_phases(const std::vector<PhaseInfo>& new_phases_info,
                           const unsigned int new_max_num_tiles_per_phase);

        // Replaces current phase configs with the new one. Base config remains the same. Caller is responsible for
        // providing new max num tiles per phase (it may be the same as the old one).
        void update_phases(std::vector<PhaseConfig>&& new_phase_configs,
                           const unsigned int new_max_num_tiles_per_phase);

        // Gets a reference to the base config of the stream.
        StreamConfig& get_base_config() { return m_base_config; }

        // Moves base config into a stream.
        void set_base_config(StreamConfig&& stream_config) { m_base_config = std::move(stream_config); }

        // Gets a const reference to the base config of the stream.
        const StreamConfig& get_base_config() const { return m_base_config; }

        // Gets a reference to configs of all phases.
        std::vector<PhaseConfig>& get_phase_configs()
        {
            return m_phase_configs;
        }

        // Gets a const reference to configs of all phases.
        const std::vector<PhaseConfig>& get_phase_configs() const
        {
            return m_phase_configs;
        }

        // Gets an iterator to config of a specific phase.
        std::vector<PhaseConfig>::iterator get_phase_config_iterator(const uint32_t phase_index)
        {
            log_assert(phase_index < m_phase_configs.size(), "Invalid phase index");
            return m_phase_configs.begin() + phase_index;
        }

        // Gets a reference to config of a specific phase.
        StreamConfig& get_phase_config(const uint32_t phase_index)
        {
            log_assert(phase_index < m_phase_configs.size(), "Invalid phase index");
            return m_phase_configs[phase_index].config;
        }

        uint32_t get_num_phases() const { return m_phase_configs.size(); }

        // Moves NCRISC configs into stream.
        void set_ncrisc_configs(std::vector<NcriscConfig>&& configs)
        {
            m_ncrisc_configs = std::move(configs);
        }

        // Gets a const reference to the base NCRISC config of the stream.
        const NcriscConfig& get_base_ncrisc_config() const;

        // Gets a reference to the base NCRISC config of the stream.
        NcriscConfig& get_base_ncrisc_config();

        // Gets all NCRISC configs.
        const std::vector<NcriscConfig>& get_ncrisc_configs() const { return m_ncrisc_configs; }

        // Gets all NCRISC configs (non-const).
        std::vector<NcriscConfig>& get_ncrisc_configs() { return m_ncrisc_configs; }

        // Returns pointer to the NCRISC config of the stream if it is a dram writer. Otherwise returns nullptr.
        NcriscConfig* get_ncrisc_writer_config()
        {
            if (m_ncrisc_configs.size() != 1)
            {
                // More than 1 NCRISC config means that the stream is a dram reader. 0 configs means that the stream is
                // not a reader or a writer anyway.
                return nullptr;
            }
            if (m_ncrisc_configs[0].dram_output.value_or(false))
            {
                return &m_ncrisc_configs[0];
            }
            return nullptr;
        }

        // Assigns ID to the stream.
        void assign_stream_id(StreamId stream_id) { m_stream_id = stream_id; }

        // Returns total number of tiles the stream is configured to transfer in an iteration. For scattered streams, we
        // take into account only the number of tiles that are streamed to a single destination stream.
        unsigned int get_num_tiles_in_iteration() const;

        unsigned int get_num_iterations_in_epoch() const
        {
            log_assert(m_base_config.get_num_iters_in_epoch().has_value(), "Stream has no num iterations in epoch");
            return m_base_config.get_num_iters_in_epoch().value();
        }

        PhaseId get_last_phase_offset() const
        {
            return m_phase_configs.back().phase_id;
        }

        // Returns whether the stream changes destination streams between phases.
        bool changes_destinations() const;

        // Returns whether the stream changes source between phases.
        bool changes_source() const;

        void set_buffer_address(const unsigned int buffer_address);

        bool has_assigned_buffer_address() const { return m_has_assigned_buffer_address; }

        bool is_deleted() const { return m_is_deleted; }

        // Marks the stream as deleted and clears the phase and ncrisc configs.
        void soft_delete();

        bool is_sharing_buffer() const { return m_base_config.get_space_shared_with_stream().has_value(); }

        void set_space_shared_with_stream(StreamNode* other_stream);

        // Returns a list of unique stream nodes whose dest is this stream.
        std::vector<StreamNode*> get_unique_sources() const;

        // Returns a list of unique stream nodes whose source is this stream.
        std::vector<StreamNode*> get_unique_destinations() const;

        // Returns true if this stream is a multicast stream.
        bool is_multicast_stream() const;

        unsigned int get_max_num_tiles_per_phase() const { return m_max_num_tiles_per_phase; }

    private:
        void set_max_num_tiles_per_phase(unsigned int max_num_tiles_per_phase)
        {
            log_assert(max_num_tiles_per_phase > 0, "Max num tiles per phase must be positive");
            m_max_num_tiles_per_phase = max_num_tiles_per_phase;
        }

        // Stream type used for stream id assignment.
        StreamType m_stream_type;

        // Stream physical location on the chip.
        tt_cxy_pair m_physical_location;

        // Stream ID.
        StreamId m_stream_id;

        // Used for streams that are associated with a kernel operand. Id dictates which stream would be assigned to it.
        unsigned int m_operand_id;

        // Whether the stream has assigned buffer address.
        bool m_has_assigned_buffer_address;

        // Base config of the stream. Stream's config may change from phase to phase. This config is used as a base for
        // all other configs.
        StreamConfig m_base_config;

        // Vector of phase configs. For initial phase the full config is stored. For every subsequent phase it is
        // sufficient to store diff relative to the previous phase.
        std::vector<PhaseConfig> m_phase_configs;

        // List of NCRISC configs of this stream. One config per unique dram buffer that stream reads from or writes to.
        std::vector<NcriscConfig> m_ncrisc_configs;

        // Indicates if this stream has been deleted - deleted streams will be ignored when processing stream graph.
        bool m_is_deleted;

        // Maximum number of tiles this stream can transfer in a phase. More tiles than this number in a phase can cause
        // stream hangs. This number limits how many phases can be optimized into a single phase.
        unsigned int m_max_num_tiles_per_phase;

        // Netlist op name. Experimental purpose only.
        std::string m_op_name;
    };
}