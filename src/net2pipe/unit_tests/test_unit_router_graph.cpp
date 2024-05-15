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



// // static const std::unique_ptr<buda_SocDescriptor> wh_soc_desc = load_test_wh_soc_descriptor();

// void validate_pipe_connectivity(
//     Router &router,
//     unique_id_t pipe_id, 
//     std::vector<unique_id_t> expected_input_ids, 
//     std::vector<router::router_buffer_info_t> expected_input_buffers,
//     std::vector<unique_id_t> expected_output_ids, 
//     std::vector<router::router_buffer_info_t> expected_output_buffers
// ) {
//     ASSERT_TRUE(router.get_pipes().find(pipe_id) != router.get_pipes().end());
//     const auto &pipe = router.get_pipes().at(pipe_id);

//     ASSERT_EQ(pipe.input_buffer_ids.size(), expected_input_ids.size());
//     for (int i = 0; i < expected_input_ids.size(); i++) {
//         unique_id_t expected_buf_id = expected_input_ids.at(i);
//         const auto &expected_buf = expected_input_buffers.at(i);
//         ASSERT_EQ(pipe.input_buffer_ids.at(i), expected_buf_id);

//         ASSERT_TRUE(router.get_buffer_map().find(expected_buf_id) != router.get_buffer_map().end());
//         const auto &buf = router.get_buffer_map().at(expected_buf_id);
//         ASSERT_TRUE(buf == expected_buf);

//         ASSERT_TRUE(router.get_buffer_output_pipes().find(expected_buf_id) != router.get_buffer_output_pipes().end());
//     }

//     ASSERT_EQ(pipe.output_buffer_ids().size(), expected_output_ids.size());
//     for (int i = 0; i < expected_output_ids.size(); i++) {
//         unique_id_t expected_buf_id = expected_output_ids.at(i);
//         const auto &expected_buf = expected_output_buffers.at(i);
//         ASSERT_TRUE(pipe.output_buffer_ids().at(i) == expected_buf_id);

//         ASSERT_TRUE(router.get_buffer_map().find(expected_buf_id) != router.get_buffer_map().end());
//         const auto &buf = router.get_buffer_map().at(expected_buf_id);
//         ASSERT_TRUE(buf == expected_buf);

//         ASSERT_TRUE(router.get_buffer_input_pipes().find(expected_buf_id) != router.get_buffer_input_pipes().end());
//     }
// }

// TEST(Net2PipeRouterGraph, TestConnectBuffersWithPipe) {
//     const auto &[buf_id_1, buf_1] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_2, buf_2] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 2, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 3, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_4, buf_4] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 4, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_5, buf_5] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 5, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_6, buf_6] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 6, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_7, buf_7] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 7, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_8, buf_8] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 0, 1), tt::RouterBufferType::Relay);

//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {};
//     std::unordered_map<unique_id_t, pipe_t> pipes = {};
//     std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
//     std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
//     std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> op_input_buf_map = {};
//     std::map<std::string, std::map<int, std::map<int, unique_id_t>>> op_intermediate_buf_map = {};
//     std::map<std::string, std::map<int, std::map<int, unique_id_t>>> op_output_buf_map = {};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1},
//         {buf_id_2, buf_2},
//         {buf_id_3, buf_3},
//         {buf_id_4, buf_4},
//         {buf_id_5, buf_5},
//         {buf_id_6, buf_6},
//         {buf_id_7, buf_7},
//         {buf_id_8, buf_8}
//     };
//     for (const auto &[id, buf] : buffer_map) {
//         buffer_output_pipes[id] = {};
//     }

//     auto router = Router(        
//         *(wh_soc_desc.get()),
//         {0},
//         ClusterGraph(*(wh_2chip_cluster.get())),

//         UNIQUE_ID_ALIGN,
//         1,
//         unit_test_unique_id,

//         queue_map,
//         buffer_map,
//         pipes,
//         buffer_output_pipes,
//         buffer_input_pipes,
//         op_input_buf_map,
//         op_intermediate_buf_map,
//         op_output_buf_map
//     );

//     auto unicast_pipe_id = router.create_unicast_pipe_connection(buf_id_1, buf_id_2, router.get_buffer_location(buf_id_2));
//     log_debug(tt::LogTest, "Validate connect unicast pipe");
//     validate_pipe_connectivity(
//         router,
//         unicast_pipe_id, 
//         {buf_id_1}, {buf_1},
//         {buf_id_2}, {buf_2}
//     );

//     auto multicast_pipe_id = router.create_multicast_pipe_connection(buf_id_2, {buf_id_3, buf_id_4}, router.get_buffer_location(buf_id_3));
//     log_debug(tt::LogTest, "Validate connect multicast pipe");
//     validate_pipe_connectivity(
//         router,
//         multicast_pipe_id, 
//         {buf_id_2}, {buf_2},
//         {buf_id_3, buf_id_4}, {buf_3, buf_4}
//     );

//     log_debug(tt::LogTest, "Validate connect gather pipe");
//     auto gather_pipe_id = router.create_gather_pipe_connection({buf_id_3, buf_id_4}, buf_id_5, router.get_buffer_location(buf_id_5));
//     validate_pipe_connectivity(
//         router,
//         gather_pipe_id, 
//         {buf_id_3, buf_id_4}, {buf_3, buf_4},
//         {buf_id_5}, {buf_5}
//     );

//     log_debug(tt::LogTest, "Validate connect gather multicast pipe");
//     auto gather_multicast_pipe_id = router.create_gather_multicast_pipe_connection({buf_id_5, buf_id_6}, {buf_id_7, buf_id_8}, router.get_buffer_location(buf_id_7));
//     validate_pipe_connectivity(
//         router,
//         gather_multicast_pipe_id, 
//         {buf_id_5, buf_id_6}, {buf_5, buf_6},
//         {buf_id_7, buf_id_8}, {buf_7, buf_8}
//     );

// }

// TEST(Net2PipeRouterGraph, TestReplacePipeInput) {
//     const auto &[buf_id_1, buf_1] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_2, buf_2] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 2, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 3, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_4, buf_4] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 4, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_5, buf_5] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 5, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_6, buf_6] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 6, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_7, buf_7] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 7, 0), tt::RouterBufferType::Relay);
//     const auto &[buf_id_8, buf_8] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 0, 1), tt::RouterBufferType::Relay);
//     const auto &[buf_id_9, buf_9] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay);
//     const auto &[buf_id_10, buf_10] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 2, 1), tt::RouterBufferType::Relay);

//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {};
//     std::unordered_map<unique_id_t, pipe_t> pipes = {};
//     std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
//     std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
//     std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> op_input_buf_map = {};
//     std::map<std::string, std::map<int, std::map<int, unique_id_t>>> op_intermediate_buf_map = {};
//     std::map<std::string, std::map<int, std::map<int, unique_id_t>>> op_output_buf_map = {};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1},
//         {buf_id_2, buf_2},
//         {buf_id_3, buf_3},
//         {buf_id_4, buf_4},
//         {buf_id_5, buf_5},
//         {buf_id_6, buf_6},
//         {buf_id_7, buf_7},
//         {buf_id_8, buf_8},
//         {buf_id_9, buf_9},
//         {buf_id_10, buf_10}
//     };
//     for (const auto &[id, buf] : buffer_map) {
//         buffer_output_pipes[id] = {};
//     }

//     auto router = Router(        
//         *(wh_soc_desc.get()),
//         {0},
//         ClusterGraph(*(wh_2chip_cluster.get())),

//         UNIQUE_ID_ALIGN,
//         1,
//         unit_test_unique_id,

//         queue_map,
//         buffer_map,
//         pipes,
//         buffer_output_pipes,
//         buffer_input_pipes,
//         op_input_buf_map,
//         op_intermediate_buf_map,
//         op_output_buf_map
//     );

//     auto pipe_1_id = router.create_unicast_pipe_connection(buf_id_1, buf_id_2, router.get_buffer_location(buf_id_2));
//     router.replace_pipe_input(pipe_1_id, buf_id_1, buf_id_7);
//     log_debug(tt::LogTest, "Validate replace unicast pipe operand");
//     validate_pipe_connectivity(
//         router,
//         pipe_1_id, 
//         {buf_id_7}, {buf_7},
//         {buf_id_2}, {buf_2}
//     );

//     auto pipe_2_id = router.create_gather_pipe_connection({buf_id_3, buf_id_4, buf_id_5}, buf_id_6, router.get_buffer_location(buf_id_6));
//     router.replace_pipe_input(pipe_2_id, buf_id_3, buf_id_8);
//     log_debug(tt::LogTest, "Validate replace gather pipe operand 0");
//     validate_pipe_connectivity(
//         router,
//         pipe_2_id, 
//         {buf_id_8, buf_id_4, buf_id_5}, {buf_8, buf_4, buf_5},
//         {buf_id_6}, {buf_6}
//     );
//     router.replace_pipe_input(pipe_2_id, buf_id_4, buf_id_9);
//     log_debug(tt::LogTest, "Validate replace gather pipe operand 1");
//     validate_pipe_connectivity(
//         router,
//         pipe_2_id, 
//         {buf_id_8, buf_id_9, buf_id_5}, {buf_8, buf_9, buf_5},
//         {buf_id_6}, {buf_6}
//     );
//     router.replace_pipe_input(pipe_2_id, buf_id_5, buf_id_10);
//     log_debug(tt::LogTest, "Validate replace gather pipe operand 2");
//     validate_pipe_connectivity(
//         router,
//         pipe_2_id,
//         {buf_id_8, buf_id_9, buf_id_10}, {buf_8, buf_9, buf_10},
//         {buf_id_6}, {buf_6}
//     );
// }



// // TESTS TO ADD:
// // 1) Add relay buffer from dram -> relay -> buffer,
// //    - validate that relay core has 1 dram input stream in use
// //    - validate that relay core has 1 tile of l1 in use
// //    - validate that relay core has 1 relay buf stream in use

// // 2) Add dram -> buf -> dram
// //    - validate that buf core has 1 dram input and 1 dram output stream in use   

// // 3) Add dram (multicast) -> relay -> {buffers...} (x5)
// //    - validate that no more than 4 relay buffers are mapped to a core (otherwise they exceed the limit of 4 multicast streams per core)
// //        - or rather fill all the multicast streams for a core and make sure it can't be allocated to