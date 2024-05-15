// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "device/soc_info.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/stream_graph/ncrisc_config.h"

namespace pipegen2
{
    class DramInputNode;
    class DramNodeInterface;
    class DramOutputIntermedNode;
    class DramOutputNode;

    class NcriscCreator
    {
    public:
        // Virtual destructor, necessary for base class.
        virtual ~NcriscCreator() = default;

        // Creates list of NCRISC configs used to read data from DRAM. Each unique input of the given pipe is assigned
        // with a unique config.
        // TODO: since total_readers and reader_index are properties of specific dram pipe classes, now we pass them as
        // function parameters. Instead we could have shared config for dram pipes that is part of base pipe and access
        // it through base pipe pointer.
        std::vector<NcriscConfig> configure_dram_ncrisc_reader(const DataFlowInfo& data_flow_info,
                                                               const RGBasePipe* rg_pipe,
                                                               const std::vector<int>& input_total_readers,
                                                               const std::vector<int>& input_reader_index,
                                                               const unsigned int max_dram_input_buffer_size_tiles,
                                                               const SoCInfo* soc_info);

        // Create a list of NCRISC configs used to read data from DRAM in post TM prefetch mode. Each unique input of 
        // the given pipe is assigned with a unique config.
        std::vector<NcriscConfig> configure_prefetch_post_tm_dram_ncrisc_reader(
                                                                    const DataFlowInfo& data_flow_info,
                                                                    const RGBasePipe* rg_pipe,
                                                                    const std::vector<int>& input_total_readers,
                                                                    const std::vector<int>& input_reader_index,
                                                                    const unsigned int max_dram_input_buffer_size_tiles,
                                                                    const SoCInfo* soc_info);

        // Creates list of NCRISC configs used to read data from DRAM for tilizer op. Each unique input of the given
        // pipe is assigned with a unique config.
        std::vector<NcriscConfig> configure_dram_tilizer_ncrisc_reader(
                                                                    const DataFlowInfo& data_flow_info,
                                                                    const RGBasePipe* rg_pipe,
                                                                    const std::vector<int>& input_total_readers,
                                                                    const std::vector<int>& input_reader_index,
                                                                    const unsigned int max_dram_input_buffer_size_tiles,
                                                                    const SoCInfo* soc_info);

        // Creates NCRISC config used to read data from PCIe.
        NcriscConfig configure_pcie_ncrisc_reader(const DataFlowInfo& data_flow_info,
                                                  const RGBasePipe* rg_pipe,
                                                  const SoCInfo* soc_info);

        // Creates an NCRISC config used to read data from DRAM in prefetch phase.
        // TODO: Currently this is only used for prefetch Pre-TM case, however it should be used for both pre and post
        // TM, since they are configured in the exact same way. Problem is that for prefetch Post-TM, pipegen v1 will
        // use the values computed dram io code path to check if it can merge dram and the unpacker streams, and then
        // override the ncrisc config, so we would get mismatches. After we test perf, we should rename this function
        // to 'configure_prefetch_ncrisc_reader' and use it to configure NCRISC for both prefetch types.
        NcriscConfig configure_prefetch_pre_tm_ncrisc_reader(const RGBasePipe* rg_pipe,
                                                             const SoCInfo* soc_info,
                                                             const DramInputNode* dram_input_node);

        // Creates NCRISC config used to write data to DRAM.
        NcriscConfig configure_dram_ncrisc_writer(const DataFlowInfo& data_flow_info,
                                                  const RGBasePipe* rg_pipe,
                                                  const SoCInfo* soc_info);

        // Creates NCRISC config used to write data to PCIe.
        NcriscConfig configure_pcie_ncrisc_writer(const DataFlowInfo& data_flow_info,
                                                  const RGBasePipe* rg_pipe);

        // Creates NCRISC config used to write accumulated data from L1 to DRAM for DRAM output intermed stream.
        NcriscConfig configure_dram_output_intermed_ncrisc_writer(const DramOutputIntermedNode* intermed_node,
                                                                  const SoCInfo* soc_info);

    protected:
        // Hiding base constructor.
        NcriscCreator() = default;

    private:
        // Creates list of NCRISC configs used to read data from DRAM. Each unique input of the given pipe is assigned
        // with a unique config.
        std::vector<NcriscConfig> configure_dram_io_or_prefetch_post_tm_ncrisc_reader(
                                                                const DataFlowInfo& data_flow_info,
                                                                const RGBasePipe* rg_pipe,
                                                                const std::vector<int>& input_total_readers,
                                                                const std::vector<int>& input_reader_index,
                                                                const SoCInfo* soc_info,
                                                                const unsigned int max_dram_input_buffer_size_tiles,
                                                                const bool prepare_offsets_for_tilizer,
                                                                const bool is_prefetch_post_tm);

        // Computes dram scatter offsets for all inputs of a given pipe. Scatter offsets for all inputs are placed into
        // first NCRISC config, because that's how blobgen expects to read them.
        void fill_dram_scatter_offsets(
            const RGBasePipe* rg_pipe,
            const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config,
            const bool prepare_offsets_for_tilizer,
            std::vector<NcriscConfig>& ncrisc_configs);

        // Computes dram scatter offsets for tilizer op.
        void fill_tilizer_dram_scatter_offsets(
            const RGBasePipe* rg_pipe,
            std::vector<std::uint64_t>& merged_dram_scatter_offsets);

        // Computes regular dram scatter offsets.
        void fill_regular_dram_scatter_offsets(
            const RGBasePipe* rg_pipe,
            const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config,
            std::vector<std::uint64_t>& merged_dram_scatter_offsets);

        // Compresses dram scatter offsets into compact form which is later decompressed in firmware.
        void compress_dram_scatter_offsets(
            const RGBasePipe* rg_pipe,
            const bool compress_offsets_for_tilizer,
            NcriscConfig& ncrisc_config);

        // Fills dram readers pipe properties in created NCRISC configs.
        void fill_dram_readers_properties(
            const RGBasePipe* rg_pipe,
            const std::vector<int>& input_total_readers,
            const std::vector<int>& input_reader_index,
            const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config);

        // Calculates GCD of all input chunks to the pipe.
        unsigned int calculate_dram_pipe_scatter_chunk_size(const DataFlowInfo& data_flow_info,
                                                            const RGBasePipe* rg_pipe);

        // Calculates read chunk size for every input to the pipe and returns minimum of those.
        unsigned int calculate_dram_pipe_read_chunk_size(const DataFlowInfo& data_flow_info,
                                                         const std::vector<const RGBaseNode*>& unique_input_dram_nodes,
                                                         const unsigned int max_dram_input_buffer_size_tiles);

        // Calculates read chunk size for input node by considering all the output pipes and their buffers outgoing from
        // this node.
        unsigned int calculate_dram_input_read_chunk_size(const DataFlowInfo& data_flow_info,
                                                          const DramInputNode* dram_input_node,
                                                          const unsigned int max_dram_input_buffer_size_tiles);

        // Calculates GCD of all non-virtual buffers that are downstream of the given pipe.
        void get_clear_granularity_of_downstream_buffers(const RGBasePipe* rg_pipe,
                                                         const RGBaseNode* rg_node,
                                                         unsigned int* clear_granularity);

        // Calculates max chunk of data a pipe can read from dram at a time.
        unsigned int calculate_max_chunk_size_for_pipe(const RGBasePipe* rg_pipe,
                                                       const DataFlowInfo& data_flow_info,
                                                       const unsigned int max_num_tiles_per_phase);

        // Calculates max bytes which can be transferred in a single dram read NOC transaction.
        unsigned int get_dram_read_max_transfer_size_bytes(const unsigned int dram_input_node_tile_size,
                                                           const unsigned int max_dram_input_buffer_size_tiles);

        // Creates NCRISC read config for a DRAM pipe with respect to the given input DRAM node.
        NcriscConfig create_dram_ncrisc_read_config(const RGBaseNode* input_node,
                                                    const RGBasePipe* rg_pipe,
                                                    const SoCInfo* soc_info,
                                                    const unsigned int dram_buf_read_chunk_size_tiles,
                                                    const unsigned int dram_scatter_chunk_size_tiles,
                                                    const bool is_prefetch_post_tm,
                                                    const unsigned int dram_buf_size_factor);

        // Creates NCRISC config for reading.
        NcriscConfig create_ncrisc_read_config(const unsigned int num_msgs,
                                               const unsigned int msg_size,
                                               const std::uint64_t dram_buf_noc_addr,
                                               const unsigned int dram_buf_size_tiles,
                                               const unsigned int dram_buf_size_bytes,
                                               const unsigned int dram_buf_size_q_slots,
                                               const unsigned int dram_buf_read_chunk_size_tiles,
                                               const bool is_ram,
                                               const bool is_padding);

        // Creates NCRISC config for writing.
        NcriscConfig create_ncrisc_write_config(const std::vector<PhaseInfo>& phases,
                                                const unsigned int msg_size,
                                                const std::uint64_t dram_buf_noc_addr,
                                                const unsigned int dram_buf_size_tiles,
                                                const unsigned int dram_buf_size_bytes,
                                                const unsigned int dram_buf_size_q_slots,
                                                const unsigned int dram_buf_write_chunk_size_tiles,
                                                const bool is_ram);

        // Creates NCRISC config for writing.
        NcriscConfig create_ncrisc_write_config(const unsigned int num_msgs,
                                                const unsigned int msg_size,
                                                const std::uint64_t dram_buf_noc_addr,
                                                const unsigned int dram_buf_size_tiles,
                                                const unsigned int dram_buf_size_bytes,
                                                const unsigned int dram_buf_size_q_slots,
                                                const unsigned int dram_buf_write_chunk_size_tiles,
                                                const bool is_ram);

        // Calculates write chunk size for pipe writing to DRAM.
        unsigned int calculate_dram_buf_write_chunk_size_tiles(const DataFlowInfo& data_flow_info,
                                                               const RGBasePipe* rg_pipe,
                                                               const DramOutputNode* dram_output_node);

        // Checks whether given transfer_chunk_size_tiles satisfies transfer limits. If the limits are satisfied it just
        // returns the current size. Otherwise, if the limits aren't satisfied, then tries to decrease the chunk size as
        // little as possible to satisfy the limits.
        static unsigned int get_transfer_chunk_size_tiles(unsigned int transfer_chunk_size_tiles,
                                                          unsigned int min_transfer_chunk_size_tiles,
                                                          unsigned int tile_size_bytes,
                                                          unsigned int max_transfer_size_tiles,
                                                          unsigned int max_transfer_size_bytes);

        // Maximum number of tiles we can transfer across noc in one request.
        // Helper function that returns whether current transfer chunk satisfies given limits.
        static bool is_transfer_chunk_size_within_limits(unsigned int transfer_chunk_size_tiles,
                                                         unsigned int tile_size_bytes,
                                                         unsigned int max_transfer_size_tiles,
                                                         unsigned int max_transfer_size_bytes);

        // Returns appropriate dram buffer NOC address depending whether buffer is remote_io or not.
        static std::uint64_t get_dram_buffer_noc_address(const DramNodeInterface* dram_node, ChipId chip_id,
                                                         const SoCInfo* soc_info);

        // Return the appropriate buf size factor for the prefetch post TM case.
        const unsigned int get_dram_buf_size_factor(const DramInputNode* first_dram_input_node, 
                                                    bool is_prefetch_post_tm) const;

        // Flag indicating that a given dram scatter offset is read from the padding buffer.
        constexpr static std::uint64_t c_is_offset_padding_address_flag = 0x4000000000000000ULL;

        // Flag indicating that a given dram scatter offset is invalid, instead of reading from that offset
        // firmware will load zero tiles.
        constexpr static std::uint64_t c_invalid_dram_scatter_offset_flag = 0xFFFFFFFFUL;
    };
}