// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_set>
#include <vector>

// clang-format off
#include "device/tt_xy_pair.h"

#include "model/typedefs.h"
// clang-format on

namespace pipegen2
{
class RGBasePipe;

// Enumerates different types of rational graph nodes.
enum class RGNodeType
{
    DramInput = 0,
    DramEmbeddingIndexInput,
    DramEmbeddingTableInput,
    DramOutput,
    DramOutputIntermed,
    Relay,
    EthernetRelay,
    NonSharedIntermed,
    PackerInput,
    PCIeStreaming,
    Virtual,
    SerialFork,
    SharedPackerIntermed,
    UnpackerOutput
};

// Returns appropriate name string for each node type.
std::string node_type_to_string(RGNodeType node_type);

#ifdef TT_DEBUG
// Returns appropriate color string for each node type, used for debug visualizer to visualize rational graph.
std::string node_type_to_color(RGNodeType node_type);
#endif

// Rational graph base node class.
class RGBaseNode
{
public:
    class BlockingParams
    {
    public:
        BlockingParams(
            unsigned int mblock_m,
            unsigned int mblock_n,
            unsigned int mblock_k,
            unsigned int ublock_rt,
            unsigned int ublock_ct) :
            m_mblock_m(mblock_m),
            m_mblock_n(mblock_n),
            m_mblock_k(mblock_k),
            m_ublock_rt(ublock_rt),
            m_ublock_ct(ublock_ct)
        {
        }

        BlockingParams(const BlockingParams&) = default;

        BlockingParams(BlockingParams&&) = default;

        unsigned int get_mblock_m() const { return m_mblock_m; }

        unsigned int get_mblock_n() const { return m_mblock_n; }

        unsigned int get_mblock_k() const { return m_mblock_k; }

        unsigned int get_ublock_rt() const { return m_ublock_rt; }

        unsigned int get_ublock_ct() const { return m_ublock_ct; }

    private:
        // Maps directly to netlist op mblock m dimension.
        unsigned int m_mblock_m;

        // Maps directly to netlist op mblock n dimension.
        unsigned int m_mblock_n;

        // Maps directly to netlist op mblock k dimension.
        unsigned int m_mblock_k;

        // Maps directly to netlist op ublock rt dimension.
        unsigned int m_ublock_rt;

        // Maps directly to netlist op ublock ct dimension.
        unsigned int m_ublock_ct;
    };

    virtual ~RGBaseNode() = default;

    const NodeId get_id() const { return m_id; }

    const RGNodeType get_node_type() const { return m_node_type; }

    const tt_cxy_pair& get_physical_location() const { return m_physical_location; }

    unsigned int get_tile_size() const { return m_tile_size; }

    unsigned int get_num_epoch_tiles() const { return m_num_epoch_tiles; }

    unsigned int get_size_tiles() const { return m_size_tiles; }

    unsigned int get_tiles_per_input() const { return m_tiles_per_input; }

    unsigned int get_scatter_gather_num_tiles() const { return m_scatter_gather_num_tiles; }

    bool has_input_pipe() const { return m_input_pipe != nullptr; }

    RGBasePipe* get_input_pipe() const { return m_input_pipe; }

    bool has_output_pipes() const { return !m_output_pipes.empty(); }

    const std::vector<RGBasePipe*>& get_output_pipes() const { return m_output_pipes; }

    const std::unordered_set<RGBasePipe*>& get_output_pipes_set() const { return m_output_pipes_set; }

    RGBasePipe* get_output_pipe() const;

    unsigned int get_output_count() const { return m_output_pipes.size(); }

    void set_input_pipe(RGBasePipe* pipe) { m_input_pipe = pipe; }

    void add_output_pipe(RGBasePipe* pipe);

    void clear_output_pipes() { m_output_pipes.clear(); }

    void remove_output_pipe(RGBasePipe* pipe);

    void replace_output_pipe(RGBasePipe* old_pipe, RGBasePipe* new_pipe);

protected:
    RGBaseNode(
        NodeId node_id,
        RGNodeType node_type,
        const tt_cxy_pair& physical_location,
        unsigned int size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input,
        unsigned int scatter_gather_num_tiles = 1) :
        m_id(node_id),
        m_node_type(node_type),
        m_physical_location(physical_location),
        m_size_tiles(size_tiles),
        m_tile_size(tile_size),
        m_num_epoch_tiles(num_epoch_tiles),
        m_tiles_per_input(tiles_per_input),
        m_scatter_gather_num_tiles(scatter_gather_num_tiles),
        m_input_pipe(nullptr)
    {
    }

    RGBaseNode(const RGBaseNode& other_node) :
        m_id(other_node.m_id),
        m_node_type(other_node.m_node_type),
        m_physical_location(other_node.m_physical_location),
        m_size_tiles(other_node.m_size_tiles),
        m_tile_size(other_node.m_tile_size),
        m_num_epoch_tiles(other_node.m_num_epoch_tiles),
        m_tiles_per_input(other_node.m_tiles_per_input),
        m_scatter_gather_num_tiles(other_node.m_scatter_gather_num_tiles),
        m_input_pipe(other_node.m_input_pipe)
    {
    }

    // Node ID, used for debug purposes.
    // Currently set to ID of the PipeGraph buffer this node was created from.
    NodeId m_id;

    // Node type.
    RGNodeType m_node_type;

    // Node physical location on the chip.
    tt_cxy_pair m_physical_location;

    // Size of buffer associated with this node in tiles.
    unsigned int m_size_tiles;

    // Tile size in bytes.
    unsigned int m_tile_size;

    // Total number of tiles that will move through this node in one epoch.
    unsigned int m_num_epoch_tiles;

    // TODO: fill in
    unsigned int m_tiles_per_input;

    // Number of tiles moved in one math iteration through this buffer or its scatter chunk.
    // TODO: We should sync with net2pipe crew and see if we can create a better structure of fields related to
    //       number of tiles in a buffer (tiles_per_input, size_tiles, scatter_gather_num_tiles, epoch_tiles).
    unsigned int m_scatter_gather_num_tiles;

    // Input pipe to this node.
    RGBasePipe* m_input_pipe;

    // Output pipes from this node.
    std::vector<RGBasePipe*> m_output_pipes;

    // TODO: convert to map from pipe to index in m_output_pipes so we don't have to search for it when trying to
    // remove it from the vector.
    // Output pipes from this node stored in a set for fast lookup.
    std::unordered_set<RGBasePipe*> m_output_pipes_set;
};
}  // namespace pipegen2