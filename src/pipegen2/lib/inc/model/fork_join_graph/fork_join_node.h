// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/resource_manager.h"
#include "model/pipe_graph/pg_buffer.h"

namespace pipegen2
{
    
class ForkJoinNode
{
public:
    ForkJoinNode(const PGBuffer* unpacker_buffer, 
                 const PGBuffer* packer_buffer, 
                 const ResourceManager* resource_manager);

    ForkJoinNode(const PGBuffer* packer_buffer, const ResourceManager* resource_manager);

    const std::string get_op_name() const { return m_op_name; }

    void set_output_node(const ForkJoinNode* output_node) { m_fork_join_output_node = output_node; }

    void set_input_node(const ForkJoinNode* input_node) { m_fork_join_input_node = input_node; }

    void set_num_tiles_per_iteration(const unsigned int num_tiles_per_iteration) 
    { 
        m_num_tiles_per_iteration = num_tiles_per_iteration; 
    }

    const unsigned int get_tiles_per_input_unpacker() const { return m_tiles_per_input_unpacker; }

    const unsigned int get_tiles_per_input_packer() const { return m_tiles_per_input_packer; }

    const unsigned int get_num_tiles_per_iteration() const { return m_num_tiles_per_iteration; }

    const unsigned int get_buf_size_mb() const { return m_buf_size_mb; }

    const NodeId get_buffer_id_of_next_unpacker() const;

    const NodeId get_unpacker_buffer_id() const { return m_unpacker_buffer_id; }

    const NodeId get_packer_buffer_id() const { return m_packer_buffer_id; }

    const tt_cxy_pair& get_physical_location() const { return m_physical_location; }

private:
    // Node to which the data is passed.
    const ForkJoinNode* m_fork_join_output_node;

    // Node to which the data is gathered from.
    const ForkJoinNode* m_fork_join_input_node;

    // Name of the corresponding op in the teslist.
    const std::string m_op_name;

    // Size of the mblock in tiles of the corresponding op.
    const unsigned int m_mblock_size_tiles;

    // Tiles per input of the unpacker of the corresponding op.
    const unsigned int m_tiles_per_input_unpacker;

    // Tiles per input of the packer of the corresponding op.
    const unsigned int m_tiles_per_input_packer;

    // Buf_size_mb value of the corresponding op.
    const unsigned int m_buf_size_mb;

    // Number of tiles per one firmware iteration.
    unsigned int m_num_tiles_per_iteration;

    // Buffer id of the unpacker in the fork_join node.
    const NodeId m_unpacker_buffer_id;

    // Buffer id of the packer in the fork_join node.
    const NodeId m_packer_buffer_id;

    // Physical location of the core.
    tt_cxy_pair m_physical_location;
};

} // namespace pipegen2