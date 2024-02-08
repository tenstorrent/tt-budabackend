// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/stream_node.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "pipegen2_utils.h"

namespace pipegen2
{
#ifdef TT_DEBUG
    std::string stream_type_to_color(StreamType stream_type)
    {
        switch (stream_type)
        {
            case StreamType::Multicast:
            case StreamType::PackerMulticast:
                return "#FF4500";
            case StreamType::Unpacker:
            case StreamType::Packer:
                return "#FFFF00";
            case StreamType::Relay:
            case StreamType::GatherRelay:
                return "#C0C0C0";
            case StreamType::Gather:
                return "#1E90FF";
            case StreamType::Intermed:
                return "#808080";
            default:
                return "#FFFFFF";
        }
    }
#endif

    std::string stream_type_to_string(StreamType stream_type)
    {
        switch (stream_type)
        {
            case StreamType::Multicast:
                return "Multicast";
            case StreamType::PackerMulticast:
                return "PackerMulticast";
            case StreamType::Unpacker:
                return "Unpacker";
            case StreamType::Packer:
                return "Packer";
            case StreamType::Relay:
                return "Relay";
            case StreamType::GatherRelay:
                return "GatherRelay";
            case StreamType::Gather:
                return "Gather";
            case StreamType::Intermed:
                return "Intermed";
            default:
                return "Unknown";
        }
    }

    StreamNode::StreamNode(StreamType stream_type,
                           const tt_cxy_pair& physical_location,
                           unsigned int operand_id) :
        m_stream_type(stream_type),
        m_physical_location(physical_location),
        m_stream_id(-1),
        m_operand_id(operand_id),
        m_has_assigned_buffer_address(false),
        m_is_deleted(false),
        m_max_num_tiles_per_phase(0)
    {
    }

    StreamNode::StreamNode(StreamType stream_type,
                           const tt_cxy_pair& physical_location,
                           const std::vector<PhaseInfo>& phases_info,
                           const unsigned int num_iterations_in_epoch,
                           const unsigned int max_num_tiles_per_phase,
                           const unsigned int operand_id) :
        m_stream_type(stream_type),
        m_physical_location(physical_location),
        m_stream_id(-1),
        m_operand_id(operand_id),
        m_has_assigned_buffer_address(false),
        m_is_deleted(false)
    {
        add_phase_configs(phases_info, max_num_tiles_per_phase);
        get_base_config().set_num_iters_in_epoch(num_iterations_in_epoch);
    }

    void StreamNode::add_phase_configs(const std::vector<PhaseInfo>& phases_info,
                                       const unsigned int max_num_tiles_per_phase)
    {
        set_max_num_tiles_per_phase(max_num_tiles_per_phase);

        for (const PhaseInfo& phase_info : phases_info)
        {
            StreamConfig phase_config;
            phase_config.set_num_msgs(phase_info.num_msgs);
            add_phase_config(phase_info.phase_offset, std::move(phase_config));
        }
    }

    void StreamNode::add_phase_configs(std::vector<PhaseConfig>&& phase_configs,
                                       const unsigned int max_num_tiles_per_phase)
    {
        set_max_num_tiles_per_phase(max_num_tiles_per_phase);

        m_phase_configs.insert(m_phase_configs.end(),
                               std::make_move_iterator(phase_configs.begin()),
                               std::make_move_iterator(phase_configs.end()));
    }

    void StreamNode::add_phase_config(PhaseId phase_id, StreamConfig&& config)
    {
        m_phase_configs.emplace_back(phase_id, std::move(config));
    }

    void StreamNode::sort_phases()
    {
        std::sort(m_phase_configs.begin(), m_phase_configs.end());
    }

    void StreamNode::update_phases(const std::vector<PhaseInfo>& new_phases_info,
                                   const unsigned int new_max_num_tiles_per_phase)
    {
        log_assert(!new_phases_info.empty(), "Expecting at least one new phase");
        log_assert(m_phase_configs.empty() || new_phases_info[0].phase_offset == m_phase_configs.at(0).phase_id,
                   "Expecting the same inital phase id when updating phases");

        m_phase_configs.clear();
        for (unsigned int i = 0; i < new_phases_info.size(); ++i)
        {
            StreamConfig phase_config;
            phase_config.set_num_msgs(new_phases_info[i].num_msgs);
            add_phase_config(new_phases_info[i].phase_offset, std::move(phase_config));
        }

        set_max_num_tiles_per_phase(new_max_num_tiles_per_phase);
    }

    void StreamNode::update_phases(std::vector<PhaseConfig>&& new_phase_configs,
                                   const unsigned int new_max_num_tiles_per_phase)
    {
        log_assert(!new_phase_configs.empty(), "Expecting at least one new phase config");
        log_assert(m_phase_configs.empty() || new_phase_configs[0].phase_id == m_phase_configs.at(0).phase_id,
                   "Expecting the same inital phase id when updating phases");

        m_phase_configs = std::move(new_phase_configs);

        set_max_num_tiles_per_phase(new_max_num_tiles_per_phase);
    }

    const NcriscConfig& StreamNode::get_base_ncrisc_config() const
    {
        return get_base_ncrisc_config();
    }

    NcriscConfig& StreamNode::get_base_ncrisc_config()
    {
        log_assert(
            !m_ncrisc_configs.empty(), "Attempting to read NCRISC config from stream that has no NCRISC configs set");

        return m_ncrisc_configs[0];
    }

    unsigned int StreamNode::get_num_tiles_in_iteration() const
    {
        if (m_phase_configs.empty())
        {
            return 0;
        }

        unsigned int num_tiles_in_iteration = 0;
        const unsigned int stream_initial_scatter_idx = m_phase_configs.at(0).config.get_scatter_idx().value_or(0);
        for (const PhaseConfig& phase_config : m_phase_configs)
        {
            unsigned int scatter_idx = phase_config.config.get_scatter_idx().value_or(stream_initial_scatter_idx);
            if (scatter_idx == stream_initial_scatter_idx)
            {
                num_tiles_in_iteration += phase_config.config.get_num_msgs().value();
            }
        }

        return num_tiles_in_iteration;
    }

    bool StreamNode::changes_destinations() const
    {
        if (m_phase_configs.size() == 1)
        {
            return false;
        }

        const StreamConfig& first_phase_config = m_phase_configs.front().config;
        if (!first_phase_config.get_dest().has_value())
        {
            // If the first phase has no destination, then the stream is not changing destinations.
            return false;
        }

        StreamNode* first_phase_dest = first_phase_config.get_dest().value().at(0);
        for (const PhaseConfig& phase_config : m_phase_configs)
        {
            const StreamConfig& config = phase_config.config;
            log_assert(config.get_dest().has_value(), "Expected all phases to have a destination");

            unsigned int current_unroll_iter = config.get_unroll_iter().value_or(0);
            if (current_unroll_iter != 0)
            {
                // If phase belongs to an unrolled iteration, then we exhausted all destination in the first
                // unrolled iteration. Therefore, the stream is not changing destinations.
                return false;
            }

            StreamNode* dest = config.get_dest().value().at(0);
            if (dest != first_phase_dest)
            {
                // If any phase has a different destination than the first phase, then the stream is changing
                // destinations.
                return true;
            }
        }
        return false;
    }

    bool StreamNode::changes_source() const
    {
        log_assert(m_phase_configs.size() > 0, "Must be called after phases are set");
        if (m_phase_configs.size() == 1)
        {
            return false;
        }
        const StreamConfig& first_phase_config = m_phase_configs.front().config;
        StreamNode* first_phase_src = first_phase_config.get_source().value_or(nullptr);
        if (!first_phase_src)
        {
            // If the first phase has no source, then the stream is not changing sources.
            return false;
        }

        for (const PhaseConfig& phase_config : m_phase_configs)
        {
            const StreamConfig& config = phase_config.config;
            log_assert(config.get_source().has_value(), "Expected all phases to have a source");
            if (config.get_source().value() != first_phase_src)
            {
                return true;
            }
        }

        return false;
    }

    void StreamNode::set_buffer_address(const unsigned int buffer_address)
    {
        // If stream has buf_full_size_bytes field set that means it is reading from scattered input and we have already
        // configured its configs to have buffer address set to corresponding offset, and now have only to increment it
        // by the base address. Otherwise we should only set buffer address of the first config to the base address.
        if (m_base_config.get_buf_full_size_bytes().has_value())
        {
            m_base_config.set_buf_base_addr(buffer_address);

            for (PhaseConfig& phase_config : m_phase_configs)
            {
                phase_config.config.set_buf_addr(phase_config.config.get_buf_addr().value() + buffer_address);
            }
        }
        else
        {
            m_base_config.set_buf_addr(buffer_address);
        }

        m_has_assigned_buffer_address = true;
    }

    void StreamNode::soft_delete()
    {
        m_phase_configs.clear();
        m_ncrisc_configs.clear();
        m_is_deleted = true;
    }


    void StreamNode::set_space_shared_with_stream(StreamNode* other_stream)
    {
        if (is_sharing_buffer())
        {
            // Buffer sharing is already configured, so we should not change it. Case when this can happen is when an
            // unpacker stream shares buffer with packer, but packer is forking to multiple streams, therefore unpacker
            // shares buffer with all packer streams. We will allow only the first packer stream to set the buffer
            // sharing.
            return;
        }
        m_base_config.set_space_shared_with_stream(other_stream);
    }

    std::vector<StreamNode*> StreamNode::get_unique_sources() const
    {
        std::unordered_set<StreamNode*> stream_sources;

        for (const PhaseConfig& phase_config: get_phase_configs())
        {
            if (phase_config.config.get_source().has_value())
            {
                stream_sources.insert(phase_config.config.get_source().value());
            }
        }

        return std::vector<StreamNode*>(std::make_move_iterator(stream_sources.begin()),
                                        std::make_move_iterator(stream_sources.end()));
    }

    std::vector<StreamNode*> StreamNode::get_unique_destinations() const
    {
        std::unordered_set<StreamNode*> stream_destinations;

        for (const PhaseConfig& phase_config: get_phase_configs())
        {
            if (!phase_config.config.get_dest().has_value())
            {
                continue;
            }

            for (StreamNode* dest : phase_config.config.get_dest().value())
            {
                stream_destinations.insert(dest);
            }
        }

        return std::vector<StreamNode*>(std::make_move_iterator(stream_destinations.begin()),
                                        std::make_move_iterator(stream_destinations.end()));
    }

    bool StreamNode::is_multicast_stream() const
    {
        return m_stream_type == StreamType::Multicast || m_stream_type == StreamType::PackerMulticast;
    }
} // namespace pipegen2