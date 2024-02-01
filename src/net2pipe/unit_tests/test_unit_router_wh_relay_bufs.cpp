// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// #include "router.hpp"
// #include "net2pipe.h"
// #include "test_unit_common.hpp"
// #include "common/tt_cluster_graph.hpp"
// #include "common/buda_soc_descriptor.h"
// #include "router/router_passes.h"
// #include "gtest/gtest.h"


// using router::unique_id_t;
// using router::router_buffer_info_t;
// using router::Router;
// using n2p::UNIQUE_ID_ALIGN;

// // TODO(snijjar): use proper gtest setup/teardown in a way that this is shared by all tests
// // static const std::unique_ptr<buda_SocDescriptor> wh_soc_desc = load_test_wh_soc_descriptor();


// std::tuple<std::unique_ptr<Router>, std::vector<unique_id_t>, std::vector<unique_id_t>> generate_multicast_scenario(
//     const buda_SocDescriptor &soc_desc, const tt_cxy_pair &dram_queue_cores, const std::vector<tt_cxy_pair> &gather_core_locations
// ) {
//     const auto &[queue_id, queue, buf_id_1, buf_1] = generate_dummy_router_queue(0, 0, soc_desc, 1);
//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {{queue_id, queue}};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1}
//     };

//     std::vector<unique_id_t> multicast_target_buf_ids = {};
//     for (const auto &loc : gather_core_locations) {
//         const auto &[buf_id, buf] = generate_dummy_test_router_buffer(loc, tt::RouterBufferType::Input);
//         buffer_map.insert({buf_id, buf});
//         multicast_target_buf_ids.push_back(buf_id);
//     }
    
//     auto router = generate_test_router(soc_desc, 1, buffer_map, queue_map);

//     auto multicast_pipe_id = router->create_multicast_pipe_connection(buf_id_1, multicast_target_buf_ids, router->get_buffer_location(multicast_target_buf_ids.at(0)));

//     return {std::move(router), std::vector<unique_id_t>{buf_id_1}, multicast_target_buf_ids};
// }

// TEST(Net2PipeRouterWhA0RelayBufs, TestInsertWhUnicastRelayBuffer) {
//     const auto &[queue_id, queue, buf_id_1, buf_1] = generate_dummy_router_queue(0, 0, *(wh_soc_desc.get()), 1);
//     const auto &[buf_id_2, buf_2] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 5), tt::RouterBufferType::Input);

//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {{queue_id, queue}};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1},
//         {buf_id_2, buf_2},
//     };

//     auto router = generate_test_router(*(wh_soc_desc.get()), 1, buffer_map, queue_map);

//     auto unicast_pipe_id = router->create_unicast_pipe_connection(buf_id_1, buf_id_2, router->get_buffer_location(buf_id_2));

//     router::insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(*(router.get()));;

//     ASSERT_EQ(router->get_pipes().size(), 2);
//     ASSERT_EQ(router->get_buffer_map().size(), 3);

//     auto is_relay_buffer = [](std::pair<unique_id_t, router_buffer_info_t> iter) -> bool {
//         unique_id_t buf_id = iter.first;
//         const router_buffer_info_t &buffer = iter.second;
//         if (buffer.is_queue()) {
//             return false;
//         }

//         return buffer.info().type() == tt::RouterBufferType::Relay;
//     };

//     auto relay_buffer_id_iter = std::find_if(router->get_buffer_map().begin(), router->get_buffer_map().end(), is_relay_buffer);
//     ASSERT_NE(relay_buffer_id_iter, router->get_buffer_map().end());

//     // assert no other relay buffers
//     const unique_id_t relay_buffer_id = relay_buffer_id_iter->first;
//     const auto relay_buffer = relay_buffer_id_iter->second;
//     ASSERT_TRUE(std::find_if(++relay_buffer_id_iter, router->get_buffer_map().end(), is_relay_buffer) == router->get_buffer_map().end());

//     const auto &relay_input_pipe_id = router->get_buffer_input_pipes().at(relay_buffer_id);
//     const auto &dram_to_relay_pipe = router->get_pipes().at(relay_input_pipe_id);
//     ASSERT_TRUE(dram_to_relay_pipe.input_buffer_ids.size() == 1);
//     ASSERT_TRUE(dram_to_relay_pipe.input_buffer_ids.at(0) == buf_id_1);
//     ASSERT_TRUE(dram_to_relay_pipe.output_buffer_ids().size() == 1);
//     ASSERT_TRUE(dram_to_relay_pipe.output_buffer_ids().at(0) == relay_buffer_id);

//     const auto &relay_output_pipe_ids = router->get_buffer_output_pipes().at(relay_buffer_id);
//     ASSERT_TRUE(relay_output_pipe_ids.size() == 1);
//     const auto &relay_to_input_pipe = router->get_pipes().at(*(relay_output_pipe_ids.begin()));
//     ASSERT_TRUE(relay_to_input_pipe.input_buffer_ids.size() == 1);
//     ASSERT_TRUE(relay_to_input_pipe.input_buffer_ids.at(0) == relay_buffer_id);
//     ASSERT_TRUE(relay_to_input_pipe.output_buffer_ids().size() == 1);
//     ASSERT_TRUE(relay_to_input_pipe.output_buffer_ids().at(0) == buf_id_2);
// }

// TEST(Net2PipeRouterWhA0RelayBufs, TestInsertWhMulticastRelayBuffer) {

//     auto [router, queue_buffer_ids, consumer_buffer_ids] = generate_multicast_scenario(*(wh_soc_desc.get()), tt_cxy_pair(0,0,0), {tt_cxy_pair(0, 1, 2), tt_cxy_pair(0, 2, 2)});

//     log_assert(queue_buffer_ids.size() == 1);
//     router::insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(*(router.get()));;

//     ASSERT_EQ(router->get_pipes().size(), 2);
//     ASSERT_EQ(router->get_buffer_map().size(), 4);

//     auto is_relay_buffer = [](std::pair<unique_id_t, router_buffer_info_t> iter) -> bool {
//         unique_id_t buf_id = iter.first;
//         const router_buffer_info_t &buffer = iter.second;
//         if (buffer.is_queue()) {
//             return false;
//         }

//         return buffer.info().type() == tt::RouterBufferType::Relay;
//     };

//     auto relay_buffer_id_iter = std::find_if(router->get_buffer_map().begin(), router->get_buffer_map().end(), is_relay_buffer);
//     ASSERT_NE(relay_buffer_id_iter, router->get_buffer_map().end());

//     // assert no other relay buffers
//     const unique_id_t relay_buffer_id = relay_buffer_id_iter->first;
//     const auto relay_buffer = relay_buffer_id_iter->second;
//     ASSERT_TRUE(std::find_if(++relay_buffer_id_iter, router->get_buffer_map().end(), is_relay_buffer) == router->get_buffer_map().end());

//     // Validate DRAM -> relay buffer pipe
//     const auto &relay_input_pipe_id = router->get_buffer_input_pipes().at(relay_buffer_id);
//     const auto &dram_to_relay_pipe = router->get_pipes().at(relay_input_pipe_id);
//     ASSERT_TRUE(dram_to_relay_pipe.input_buffer_ids.size() == 1);
//     ASSERT_TRUE(dram_to_relay_pipe.input_buffer_ids.at(0) == queue_buffer_ids.at(0));
//     ASSERT_TRUE(dram_to_relay_pipe.output_buffer_ids().size() == 1);
//     ASSERT_TRUE(dram_to_relay_pipe.output_buffer_ids().at(0) == relay_buffer_id);

//     // Validate only one output pipe from relay buffer
//     const auto &relay_output_pipe_ids = router->get_buffer_output_pipes().at(relay_buffer_id);
//     ASSERT_EQ(relay_output_pipe_ids.size(), 1);

//     // Validate relay buffer -> scatter targets pipe
//     const auto &relay_to_input_pipe = router->get_pipes().at(*(relay_output_pipe_ids.begin()));
//     ASSERT_TRUE(relay_to_input_pipe.input_buffer_ids.size() == 1);
//     ASSERT_TRUE(relay_to_input_pipe.input_buffer_ids.at(0) == relay_buffer_id);
//     ASSERT_TRUE(relay_to_input_pipe.output_buffer_ids().size() == consumer_buffer_ids.size());
//     ASSERT_TRUE(relay_to_input_pipe.output_buffer_ids() == consumer_buffer_ids);
// }

// void print_pipes(Router &router) {
//     for (const auto &[pipe_id, pipe]: router.get_pipes()) {
//         std::string inputs_string = "{";
//         for (auto in : pipe.input_buffer_ids) {
//             inputs_string += std::to_string(in) + ",";
//         }
//         inputs_string += "}";  

//         std::string outputs_string = "{";
//         for (auto out : pipe.output_buffer_ids()) {
//             outputs_string += std::to_string(out) + ",";
//         }
//         outputs_string += "}";  

//         log_debug(tt::LogRouter, "{} -> {}", inputs_string, outputs_string);
//     }
// }

// void print_buffer_cores(Router &router) {
//     for (const auto &[buf_id, buffer]: router.get_buffer_map()) {
//         log_debug(tt::LogRouter, "{}: x={},y={}", buf_id, buffer.core_location().x, buffer.core_location().y);
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufs, TestInsertWhGatherMulticastRelayBuffer) {

//     // Pull out to `generate_gather_test_scenario` helper ////////////////
//     const auto &[queue_id, queue, buf_id_1, buf_1] = generate_dummy_router_queue(0, 1, *(wh_soc_desc.get()), 1);
//     const auto &[queue_id_2, queue_2, buf_id_2, buf_2] = generate_dummy_router_queue(1, 0, *(wh_soc_desc.get()), 1);
//     const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Input);
//     const auto &[buf_id_4, buf_4] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 2, 2), tt::RouterBufferType::Input);

//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {{queue_id, queue}, {queue_id_2, queue_2}};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1},
//         {buf_id_2, buf_2},
//         {buf_id_3, buf_3},
//         {buf_id_4, buf_4}
//     };

//     auto router = generate_test_router(*(wh_soc_desc.get()), 1, buffer_map, queue_map);
//     //////////////////////////////////////////////////////////////////////

//     const std::vector<unique_id_t> gather_sources = {buf_id_1, buf_id_2};
//     const std::vector<unique_id_t> multicast_targets = {buf_id_3, buf_id_4};
//     auto gather_multicast_pipe_id = router->create_gather_multicast_pipe_connection(gather_sources, multicast_targets, router->get_buffer_location(multicast_targets.at(0)));

//     router::insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(*(router.get()));;
    
//     print_buffer_cores(*(router.get()));
//     print_pipes(*(router.get()));

//     ASSERT_EQ(router->get_pipes().size(), 3);
//     ASSERT_EQ(router->get_buffer_map().size(), 6);

//     auto is_relay_buffer = [](std::pair<unique_id_t, router_buffer_info_t> iter) -> bool {
//         unique_id_t buf_id = iter.first;
//         const router_buffer_info_t &buffer = iter.second;
//         if (buffer.is_queue()) {
//             return false;
//         }

//         return buffer.info().type() == tt::RouterBufferType::Relay;
//     };

//     std::unordered_set<unique_id_t> relay_buffer_ids = {};
//     auto relay_buffer_id_iter = std::find_if(router->get_buffer_map().begin(), router->get_buffer_map().end(), is_relay_buffer);
//     ASSERT_NE(relay_buffer_id_iter, router->get_buffer_map().end());

//     // assert no other relay buffers
//     const unique_id_t first_relay_buffer_id = relay_buffer_id_iter->first;
//     relay_buffer_ids.insert(first_relay_buffer_id);
//     const pipe_t &first_relay_input_pipe = router->get_pipes().at(router->get_buffer_input_pipes().at(first_relay_buffer_id));
//     const auto first_relay_buffer = relay_buffer_id_iter->second;

//     relay_buffer_id_iter = std::find_if(++relay_buffer_id_iter, router->get_buffer_map().end(), is_relay_buffer);
//     ASSERT_NE(relay_buffer_id_iter, router->get_buffer_map().end());
//     const unique_id_t second_relay_buffer_id = relay_buffer_id_iter->first;
//     relay_buffer_ids.insert(second_relay_buffer_id);
//     const auto second_relay_buffer = relay_buffer_id_iter->second;

//     ASSERT_TRUE(std::find_if(++relay_buffer_id_iter, router->get_buffer_map().end(), is_relay_buffer) == router->get_buffer_map().end());

//     // FIRST QUEUE RELAY VALIDATION
//     // Validate DRAM -> relay buffer pipes
//     const auto &buf_1_output_pipes = router->get_buffer_output_pipes().at(buf_id_1);
//     ASSERT_EQ(buf_1_output_pipes.size(), 1);
//     unique_id_t queue_1_pipe_id = *(buf_1_output_pipes.begin());
//     const auto &queue_1_pipe = router->get_pipes().at(queue_1_pipe_id);
//     ASSERT_EQ(queue_1_pipe.output_buffer_ids().size(), 1);
//     ASSERT_TRUE(relay_buffer_ids.find(queue_1_pipe.output_buffer_ids().at(0)) != relay_buffer_ids.end());
    
//     const auto &buf_2_output_pipes = router->get_buffer_output_pipes().at(buf_id_2);
//     ASSERT_EQ(buf_2_output_pipes.size(), 1);
//     unique_id_t queue_2_pipe_id = *(buf_2_output_pipes.begin());
//     const auto &queue_2_pipe = router->get_pipes().at(queue_2_pipe_id);
//     ASSERT_EQ(queue_2_pipe.output_buffer_ids().size(), 1);
//     ASSERT_TRUE(relay_buffer_ids.find(queue_2_pipe.output_buffer_ids().at(0)) != relay_buffer_ids.end());


//     // Validate consumers are fed by same pipe and by only relay buffers
//     unique_id_t consumer_mcast_pipe_id = router->get_buffer_input_pipes().at(buf_id_3);
//     ASSERT_EQ(router->get_buffer_input_pipes().at(buf_id_3), consumer_mcast_pipe_id);
//     const auto &consumer_mcast_pipe = router->get_pipes().at(consumer_mcast_pipe_id);
//     ASSERT_EQ(consumer_mcast_pipe.output_buffer_ids().size(), 2);
//     for (auto consumer_mcast_pipe_input_id : consumer_mcast_pipe.input_buffer_ids) {
//         ASSERT_TRUE(relay_buffer_ids.find(consumer_mcast_pipe_input_id) != relay_buffer_ids.end());
//     }

//     if (consumer_mcast_pipe.input_buffer_ids.size() > 1) {
//         // Both relay bufs from the queues feed the pipe together and it's a gather multicast pipe
//         ASSERT_EQ(consumer_mcast_pipe.input_buffer_ids.size(), 2);
//     } else {
//         // the pipe was broken into separate gather and multicast pipes
//         unique_id_t gather_pipe_id = router->get_buffer_input_pipes().at(consumer_mcast_pipe.input_buffer_ids.at(0));
//         const auto &gather_pipe = router->get_pipes().at(gather_pipe_id);
//         ASSERT_EQ(gather_pipe.input_buffer_ids.size(), 2);
//         ASSERT_TRUE(gather_pipe.input_buffer_ids.at(0) == buf_id_1 || gather_pipe.input_buffer_ids.at(0) == queue_1_pipe.output_buffer_ids().at(0));
//         ASSERT_TRUE(gather_pipe.input_buffer_ids.at(1) == buf_id_2 || gather_pipe.input_buffer_ids.at(1) == queue_2_pipe.output_buffer_ids().at(0));
//     }
// }

// TEST(Net2PipeRouterWhA0RelayBufs, TestInsertWhGatherRelayBuffer) {

//     // Pull out to `generate_gather_test_scenario` helper ////////////////
//     const auto &[queue_id, queue, buf_id_1, buf_1] = generate_dummy_router_queue(0, 0, *(wh_soc_desc.get()), 1);
//     const auto &[queue_id_2, queue_2, buf_id_2, buf_2] = generate_dummy_router_queue(1, 0, *(wh_soc_desc.get()), 1);
//     const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(0, 1, 5), tt::RouterBufferType::Input);

//     std::unordered_map<std::uint64_t, tt_queue_info> queue_map = {{queue_id, queue}, {queue_id_2, queue_2}};
//     auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{
//         {buf_id_1, buf_1},
//         {buf_id_2, buf_2},
//         {buf_id_3, buf_3},
//     };

//     auto router = generate_test_router(*(wh_soc_desc.get()), 1, buffer_map, queue_map);
//     //////////////////////////////////////////////////////////////////////

//     const std::vector<unique_id_t> gather_sources = {buf_id_1, buf_id_2};
//     auto gather_pipe_id = router->create_gather_pipe_connection(gather_sources, buf_id_3, router->get_buffer_location(buf_id_3));

//     router::insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(*(router.get()));;

//     ASSERT_TRUE(router->get_pipes().size() == 2);
//     ASSERT_TRUE(router->get_buffer_map().size() == 4);

//     auto is_relay_buffer = [](std::pair<unique_id_t, router_buffer_info_t> iter) -> bool {
//         unique_id_t buf_id = iter.first;
//         const router_buffer_info_t &buffer = iter.second;
//         if (buffer.is_queue()) {
//             return false;
//         }

//         return buffer.info().type() == tt::RouterBufferType::Relay;
//     };

//     auto relay_buffer_id_iter = std::find_if(router->get_buffer_map().begin(), router->get_buffer_map().end(), is_relay_buffer);
//     ASSERT_NE(relay_buffer_id_iter, router->get_buffer_map().end());

//     // assert no other relay buffers
//     const unique_id_t relay_buffer_id = relay_buffer_id_iter->first;
//     const auto relay_buffer = relay_buffer_id_iter->second;
//     ASSERT_TRUE(std::find_if(++relay_buffer_id_iter, router->get_buffer_map().end(), is_relay_buffer) == router->get_buffer_map().end());

//     // Validate DRAM -> relay buffer pipe
//     const auto &relay_input_pipe_id = router->get_buffer_input_pipes().at(relay_buffer_id);
//     const auto &dram_to_relay_pipe = router->get_pipes().at(relay_input_pipe_id);
//     ASSERT_EQ(dram_to_relay_pipe.input_buffer_ids.size(), 1);
//     ASSERT_EQ(dram_to_relay_pipe.input_buffer_ids.at(0), buf_id_1);
//     ASSERT_EQ(dram_to_relay_pipe.output_buffer_ids().size(), 1);
//     ASSERT_EQ(dram_to_relay_pipe.output_buffer_ids().at(0), relay_buffer_id);

//     // Validate only one output pipe from relay buffer
//     const auto &relay_output_pipe_ids = router->get_buffer_output_pipes().at(relay_buffer_id);
//     ASSERT_EQ(relay_output_pipe_ids.size(), 1);

//     // Validate relay buffer -> scatter targets pipe
//     const auto &relay_to_input_pipe = router->get_pipes().at(*(relay_output_pipe_ids.begin()));
//     ASSERT_EQ(relay_to_input_pipe.input_buffer_ids.size(), 2);
//     ASSERT_EQ(relay_to_input_pipe.input_buffer_ids.at(0), relay_buffer_id);
//     ASSERT_EQ(relay_to_input_pipe.input_buffer_ids.at(1), gather_sources.at(1));
//     ASSERT_EQ(relay_to_input_pipe.output_buffer_ids().size(), 1);
//     ASSERT_EQ(relay_to_input_pipe.output_buffer_ids().at(0), buf_id_3);
// }

