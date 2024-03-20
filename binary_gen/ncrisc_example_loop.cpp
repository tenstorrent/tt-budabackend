// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

const uint TRISC0_BASE;
const uint l1_param_park_buffer_base[8];     // Where is this coming from ??
const uint l1_param_park_buffer_base[8];     // Where is this coming from ??   
const uint l1_param_park_buffer_base[8];     // Where is this coming from ??

uint main() {
    
    program_descriptor_t program = read_program_descriptor();      // From predefined location
    epoch = 0;
    while (epoch < program.num_epochs) {
        epoch_descriptor_t epoch_data_desc = get_epoch_data(epoch);
        
        // Fetch hex images
        read_from_dram(TRISC0_BASE, epoch_data_desc.hex_images.trisc0_base, epoch_data_desc.hex_images.trisc0_size);
        read_from_dram(TRISC1_BASE, epoch_data_desc.hex_images.trisc1_base, epoch_data_desc.hex_images.trisc1_size);
        read_from_dram(TRISC2_BASE, epoch_data_desc.hex_images.trisc2_base, epoch_data_desc.hex_images.trisc2_size);
        read_from_dram(OVERLAY_BASE, epoch_data_desc.hex_images.overlay_base, epoch_data_desc.hex_images.overlay_size);

        start_stream_config();      // give streams "green light" that blob is loaded

        // Read param buffers into park buffers which have streams associacted
        // Streams should take care of further distribution of params:
        //  a) local to core, no further distribution
        //  b) send to master
        //  c) local to core, send to different stream
        for (int pbuf=0; pbuf<epoch_data_desc.num_param_bufs; ++pbuf) {
            read_from_dram_to_stream(l1_param_park_buffer_base[pbuf], epoch_data_desc.param_bufs[pbuf].start_address, epoch_data_desc.param_bufs[pbuf].size_in_B);
        }

        signal_epoch_start();

        epoch++;                // read epoch command buffer and determine next epoch (inc, dec, jump, loop (this needs more thought) ??)

    }
}

