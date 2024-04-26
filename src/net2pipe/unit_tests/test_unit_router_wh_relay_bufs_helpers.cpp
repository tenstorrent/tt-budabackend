// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// #include "router.hpp"
// #include "net2pipe.h"
// #include "test_unit_common.hpp"
// #include "common/buda_soc_descriptor.h"

// #include "gtest/gtest.h"


// using router::unique_id_t;
// using router::router_buffer_info_t;
// using router::Router;
// using n2p::UNIQUE_ID_ALIGN;




// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case1) {
//     const std::set<int> all_dram_rows = {0};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0)};
//     const std::unordered_set<int> &consumer_buffer_rows = {0};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     ASSERT_TRUE(result.size() == 1);
//     bool first_result_selected_row_is_0 = std::get<0>(result.at(0)) == 0;
//     ASSERT_TRUE(first_result_selected_row_is_0);
//     bool first_result_relay_buf_count_is_0 = std::get<1>(result.at(0)) == 0;
//     ASSERT_TRUE(first_result_relay_buf_count_is_0);
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case2) {
//     const std::set<int> all_dram_rows = {0};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0)};
//     const std::unordered_set<int> &consumer_buffer_rows = {1};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     ASSERT_TRUE(result.size() == 1);
//     bool first_result_selected_row_is_0 = std::get<0>(result.at(0)) == 0;
//     ASSERT_TRUE(first_result_selected_row_is_0);
//     ASSERT_EQ(std::get<1>(result.at(0)), 1);
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case3) {
//     const std::set<int> all_dram_rows = {0,1};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0), tt_cxy_pair(0,0,1)};
//     const std::unordered_set<int> &consumer_buffer_rows = {1};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 2}, {1, 1}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case4) {
//     const std::set<int> all_dram_rows = {0};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0)};
//     const std::unordered_set<int> &consumer_buffer_rows = {0, 1};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 1}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case5) {
//     const std::set<int> all_dram_rows = {0};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0)};
//     const std::unordered_set<int> &consumer_buffer_rows = {1, 2};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 1}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case6) {
//     const std::set<int> all_dram_rows = {0, 1};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0), tt_cxy_pair(0,0,1)};
//     const std::unordered_set<int> &consumer_buffer_rows = {2, 3};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 2}, {1, 2}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case7) {
//     const std::set<int> all_dram_rows = {0,1};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0),tt_cxy_pair(0,0,1), tt_cxy_pair(0,0,1)};
//     const std::unordered_set<int> &consumer_buffer_rows = {0};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 2}, {1, 2}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case8) {
//     const std::set<int> all_dram_rows = {0,1};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0),tt_cxy_pair(0,0,1), tt_cxy_pair(0,0,1)};
//     const std::unordered_set<int> &consumer_buffer_rows = {1};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 3}, {1, 1}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufHelpers, TestFunction_ComputeRelayBufferCountForEachSelectedPhysicalDramRow_Case9) {
//     const std::set<int> all_dram_rows = {0,1};
//     const std::vector<tt_cxy_pair> &input_queue_buffer_locations = {tt_cxy_pair(0,0,0),tt_cxy_pair(0,0,1), tt_cxy_pair(0,0,1)};
//     const std::unordered_set<int> &consumer_buffer_rows = {2};
//     const auto &result = router::compute_relay_buffer_count_for_each_selected_physical_dram_row(
//         all_dram_rows,
//         input_queue_buffer_locations, 
//         consumer_buffer_rows
//     );

//     std::unordered_map<int, int> expected_row_to_relay_count = {{0, 3}, {1, 2}};

//     ASSERT_TRUE(result.size() == expected_row_to_relay_count.size());
//     for (const auto &[row, relay_count] : result) {
//         ASSERT_EQ(expected_row_to_relay_count.at(row), relay_count);
//     }
// }