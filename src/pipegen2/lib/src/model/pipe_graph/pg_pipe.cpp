// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/pipe_graph/pg_pipe.h"

#include <set>

#include "pipegen2_utils.h"

namespace pipegen2
{
    PGPipe::PGPipe() : PGPipe(-1)
    {
    }

    PGPipe::PGPipe(NodeId id) :
        m_id(id),
        m_pipe_periodic_repeat(1),
        m_consumer_repeat(1),
        m_ethernet_channel(-1),
        m_incoming_noc_id(0),
        m_incoming_noc_vc(0),
        m_outgoing_noc_id(0),
        m_outgoing_noc_vc(0),
        m_mmio_pipe(false),
        m_mmio_pipe_downstream(false),
        m_ethernet_pipe(false),
        m_disable_gather_optimization(false),
        m_packer_multicast_optimization_enabled(false),
        m_op_input_dram_io_buf_size_tiles(0)
    {
    }

    const std::vector<PGPipe::Input>& PGPipe::get_inputs(const std::size_t scatter_index) const
    {
        return is_output_padding(scatter_index) ?
               m_scatter_index_to_output_padding_inputs.at(scatter_index) : m_inputs;
    }

    std::vector<PGPipe::Input>& PGPipe::get_inputs(const std::size_t scatter_index)
    {
        return const_cast<std::vector<PGPipe::Input>&>(const_cast<const PGPipe*>(this)->get_inputs(scatter_index));
    }

    void PGPipe::remove_all_inputs()
    {
        m_inputs.clear();
        m_input_buffers_ids.clear();
    }

    bool PGPipe::is_output_padding(std::size_t scatter_index) const
    {
        return m_scatter_index_to_output_padding_inputs.find(scatter_index) !=
               m_scatter_index_to_output_padding_inputs.end();
    }

    std::vector<const PGBuffer*> PGPipe::get_unique_input_buffers() const
    {
        std::vector<const PGBuffer*> unique_input_buffers;
        std::unordered_set<const PGBuffer*> unique_input_buffers_set;
        for (const PGPipe::Input& input : m_inputs)
        {
            const PGBuffer* buffer = input.get_buffer();
            if (unique_input_buffers_set.find(buffer) == unique_input_buffers_set.end())
            {
                unique_input_buffers.push_back(buffer);
                unique_input_buffers_set.insert(buffer);
            }
        }

        return unique_input_buffers;
    }

    const PGBuffer* PGPipe::get_single_output_buffer() const
    {
        log_assert(has_single_output(), "Pipe does not have only one output buffer!");
        return m_output_buffers[0][0];
    }

    PGBuffer* PGPipe::get_single_output_buffer()
    {
        return const_cast<PGBuffer*>(const_cast<const PGPipe*>(this)->get_single_output_buffer());
    }

    std::vector<const PGBuffer*> PGPipe::get_unique_output_padding_buffers() const
    {
        std::unordered_set<const PGBuffer*> unique_output_padding_buffers_set;
        std::vector<const PGBuffer*> unique_output_padding_buffers;
        for (const auto& [scatter_index, output_padding_inputs] : m_scatter_index_to_output_padding_inputs)
        {
            log_assert(output_padding_inputs.size() > 0,
                       "Expecting at least one scatter padding input for every scatter padding index");

            const PGBuffer* output_padding_buffer = output_padding_inputs[0].get_buffer();
            // It is enough to only look at the first buffer since pipe can read from at most one output padding buffer
            // per every scatter index.
            if (unique_output_padding_buffers_set.find(output_padding_buffer) == 
                unique_output_padding_buffers_set.end())
            {
                unique_output_padding_buffers.push_back(output_padding_buffer);
                unique_output_padding_buffers_set.insert(output_padding_buffer);
            }
        }

        return unique_output_padding_buffers;
    }

    std::vector<const PGBuffer*> PGPipe::get_unique_input_buffers_including_output_padding() const
    {
        // A buffer can be both input (padding) buffer and output padding buffer, so we can't simply concatenate the two
        // lists as it is possible that we would end up with duplicated buffers in the list. 
        // Still, we need to use vectors to ensure the determinism of the output.
        std::vector<const PGBuffer*> all_unique_input_buffers = get_unique_input_buffers();
        std::unordered_set<const PGBuffer*> all_unique_input_buffers_set;
        std::copy(all_unique_input_buffers.begin(), all_unique_input_buffers.end(), 
                  std::inserter(all_unique_input_buffers_set, all_unique_input_buffers_set.end()));
        for (const PGBuffer* output_padding_buffer : get_unique_output_padding_buffers())
        {
            if (all_unique_input_buffers_set.find(output_padding_buffer) == all_unique_input_buffers_set.end())
            {
                all_unique_input_buffers.push_back(output_padding_buffer);
                all_unique_input_buffers_set.insert(output_padding_buffer);
            }
        }

        return all_unique_input_buffers;
    }

    bool PGPipe::has_consumer_duplicates() const
    {
        return m_consumer_repeat > 1 || has_output_list_duplicates();
    }

    bool PGPipe::has_output_list_duplicates() const
    {
        // There is no default implementation for std::has<std::vector<NodeId>> so ordered set is used instead of the
        // unordered one.
        std::set<std::vector<NodeId>> seen_output_buffers;
        for (std::size_t i = 0; i < m_output_buffers_ids.size(); i += m_consumer_repeat)
        {
            const std::vector<NodeId>& output_buffers = m_output_buffers_ids[i];
            if (seen_output_buffers.find(output_buffers) != seen_output_buffers.end())
            {
                return true;
            }

            seen_output_buffers.insert(output_buffers);
        }

        return false;
    }

    void PGPipe::add_output_buffer(PGBuffer* buffer, std::size_t scatter_index)
    {
        while (m_output_buffers.size() <= scatter_index)
        {
            m_output_buffers.emplace_back(std::vector<PGBuffer*>());
        }
        m_output_buffers[scatter_index].push_back(buffer);
    }

    void PGPipe::add_output_padding_buffer(PGBuffer* output_padding_buffer, std::size_t scatter_index)
    {
        const unsigned int num_msgs_per_scatter_index = get_num_msgs_per_scatter_index();

        log_assert(num_msgs_per_scatter_index % output_padding_buffer->get_scatter_gather_num_tiles() == 0,
                   "Expecting output padding buffer to have scatter_gather_num_tiles which divides total number of"
                   "messages pipe transfers per scatter index");

        const unsigned int num_output_padding_scatter_inputs =
            num_msgs_per_scatter_index / output_padding_buffer->get_scatter_gather_num_tiles();

        std::vector<Input> output_padding_scatter_input_list(
            num_output_padding_scatter_inputs, PGPipe::Input(output_padding_buffer));
        m_scatter_index_to_output_padding_inputs.emplace(scatter_index, std::move(output_padding_scatter_input_list));
    }

    void PGPipe::replace_output_buffer(PGBuffer* old_buffer, PGBuffer* new_buffer)
    {
        for (std::vector<PGBuffer*>& scatter_output_buffers : m_output_buffers)
        {
            for (std::size_t i = 0; i < scatter_output_buffers.size(); ++i)
            {
                if (scatter_output_buffers[i] == old_buffer)
                {
                    scatter_output_buffers[i] = new_buffer;
                }
            }
        }
    }

    unsigned int PGPipe::get_num_msgs_per_scatter_index() const
    {
        unsigned int num_msgs = 0;
        for (const PGPipe::Input& input : m_inputs)
        {
            num_msgs += input.get_buffer()->get_scatter_gather_num_tiles();
        }

        return num_msgs;
    }

    bool PGPipe::is_connecting_l1_buffers() const
    {
        const std::vector<const PGBuffer*> unique_input_buffers = get_unique_input_buffers();

        // It is enough to check only first scatter output because we don't support mixed type pipe scatter outputs.
        const std::vector<PGBuffer*>& first_dest_buffers = m_output_buffers[0];

        const bool pipe_has_l1_source_buffers = std::all_of(
            unique_input_buffers.begin(), unique_input_buffers.end(), [](const PGBuffer* source_buffer)
            {
                return source_buffer->is_located_in_l1();
            });

        const bool pipe_has_l1_dest_buffers = std::all_of(
            first_dest_buffers.begin(), first_dest_buffers.end(), [](const PGBuffer* dest_buffer)
            {
                return dest_buffer->is_located_in_l1();
            });

        return pipe_has_l1_source_buffers && pipe_has_l1_dest_buffers;
    }

    bool PGPipe::has_non_prefetch_pre_tm_dram_input() const
    {
        return std::all_of(m_inputs.begin(), m_inputs.end(), [](const PGPipe::Input& pipe_input)
        {
            const PGBuffer* input_buffer = pipe_input.get_buffer();

            return input_buffer->is_non_prefetch_pre_tm_dram_input();
        });
    }

    bool PGPipe::has_single_buffer_input() const
    {
        return get_unique_input_buffers().size() == 1;
    }

    bool PGPipe::is_direct_intermediate_pipe() const
    {
        return (has_single_input() && m_inputs[0].get_buffer()->is_intermediate_operand() &&
                has_single_output() && m_output_buffers[0][0]->is_intermediate_operand());
    }

    bool PGPipe::is_join_intermediate_pipe() const
    {
        return (has_single_output() && m_output_buffers[0][0]->is_intermediate_operand() && m_inputs.size() == 2 &&
                (m_inputs[0].get_buffer()->is_intermediate_operand() ||
                 m_inputs[1].get_buffer()->is_intermediate_operand()));
    }

    bool PGPipe::is_scatter_prefetch_post_tm() const
    {
        bool pipe_has_scatter_prefetch_post_tm_inputs = std::any_of(
            m_inputs.begin(), m_inputs.end(), [](const PGPipe::Input& pipe_input)
            {
                return pipe_input.get_buffer()->is_scatter_prefetch_post_tm();
            });

        bool pipe_has_non_scatter_prefetch_post_tm_inputs = std::any_of(
            m_inputs.begin(), m_inputs.end(), [](const PGPipe::Input& pipe_input)
            {
                return !pipe_input.get_buffer()->is_scatter_prefetch_post_tm();
            });

        log_assert(pipe_has_scatter_prefetch_post_tm_inputs != pipe_has_non_scatter_prefetch_post_tm_inputs,
                   "Pipe can not mix scatter prefetch post-TM inputs with other types of inputs!");

        // If pipe has some scatter prefetch post TM inputs, they all are such, due to the assert above.
        return pipe_has_scatter_prefetch_post_tm_inputs;
    }

}