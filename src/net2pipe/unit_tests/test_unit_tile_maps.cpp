// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tile_maps.h"
#include "gtest/gtest.h"
#include <exception>


TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, NeedPhasedStack_NoStack) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        1 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        8 /* mblock_ublocks_m */, 
        1 /* mblock_ublocks_n */, 
        1 /* num_cores_r */, 
        1 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.apply_tm("vslice", {2});
    tile_map = tile_map.apply_tm("vslice", {2});

    EXPECT_FALSE(tile_map.need_phased_stack());
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, NeedPhasedStack_StackWithEnoughProducerBuffering) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        8 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        1 /* mblock_ublocks_m */, 
        1 /* mblock_ublocks_n */, 
        1 /* num_cores_r */, 
        1 /* num_cores_c */, 
        8 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.apply_tm("hstack", {4});

    EXPECT_FALSE(tile_map.need_phased_stack());
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, NeedPhasedStack_SliceStack) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        1 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        16 /* mblock_ublocks_m */, 
        1 /* mblock_ublocks_n */, 
        1 /* num_cores_r */, 
        1 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.apply_tm("vslice", {8});
    tile_map = tile_map.apply_tm("hstack", {4});

    EXPECT_FALSE(tile_map.need_phased_stack());
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, NeedPhasedStack_HasStacking) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        8 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        1 /* mblock_ublocks_m */, 
        1 /* mblock_ublocks_n */, 
        1 /* num_cores_r */, 
        1 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.apply_tm("hstack", {4});

    EXPECT_TRUE(tile_map.need_phased_stack());
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, CheckPhasedStack_ValidStacking) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        16 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        5 /* mblock_ublocks_m */, 
        17 /* mblock_ublocks_n */, 
        3 /* num_cores_r */, 
        1 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.unpad(2, 15);
    tile_map = tile_map.apply_tm("c_broadcast", {3});
    tile_map = tile_map.apply_tm("hstack", {4});
    tile_map = tile_map.pad(3, 12);

    EXPECT_TRUE(tile_map.need_phased_stack());
    EXPECT_NO_THROW(tile_map.check_phased_stack(
        2 /* consumer_ublock_tiles_r */,
        2 /* consumer_ublock_tiles_c */,
        2 /* consumer_mblock_ublocks_r */,
        9 /* consumer_mblock_ublocks_c */,
        4 /* consumer_num_cores_r */,
        2 /* consumer_num_cores_c */,
        false /* consumer_row_major_ublock_scan_order */
    ));
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, CheckPhasedStack_FailsWithTMOrdering) {
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        16 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        5 /* mblock_ublocks_m */, 
        17 /* mblock_ublocks_n */, 
        3 /* num_cores_r */, 
        1 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.unpad(2, 15);
    tile_map = tile_map.apply_tm("c_broadcast", {3});
    tile_map = tile_map.apply_tm("hstack", {4});
    tile_map = tile_map.apply_tm("r_broadcast", {2});
    tile_map = tile_map.pad(3, 12);

    EXPECT_TRUE(tile_map.need_phased_stack());

    // Cannot broadcast after stack TM.
    EXPECT_THROW(tile_map.check_phased_stack(
        4 /* consumer_ublock_tiles_r */,
        2 /* consumer_ublock_tiles_c */,
        2 /* consumer_mblock_ublocks_r */,
        9 /* consumer_mblock_ublocks_c */,
        4 /* consumer_num_cores_r */,
        2 /* consumer_num_cores_c */,
        false /* consumer_row_major_ublock_scan_order */
    ), std::exception);
}

TEST(ThreeDArrayTileSrcMap_ShapeTraceOnly, CheckPhasedStack_FailsWithUblockTileScanOrderConstraints) {
    // Example from pybuda#2286
    three_d_array_tile_src_map tile_map(
        "producer" /* producer_name */, 
        "consumer" /* consumer_name */, 
        313 /* t_dim */, 
        1 /* ublock_tiles_r */, 
        1 /* ublock_tiles_c */, 
        1 /* mblock_ublocks_m */, 
        1 /* mblock_ublocks_n */, 
        1 /* num_cores_r */, 
        4 /* num_cores_c */, 
        1 /* producer_output_buf_size_t */, 
        true /* producer_row_major_ublock_scan_order */, 
        true /* is_trace_shape_mode */);

    tile_map = tile_map.unpad(0, 0);
    tile_map = tile_map.apply_tm("vstack", {313});
    tile_map = tile_map.pad(2, 0);

    EXPECT_TRUE(tile_map.need_phased_stack());

    EXPECT_THROW(tile_map.check_phased_stack(
        3 /* consumer_ublock_tiles_r */,
        1 /* consumer_ublock_tiles_c */,
        105 /* consumer_mblock_ublocks_r */,
        1 /* consumer_mblock_ublocks_c */,
        1 /* consumer_num_cores_r */,
        4 /* consumer_num_cores_c */,
        true /* consumer_row_major_ublock_scan_order */
    ), std::exception);
}