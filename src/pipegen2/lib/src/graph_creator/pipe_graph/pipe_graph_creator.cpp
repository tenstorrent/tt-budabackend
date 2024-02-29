// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/pipe_graph_creator.h"

#include "io/pipe_graph_parser.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    std::unique_ptr<PipeGraph> PipeGraphCreator::create_pipe_graph(const std::string& pipegen_yaml_path)
    {
        std::unique_ptr<PipeGraph> pipe_graph = std::make_unique<PipeGraph>();

        PipeGraphParser::parse_graph(*pipe_graph, pipegen_yaml_path);

        create_pipe_inputs_for_scatter_buffers(*pipe_graph);
        populate_buffers_map(*pipe_graph);
        find_pipe_graph_nodes_connections(*pipe_graph);

        return pipe_graph;
    }

    void PipeGraphCreator::create_pipe_inputs_for_scatter_buffers(const PipeGraph& pipe_graph)
    {
        for (const std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
        {
            if (!buffer->is_scatter())
            {
                continue;
            }

            m_scatter_buffer_pipe_inputs.emplace(buffer->get_id(), PGPipe::Input(buffer.get()));
            for (unsigned int i = 1; i < buffer->get_replicate_count(); ++i)
            {
                unsigned int offset = i * buffer->get_scatter_gather_num_tiles();
                NodeId id = buffer->get_id() + offset;
                m_scatter_buffer_pipe_inputs.emplace(id, PGPipe::Input(buffer.get(), offset));
                add_buffer_to_map_per_id(id, buffer.get());
            }
        }
    }

    void PipeGraphCreator::populate_buffers_map(PipeGraph& pipe_graph)
    {
        for (std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
        {
            add_buffer_to_map_per_id(buffer->get_id(), buffer.get());
        }
    }

    void PipeGraphCreator::add_buffer_to_map_per_id(NodeId buffer_id, PGBuffer* buffer)
    {
        if (m_buffers_per_id.find(buffer_id) != m_buffers_per_id.end())
        {
            throw InvalidPipeGraphSpecificationException(
                "Buffer with id " + std::to_string(buffer_id) + " already exists",
                buffer->get_logical_location());
        }

        m_buffers_per_id.emplace(buffer_id, buffer);
    }

    void PipeGraphCreator::find_pipe_graph_nodes_connections(PipeGraph& pipe_graph)
    {
        for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
        {
            for (NodeId buffer_id : pipe->get_input_buffers_ids())
            {
                connect_pipe_with_input_buffer(pipe.get(), buffer_id);
            }

            const std::vector<NodeId>& output_padding_buffers_ids = pipe->get_output_padding_buffers_ids();
            for (std::size_t scatter_index = 0; scatter_index < output_padding_buffers_ids.size();
                 scatter_index += pipe->get_consumer_repeat())
            {
                const NodeId buffer_id = output_padding_buffers_ids[scatter_index];
                if (buffer_id == PipeGraph::c_no_output_padding_buffer_id)
                {
                    continue;
                }
                connect_pipe_with_output_padding_buffer(scatter_index, buffer_id, pipe.get());
            }

            const std::vector<std::vector<NodeId>>& output_buffer_ids = pipe->get_output_buffers_ids();
            for (std::size_t scatter_index = 0; scatter_index < output_buffer_ids.size();
                 scatter_index += pipe->get_consumer_repeat())
            {
                for (NodeId buffer_id : output_buffer_ids[scatter_index])
                {
                    connect_pipe_with_output_buffer(pipe.get(), scatter_index, buffer_id);
                }
            }
        }

        // TODO: these handlers are mainly implemented to circumvent shortcomings of pipegen.yaml. Once net2pipe is
        // fixed, we will have all info needed to properly create a read-only pipe graph, with no need to change it.
        handle_embedding_connections(pipe_graph);
        handle_intermediate_buffers(pipe_graph);
        handle_end_to_end_queues(pipe_graph);
        handle_post_tm_pipes_optimization(pipe_graph);
    }

    void PipeGraphCreator::handle_embedding_connections(PipeGraph& pipe_graph)
    {
        for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
        {
            const PGBuffer* input_buffer = pipe->get_inputs()[0].get_buffer();
            if (input_buffer->is_embedding_table())
            {
                log_assert(pipe->get_output_buffers().size() == 1, "Embedding table pipe should have only one output");
                const PGBuffer* embedding_table_kernel_buffer = pipe->get_output_buffers()[0][0];
                const PGBuffer* embedding_index_kernel_buffer = find_embedding_index_kernel_buffer(
                    pipe_graph, embedding_table_kernel_buffer);
                log_assert(embedding_index_kernel_buffer != nullptr,
                           "Could not find embedding index kernel buffer for embedding table kernel buffer");
                connect_pipe_with_input_buffer(pipe.get(), embedding_index_kernel_buffer->get_id());
                copy_embedding_index_properties(embedding_index_kernel_buffer);
            }
        }
    }

    const PGBuffer* PipeGraphCreator::find_embedding_index_kernel_buffer(
        PipeGraph& pipe_graph,
        const PGBuffer* embedding_table_kernel_buffer)
    {
        // Go over buffers in pipe_graph and find buffer on same location as embedding_table_kernel_buffer but with
        // different id and that is input_operand
        for (std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
        {
            if (buffer->get_id() == embedding_table_kernel_buffer->get_id())
            {
                continue;
            }

            if (buffer->get_logical_location() == embedding_table_kernel_buffer->get_logical_location() &&
                buffer->is_input_operand())
            {
                return buffer.get();
            }
        }

        return nullptr;
    }

    void PipeGraphCreator::copy_embedding_index_properties(const PGBuffer* embedding_index_kernel_buffer)
    {
        const PGPipe* embedding_index_pipe = embedding_index_kernel_buffer->get_input_pipe();
        log_assert(embedding_index_pipe != nullptr,
                   "Could not find embedding index pipe for embedding index kernel buffer");
        log_assert(embedding_index_pipe->get_inputs().size() == 1,
                   "Embedding index pipe should have only one input");
        const PGBuffer* embedding_index_dram_buffer = embedding_index_pipe->get_inputs()[0].get_buffer();

        auto embedding_index_kernel_buffer_iter = m_buffers_per_id.find(embedding_index_kernel_buffer->get_id());
        log_assert(embedding_index_kernel_buffer_iter != m_buffers_per_id.end(),
                   "Could not find embedding index kernel buffer in buffers map");
        PGBuffer* embedding_index_kernel_buffer_non_const = embedding_index_kernel_buffer_iter->second;
        embedding_index_kernel_buffer_non_const->set_embedding_index(1);
        embedding_index_kernel_buffer_non_const->set_embedding_indices_per_input(
            embedding_index_dram_buffer->get_embedding_indices_per_input());
        embedding_index_kernel_buffer_non_const->set_embedding_indices_per_tile(
            embedding_index_dram_buffer->get_embedding_indices_per_tile());
    }

    void PipeGraphCreator::handle_intermediate_buffers(PipeGraph& pipe_graph)
    {
        // In order to fit intermediate buffers into our logic, we need to create a dummy intermed buffer which will be
        // input to a dummy pipe which has the real intermediate buffer as output. This way logic revolves around pipes
        // and data flow, not around lone nodes.
        std::vector<std::unique_ptr<PGBuffer>> newly_created_buffers;

        for (const std::unique_ptr<PGBuffer>& buf : pipe_graph.get_buffers())
        {
            if (buf->has_no_input() && buf->has_no_outputs())
            {
                // Create dummy input buffer as a clone of this intermediate buf.
                const PGBuffer* pipe_output_buf = buf.get();
                std::unique_ptr<PGBuffer> pipe_input_buf = std::make_unique<PGBuffer>(*pipe_output_buf);
                // TODO not the best way to uniquely identify this buffer. For now it has to be that way until
                // net2pipe is fixed.
                pipe_input_buf->set_id(pipe_output_buf->get_id() - 2);
                add_buffer_to_map_per_id(pipe_input_buf->get_id(), pipe_input_buf.get());

                // Create dummy pipe to connect the clone and original buf.
                // TODO not the best way to uniquely identify this pipe. For now it has to be that way until net2pipe
                // is fixed.
                std::unique_ptr<PGPipe> pipe = std::make_unique<PGPipe>(pipe_output_buf->get_id() - 1);
                // TODO temporary fix as a way to indicate this is a dummy pipe with invalid location. Planning to move
                // all of this to RG creator.
                pipe->add_mcast_core_logical_location(tt_cxy_pair(0 /* chip_id */,
                                                                  constants::unmapped_logical_location,
                                                                  constants::unmapped_logical_location));
                pipe->add_input_buffer_id(pipe_input_buf->get_id());
                connect_pipe_with_input_buffer(pipe.get(), pipe_input_buf->get_id());
                pipe->add_output_buffer_id(pipe_output_buf->get_id());
                connect_pipe_with_output_buffer(pipe.get(), 0 /* scatter_index */, pipe_output_buf->get_id());

                const PGBuffer* shared_buf = pipe_graph.get_shared_output_buffer(buf->get_id());
                if (shared_buf != nullptr)
                {
                    // In case this intermediate buffer has an associated (packer) buffer, we also make it an input to
                    // this dummy pipe in order to be able to reuse packer stream for intermed stream we create.
                    buf->set_shared_space_buffer_id(shared_buf->get_id());
                    pipe_input_buf->set_shared_space_buffer_id(shared_buf->get_id());
                    pipe->add_input_buffer_id(shared_buf->get_id());
                    connect_pipe_with_input_buffer(pipe.get(), shared_buf->get_id());
                }

                newly_created_buffers.push_back(std::move(pipe_input_buf));
                pipe_graph.add_pipe(std::move(pipe));
            }
        }

        for (std::unique_ptr<PGBuffer>& buf : newly_created_buffers)
        {
            pipe_graph.add_buffer(std::move(buf));
        }
    }

    void PipeGraphCreator::handle_end_to_end_queues(PipeGraph& pipe_graph)
    {
        // Create separate buffers for output to DRAM.
        for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
        {
            if (!pipe->has_single_output())
            {
                continue;
            }

            PGBuffer* e2e_buffer = pipe->get_single_output_buffer();
            if (!e2e_buffer->is_end_to_end_queue())
            {
                continue;
            }

            std::unique_ptr<PGBuffer> e2e_buffer_replica_for_dram_write = std::make_unique<PGBuffer>(*e2e_buffer);
            // This is okay, net2pipe is spacing out these IDs by couple of bits.
            e2e_buffer_replica_for_dram_write->set_id(e2e_buffer->get_id() + 1);

            // Connect pipe to e2e replica buffer and decouple e2e buffer and the pipe.
            e2e_buffer_replica_for_dram_write->set_input_pipe(pipe.get());
            pipe->replace_output_buffer(e2e_buffer, e2e_buffer_replica_for_dram_write.get());
            e2e_buffer->set_input_pipe(nullptr);

            pipe_graph.add_buffer(std::move(e2e_buffer_replica_for_dram_write));
        }
    }

    void PipeGraphCreator::connect_pipe_with_input_buffer(PGPipe* pipe, NodeId input_buffer_id)
    {
        auto buffer_it = m_buffers_per_id.find(input_buffer_id);
        if (buffer_it == m_buffers_per_id.end())
        {
            throw InvalidPipeGraphSpecificationException(
                "Pipe with id " + std::to_string(pipe->get_id()) +
                " has inexistent input buffer with id " + std::to_string(input_buffer_id),
                pipe->get_mcast_core_logical_locations().at(0));
        }

        buffer_it->second->add_output_pipe(pipe);

        auto pipe_input_it = m_scatter_buffer_pipe_inputs.find(input_buffer_id);
        if (pipe_input_it == m_scatter_buffer_pipe_inputs.end())
        {
            pipe->add_input_buffer(buffer_it->second);
        }
        else
        {
            pipe->add_input(pipe_input_it->second);
        }
    }

    void PipeGraphCreator::connect_pipe_with_output_padding_buffer(std::size_t scatter_index,
                                                                   NodeId output_padding_buffer_id,
                                                                   PGPipe* pipe)
    {
        auto buffer_it = m_buffers_per_id.find(output_padding_buffer_id);
        if (buffer_it == m_buffers_per_id.end())
        {
            throw InvalidPipeGraphSpecificationException(
                "Pipe with id " + std::to_string(pipe->get_id()) +
                " has inexistent output padding buffer with id " + std::to_string(output_padding_buffer_id),
                pipe->get_mcast_core_logical_locations().at(scatter_index));
        }

        pipe->add_output_padding_buffer(buffer_it->second, scatter_index / pipe->get_consumer_repeat());
        buffer_it->second->add_output_pipe(pipe);
    }

    void PipeGraphCreator::connect_pipe_with_output_buffer(PGPipe* pipe, std::size_t scatter_index,
                                                           NodeId output_buffer_id)
    {
        auto buffer_it = m_buffers_per_id.find(output_buffer_id);
        if (buffer_it == m_buffers_per_id.end())
        {
            throw InvalidPipeGraphSpecificationException(
                "Pipe with id " + std::to_string(pipe->get_id()) +
                " has inexistent output buffer with id " + std::to_string(output_buffer_id),
                pipe->get_mcast_core_logical_locations().at(scatter_index));
        }

        buffer_it->second->set_input_pipe(pipe);
        pipe->add_output_buffer(buffer_it->second, scatter_index / pipe->get_consumer_repeat());
    }

    void PipeGraphCreator::handle_post_tm_pipes_optimization(PipeGraph& pipe_graph)
    {
        // If graph is eligible for optimization, some buffers and pipes will be removed from it.
        std::unordered_set<PGPipe*> pipes_to_remove;
        std::unordered_set<PGBuffer*> buffers_to_remove;

        for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
        {
            if (!can_do_post_tm_connections_optimization(pipe.get()))
            {
                continue;
            }

            PGPipe* src_pipe = pipe.get();
            PGBuffer* relay_buffer = src_pipe->get_single_output_buffer();
            PGPipe* dest_pipe = relay_buffer->get_single_output_pipe();

            // Detach dest pipe from its inputs.
            dest_pipe->remove_all_inputs();

            std::unordered_set<PGBuffer*> replaced_buffers;

            // Detach inputs to src_pipe from it and reconnect them as inputs to dest_pipe.
            for (PGPipe::Input& src_pipe_input : src_pipe->get_inputs())
            {
                PGBuffer* src_pipe_input_buffer = src_pipe_input.get_buffer();

                if (replaced_buffers.find(src_pipe_input_buffer) == replaced_buffers.end())
                {
                    src_pipe_input_buffer->replace_output_pipe(src_pipe, dest_pipe);
                    dest_pipe->add_input_buffer_id(src_pipe_input_buffer->get_id());
                    replaced_buffers.insert(src_pipe_input_buffer);
                }
                dest_pipe->add_input_buffer(src_pipe_input_buffer, src_pipe_input.get_offset());
            }

            // Take some particular attribute values from src_pipe.
            dest_pipe->set_pipe_periodic_repeat(src_pipe->get_pipe_periodic_repeat());
            dest_pipe->set_incoming_noc_id(src_pipe->get_incoming_noc_id());
            dest_pipe->set_incoming_noc_vc(src_pipe->get_incoming_noc_vc());
            dest_pipe->set_dram_pipe_total_readers(src_pipe->get_dram_pipe_total_readers());
            dest_pipe->set_dram_pipe_reader_index(src_pipe->get_dram_pipe_reader_index());

            // src_pipe and relay_buffer remain pointing to their inputs/outputs, but they will be removed from pipe
            // graph so no need to detach them explicitly above.
            pipes_to_remove.insert(src_pipe);
            buffers_to_remove.insert(relay_buffer);
        }

        // Remove all source pipes and relay buffers from graph that were optimized away in previous optimization.
        pipe_graph.remove_pipes(pipes_to_remove);
        pipe_graph.remove_buffers(buffers_to_remove);
    }

    bool PipeGraphCreator::can_do_post_tm_connections_optimization(const PGPipe* src_pipe)
    {
        // First pipe in connection has to have a single output and scatter DRAM prefetch post TM inputs.
        if (!src_pipe->has_single_output() || !src_pipe->is_scatter_prefetch_post_tm())
        {
            return false;
        }

        // Buffer in between pipes has to be post TM relay non-forked buffer.
        const PGBuffer* middle_buffer = src_pipe->get_single_output_buffer();

        if (!middle_buffer->has_single_output() || !middle_buffer->is_post_tm_relay_buf())
        {
            return false;
        }

        // Second pipe in connection has to have only one input.
        const PGPipe* dest_pipe = middle_buffer->get_single_output_pipe();

        if (!dest_pipe->has_single_input())
        {
            return false;
        }

        return true;
    }
}