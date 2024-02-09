// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

enum epoch_cmds_e {
    INC = 0,
    DEC = 1,
    JMP = 2,
    END = 3,
    LOOP = 4
} ;

struct {
    std::int32_t num_epochs;
    epoch_descriptor_t epoch_data[32];
} program_descriptor_t;

struct {
    std::int32_t start_address;
    std::int32_t size_in_B;
} param_buf_descriptor_t;

struct {
    std::int32_t start_address;
    std::int32_t ??;
} io_q_descriptor_t;

struct {
    std::trisc0_image_base;
    std::trisc0_image_size;
    std::trisc1_image_base;
    std::trisc1_image_size;
    std::trisc2_image_base;
    std::trisc2_image_size;
    std::overlay_image_base;
    std::overlay_image_size;
} hex_images_descriptor_t;

struct {
    std::int32_t start_address;
    hex_images_descriptor_t hex_images;
    std::int32_t num_param_bufs;
    param_buf_descriptor_t param_bufs[8];
    std::int32_t num_io_queues;
    io_q_descriptor_t io_queues[8];

} epoch_descriptor_t;