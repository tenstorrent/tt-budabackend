// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "model/typedefs.h"

namespace pipegen2
{
    class StreamNode;

    class StreamConfig
    {
    public:

#define ADD_FIELD_TO_MAP(Name)                                                                                         \
            do  {                                                                                                      \
                using Type = decltype(m_##Name)::value_type;                                                           \
                auto field_map = get_field_map<Type>();                                                                \
                field_map->emplace(                                                                                    \
                    #Name,                                                                                             \
                    make_tuple(                                                                                        \
                        &StreamConfig::get_##Name,                                                                     \
                        &StreamConfig::set_##Name,                                                                     \
                        &StreamConfig::opt_set_##Name)                                                                 \
                );                                                                                                     \
            } while(0);

        StreamConfig()
        {
            ADD_FIELD_TO_MAP(dest)
            ADD_FIELD_TO_MAP(source)
            ADD_FIELD_TO_MAP(msg_size)
            ADD_FIELD_TO_MAP(num_iters_in_epoch)
            ADD_FIELD_TO_MAP(num_unroll_iter)
            ADD_FIELD_TO_MAP(data_auto_send)
            ADD_FIELD_TO_MAP(reg_update_vc)
            ADD_FIELD_TO_MAP(buf_id)
            ADD_FIELD_TO_MAP(pipe_id)
            ADD_FIELD_TO_MAP(space_shared_with_operand)
            ADD_FIELD_TO_MAP(arb_group_size)
            ADD_FIELD_TO_MAP(buffer_intermediate)
            ADD_FIELD_TO_MAP(buf_addr)
            ADD_FIELD_TO_MAP(buf_base_addr)
            ADD_FIELD_TO_MAP(buf_full_size_bytes)
            ADD_FIELD_TO_MAP(dram_buf_noc_addr)
            ADD_FIELD_TO_MAP(buffer_size)
            ADD_FIELD_TO_MAP(tile_clear_granularity)
            ADD_FIELD_TO_MAP(fork_streams)
            ADD_FIELD_TO_MAP(num_fork_streams)
            ADD_FIELD_TO_MAP(group_priority)
            ADD_FIELD_TO_MAP(dram_io)
            ADD_FIELD_TO_MAP(dram_ram)
            ADD_FIELD_TO_MAP(dram_streaming)
            ADD_FIELD_TO_MAP(dram_input)
            ADD_FIELD_TO_MAP(dram_input_no_push)
            ADD_FIELD_TO_MAP(dram_output)
            ADD_FIELD_TO_MAP(dram_output_no_push)
            ADD_FIELD_TO_MAP(dram_writes_with_cmd_buf)
            ADD_FIELD_TO_MAP(ptrs_not_zero)
            ADD_FIELD_TO_MAP(producer_epoch_id)
            ADD_FIELD_TO_MAP(unroll_iter)
            ADD_FIELD_TO_MAP(incoming_data_noc)
            ADD_FIELD_TO_MAP(input_index)
            ADD_FIELD_TO_MAP(is_scatter_pack)
            ADD_FIELD_TO_MAP(buf_space_available_ack_thr)
            ADD_FIELD_TO_MAP(pipe_scatter_output_loop_count)
            ADD_FIELD_TO_MAP(num_scatter_inner_loop)
            ADD_FIELD_TO_MAP(remote_source)
            ADD_FIELD_TO_MAP(source_endpoint)
            ADD_FIELD_TO_MAP(local_sources_connected)
            ADD_FIELD_TO_MAP(local_stream_clear_num)
            ADD_FIELD_TO_MAP(remote_receiver)
            ADD_FIELD_TO_MAP(receiver_endpoint)
            ADD_FIELD_TO_MAP(local_receiver)
            ADD_FIELD_TO_MAP(local_receiver_tile_clearing)
            ADD_FIELD_TO_MAP(phase_auto_config)
            ADD_FIELD_TO_MAP(phase_auto_advance)
            ADD_FIELD_TO_MAP(next_phase_src_change)
            ADD_FIELD_TO_MAP(next_phase_dest_change)
            ADD_FIELD_TO_MAP(resend)
            ADD_FIELD_TO_MAP(num_msgs)
            ADD_FIELD_TO_MAP(legacy_pack)
            ADD_FIELD_TO_MAP(outgoing_data_noc)
            ADD_FIELD_TO_MAP(vc)
            ADD_FIELD_TO_MAP(output_index)
            ADD_FIELD_TO_MAP(moves_raw_data)
            ADD_FIELD_TO_MAP(mblock_k)
            ADD_FIELD_TO_MAP(mblock_m)
            ADD_FIELD_TO_MAP(mblock_n)
            ADD_FIELD_TO_MAP(ublock_ct)
            ADD_FIELD_TO_MAP(ublock_rt)
            ADD_FIELD_TO_MAP(batch_dim_size)
            ADD_FIELD_TO_MAP(r_dim_size)
            ADD_FIELD_TO_MAP(c_dim_size)
            ADD_FIELD_TO_MAP(zr_dim_size)
            ADD_FIELD_TO_MAP(zc_dim_size)
            ADD_FIELD_TO_MAP(tile_dim_r)
            ADD_FIELD_TO_MAP(tile_dim_c)
            ADD_FIELD_TO_MAP(skip_col_bytes)
            ADD_FIELD_TO_MAP(skip_col_tile_row_bytes)
            ADD_FIELD_TO_MAP(skip_col_row_bytes)
            ADD_FIELD_TO_MAP(skip_zcol_bytes)
            ADD_FIELD_TO_MAP(skip_col_zrow_bytes)
            ADD_FIELD_TO_MAP(stride)
            ADD_FIELD_TO_MAP(total_strides)
            ADD_FIELD_TO_MAP(stride_offset_size_bytes)
            ADD_FIELD_TO_MAP(num_mblock_buffering)
            ADD_FIELD_TO_MAP(num_msgs_in_block)
            ADD_FIELD_TO_MAP(scatter_idx)
            ADD_FIELD_TO_MAP(scatter_order_size)
            ADD_FIELD_TO_MAP(linked)
            ADD_FIELD_TO_MAP(padding_scatter_order_size)
            ADD_FIELD_TO_MAP(is_sending_tiles_out_of_order)
            ADD_FIELD_TO_MAP(scatter_list_stream_id)
            ADD_FIELD_TO_MAP(scatter_list_indicies_per_input)
            ADD_FIELD_TO_MAP(scatter_list_indicies_per_tile)
            ADD_FIELD_TO_MAP(dram_embeddings_row_shift)
            ADD_FIELD_TO_MAP(dram_embeddings_row_tiles)
            ADD_FIELD_TO_MAP(ncrisc_clear)
            ADD_FIELD_TO_MAP(eth_sender)
            ADD_FIELD_TO_MAP(eth_receiver)
            ADD_FIELD_TO_MAP(use_ethernet_fw_stream)
            ADD_FIELD_TO_MAP(hw_tilize)
            ADD_FIELD_TO_MAP(c_dim_loop_num_rows)
            ADD_FIELD_TO_MAP(tilize_row_col_offset)
            ADD_FIELD_TO_MAP(is_l1_producer)
            ADD_FIELD_TO_MAP(has_packer_mcast_opt)
            ADD_FIELD_TO_MAP(is_union_stream)
            ADD_FIELD_TO_MAP(is_reading_from_padding_table)
            ADD_FIELD_TO_MAP(overlay_blob_extra_size)
            ADD_FIELD_TO_MAP(shared_space_buffer_node_id)
            ADD_FIELD_TO_MAP(space_shared_with_stream)
            ADD_FIELD_TO_MAP(num_mcast_dests)
            ADD_FIELD_TO_MAP(src_dest_index)
            ADD_FIELD_TO_MAP(remote_src_is_mcast)
            ADD_FIELD_TO_MAP(data_buf_no_flow_ctrl)
            ADD_FIELD_TO_MAP(dest_data_buf_no_flow_ctrl)
            ADD_FIELD_TO_MAP(msg_info_buf_addr)
            ADD_FIELD_TO_MAP(follow_by_receiver_dummy_phase)
            ADD_FIELD_TO_MAP(follow_by_sender_dummy_phase)
            ADD_FIELD_TO_MAP(remote_src_update_noc)
            ADD_FIELD_TO_MAP(dram_scale_up_factor)
        }

#undef ADD_FIELD_TO_MAP

    private:
        // Mapping from member name to tuple of its getter and setters.
        template<typename T>
        using FieldMap = std::unordered_map<std::string,
                                            std::tuple<const std::optional<T>& (StreamConfig::*)() const,
                                                       void (StreamConfig::*)(T),
                                                       void (StreamConfig::*)(const std::optional<T>&)>>;

        // Since we can have different types of FieldMaps, we'll employ variant type that can be stored in a single map,
        // where we map type to the variant of FieldMap.
        // Note: if we need to support new member type, one must add concrete specialization of FieldMap to the variant.
        using FieldMapBase = std::variant<FieldMap<bool>,
                                          FieldMap<std::uint64_t>,
                                          FieldMap<unsigned int>,
                                          FieldMap<std::vector<StreamNode*>>,
                                          FieldMap<StreamNode*>,
                                          FieldMap<NOC_ROUTE>>;
        std::unordered_map<std::type_index, FieldMapBase> m_field_maps;

        // Returns appropriate FieldMap for a given type.
        template<typename T>
        FieldMap<T>* get_field_map()
        {
            auto fm_iter = m_field_maps.try_emplace(std::type_index(typeid(T)), FieldMap<T>()).first;
            return &std::get<FieldMap<T>>(fm_iter->second);
        }

    public:
        // Sets all values that are set in other config to this config.
        // If force_set is set to false, will skip the fields which are already set in this stream config.
        void apply(const StreamConfig& other_config, bool force_set = true)
        {
            for (const auto& kv : other_config.m_field_maps)
            {
                const auto& type_index = kv.first;
                const auto& other_fm = kv.second;
                std::visit([this, &type_index, &other_config, &force_set](auto&& arg)
                {
                    using T = std::decay_t<decltype(arg)>;
                    // We traverse all members and check if they contain value. Additionally, we could have a set of
                    // member names that have values and avoid traversing all fields.
                    // TODO rethink if we need that
                    for (const auto& [member_name, getter_setters] : arg)
                    {
                        auto getter = std::get<0>(getter_setters);

                        // Obtain optional variable from other config using its getter.
                        auto opt_var = (other_config.*getter)();
                        if (opt_var.has_value())
                        {
                            // Get appropriate map for the type from this stream config.
                            auto my_field_map = m_field_maps.at(type_index);

                            // Skip overriding if the field is already set.
                            auto getter_from_opt = std::get<0>((std::get<T>(my_field_map)).at(member_name));
                            auto this_opt_var = (this->*getter_from_opt)();
                            if (!force_set && this_opt_var.has_value())
                            {
                                continue;
                            }

                            // Get setter for the variable given by member_name for this stream config.
                            auto setter_from_opt = std::get<2>((std::get<T>(my_field_map)).at(member_name));

                            // Invoke the setter.
                            (this->*setter_from_opt)(opt_var);
                        }
                    }
                }, other_fm);
            }
        }

        // Resets all members to nullopt.
        void reset()
        {
            for (const auto& kv : m_field_maps)
            {
                std::visit([this](auto&& arg)
                {
                    for (const auto& [member_name, getter_setters] : arg)
                    {
                        auto opt_setter = std::get<2>(getter_setters);
                        // We must explicitly set nullopt as the argument here. The reason is that we can't store
                        // function arguments' default values inside a function container.
                        (this->*opt_setter)(std::nullopt);
                    }
                }, kv.second /* field_map */);
            }
        }

        // Overloaded += operator for easy appenditure stream configs.
        StreamConfig& operator+=(const StreamConfig& rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }

            apply(rhs);
            return *this;
        }

        // Reset all existing optionals and then set values as in the other config.
        StreamConfig& operator=(const StreamConfig& rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }

            reset();
            apply(rhs);
            return *this;
        }

#define DEFINE_MEMBER_AND_ACCESSOR(Type, Name)                                                                         \
    private:                                                                                                           \
        std::optional<Type> m_##Name;                                                                                  \
    public:                                                                                                            \
        const std::optional<Type>& get_##Name() const { return m_##Name; }                                             \
        void set_##Name(Type value) { m_##Name = value; }                                                              \
        void opt_set_##Name(const std::optional<Type>& value = std::nullopt) { m_##Name = value; }

        DEFINE_MEMBER_AND_ACCESSOR(std::vector<StreamNode*>, dest)
        DEFINE_MEMBER_AND_ACCESSOR(StreamNode*, source)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, msg_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_iters_in_epoch)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_unroll_iter)
        DEFINE_MEMBER_AND_ACCESSOR(bool, data_auto_send)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, reg_update_vc)
        DEFINE_MEMBER_AND_ACCESSOR(NodeId, buf_id)
        DEFINE_MEMBER_AND_ACCESSOR(NodeId, pipe_id)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, space_shared_with_operand)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, arb_group_size);
        DEFINE_MEMBER_AND_ACCESSOR(bool, buffer_intermediate)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, buf_addr)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, buf_base_addr)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, buf_full_size_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(std::uint64_t, dram_buf_noc_addr)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, buffer_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, tile_clear_granularity)
        DEFINE_MEMBER_AND_ACCESSOR(std::vector<StreamNode*>, fork_streams)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_fork_streams)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, group_priority)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_io)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_ram)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_streaming)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_input)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_input_no_push)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_output)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_output_no_push)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dram_writes_with_cmd_buf)
        DEFINE_MEMBER_AND_ACCESSOR(bool, ptrs_not_zero)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, producer_epoch_id)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, unroll_iter)
        DEFINE_MEMBER_AND_ACCESSOR(NOC_ROUTE, incoming_data_noc)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, input_index)
        DEFINE_MEMBER_AND_ACCESSOR(bool, is_scatter_pack);
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, buf_space_available_ack_thr)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, pipe_scatter_output_loop_count)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_scatter_inner_loop)
        DEFINE_MEMBER_AND_ACCESSOR(bool, remote_source)
        DEFINE_MEMBER_AND_ACCESSOR(bool, source_endpoint)
        DEFINE_MEMBER_AND_ACCESSOR(bool, local_sources_connected)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, local_stream_clear_num)
        DEFINE_MEMBER_AND_ACCESSOR(bool, remote_receiver)
        DEFINE_MEMBER_AND_ACCESSOR(bool, receiver_endpoint)
        DEFINE_MEMBER_AND_ACCESSOR(bool, local_receiver)
        DEFINE_MEMBER_AND_ACCESSOR(bool, local_receiver_tile_clearing)
        DEFINE_MEMBER_AND_ACCESSOR(bool, phase_auto_config)
        DEFINE_MEMBER_AND_ACCESSOR(bool, phase_auto_advance)
        DEFINE_MEMBER_AND_ACCESSOR(bool, next_phase_src_change)
        DEFINE_MEMBER_AND_ACCESSOR(bool, next_phase_dest_change)
        DEFINE_MEMBER_AND_ACCESSOR(bool, resend)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_msgs)
        DEFINE_MEMBER_AND_ACCESSOR(bool, legacy_pack)
        DEFINE_MEMBER_AND_ACCESSOR(NOC_ROUTE, outgoing_data_noc)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, vc)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, output_index)
        DEFINE_MEMBER_AND_ACCESSOR(bool, moves_raw_data)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, mblock_k)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, mblock_m)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, mblock_n)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, ublock_ct)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, ublock_rt)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, batch_dim_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, r_dim_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, c_dim_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, zr_dim_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, zc_dim_size)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, tile_dim_r)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, tile_dim_c)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, skip_col_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, skip_col_tile_row_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, skip_col_row_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, skip_zcol_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, skip_col_zrow_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, stride)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, total_strides)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, stride_offset_size_bytes)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_mblock_buffering)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_msgs_in_block)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, scatter_idx)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, scatter_order_size)
        DEFINE_MEMBER_AND_ACCESSOR(bool, linked)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, padding_scatter_order_size)
        DEFINE_MEMBER_AND_ACCESSOR(bool, is_sending_tiles_out_of_order)
        DEFINE_MEMBER_AND_ACCESSOR(StreamNode*, scatter_list_stream_id)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, scatter_list_indicies_per_input)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, scatter_list_indicies_per_tile)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, dram_embeddings_row_shift)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, dram_embeddings_row_tiles)
        DEFINE_MEMBER_AND_ACCESSOR(bool, ncrisc_clear)
        DEFINE_MEMBER_AND_ACCESSOR(bool, eth_sender)
        DEFINE_MEMBER_AND_ACCESSOR(bool, eth_receiver)
        DEFINE_MEMBER_AND_ACCESSOR(bool, use_ethernet_fw_stream)
        DEFINE_MEMBER_AND_ACCESSOR(bool, hw_tilize)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, c_dim_loop_num_rows)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, tilize_row_col_offset)
        DEFINE_MEMBER_AND_ACCESSOR(bool, is_l1_producer)
        DEFINE_MEMBER_AND_ACCESSOR(bool, has_packer_mcast_opt)
        DEFINE_MEMBER_AND_ACCESSOR(bool, is_union_stream)
        DEFINE_MEMBER_AND_ACCESSOR(bool, is_reading_from_padding_table)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, overlay_blob_extra_size)
        DEFINE_MEMBER_AND_ACCESSOR(NodeId, shared_space_buffer_node_id)
        DEFINE_MEMBER_AND_ACCESSOR(StreamNode*, space_shared_with_stream)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, num_mcast_dests)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, src_dest_index)
        DEFINE_MEMBER_AND_ACCESSOR(bool, remote_src_is_mcast)
        DEFINE_MEMBER_AND_ACCESSOR(bool, data_buf_no_flow_ctrl)
        DEFINE_MEMBER_AND_ACCESSOR(bool, dest_data_buf_no_flow_ctrl)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, msg_info_buf_addr)
        DEFINE_MEMBER_AND_ACCESSOR(bool, follow_by_receiver_dummy_phase)
        DEFINE_MEMBER_AND_ACCESSOR(bool, follow_by_sender_dummy_phase)
        DEFINE_MEMBER_AND_ACCESSOR(NOC_ROUTE, remote_src_update_noc)
        DEFINE_MEMBER_AND_ACCESSOR(unsigned int, dram_scale_up_factor)
#undef DEFINE_MEMBER_AND_ACCESSOR
    };
} // namespace pipegen2
