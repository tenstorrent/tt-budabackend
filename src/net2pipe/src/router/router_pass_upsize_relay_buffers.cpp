// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/tt_soc_descriptor.h"
#include "netlist_utils.hpp"
#include "utils/logger.hpp"
#include "router/router_passes_common.h"
#include "router.hpp"
#include "common/size_lib.hpp"
#include "router_types.h"
#include "tile_maps.h"
#include "net2pipe.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <tuple>

constexpr static std::array<int,1001> first_1001_primes = {
1,      2,      3,      5,      7,     11,     13,     17,     19,     23,     29, 
     31,     37,     41,     43,     47,     53,     59,     61,     67,     71, 
     73,     79,     83,     89,     97,    101,    103,    107,    109,    113, 
    127,    131,    137,    139,    149,    151,    157,    163,    167,    173, 
    179,    181,    191,    193,    197,    199,    211,    223,    227,    229, 
    233,    239,    241,    251,    257,    263,    269,    271,    277,    281, 
    283,    293,    307,    311,    313,    317,    331,    337,    347,    349, 
    353,    359,    367,    373,    379,    383,    389,    397,    401,    409, 
    419,    421,    431,    433,    439,    443,    449,    457,    461,    463, 
    467,    479,    487,    491,    499,    503,    509,    521,    523,    541, 
    547,    557,    563,    569,    571,    577,    587,    593,    599,    601, 
    607,    613,    617,    619,    631,    641,    643,    647,    653,    659, 
    661,    673,    677,    683,    691,    701,    709,    719,    727,    733, 
    739,    743,    751,    757,    761,    769,    773,    787,    797,    809, 
    811,    821,    823,    827,    829,    839,    853,    857,    859,    863, 
    877,    881,    883,    887,    907,    911,    919,    929,    937,    941, 
    947,    953,    967,    971,    977,    983,    991,    997,   1009,   1013, 
   1019,   1021,   1031,   1033,   1039,   1049,   1051,   1061,   1063,   1069, 
   1087,   1091,   1093,   1097,   1103,   1109,   1117,   1123,   1129,   1151, 
   1153,   1163,   1171,   1181,   1187,   1193,   1201,   1213,   1217,   1223, 
   1229,   1231,   1237,   1249,   1259,   1277,   1279,   1283,   1289,   1291, 
   1297,   1301,   1303,   1307,   1319,   1321,   1327,   1361,   1367,   1373, 
   1381,   1399,   1409,   1423,   1427,   1429,   1433,   1439,   1447,   1451, 
   1453,   1459,   1471,   1481,   1483,   1487,   1489,   1493,   1499,   1511, 
   1523,   1531,   1543,   1549,   1553,   1559,   1567,   1571,   1579,   1583, 
   1597,   1601,   1607,   1609,   1613,   1619,   1621,   1627,   1637,   1657, 
   1663,   1667,   1669,   1693,   1697,   1699,   1709,   1721,   1723,   1733, 
   1741,   1747,   1753,   1759,   1777,   1783,   1787,   1789,   1801,   1811, 
   1823,   1831,   1847,   1861,   1867,   1871,   1873,   1877,   1879,   1889, 
   1901,   1907,   1913,   1931,   1933,   1949,   1951,   1973,   1979,   1987, 
   1993,   1997,   1999,   2003,   2011,   2017,   2027,   2029,   2039,   2053, 
   2063,   2069,   2081,   2083,   2087,   2089,   2099,   2111,   2113,   2129, 
   2131,   2137,   2141,   2143,   2153,   2161,   2179,   2203,   2207,   2213, 
   2221,   2237,   2239,   2243,   2251,   2267,   2269,   2273,   2281,   2287, 
   2293,   2297,   2309,   2311,   2333,   2339,   2341,   2347,   2351,   2357, 
   2371,   2377,   2381,   2383,   2389,   2393,   2399,   2411,   2417,   2423, 
   2437,   2441,   2447,   2459,   2467,   2473,   2477,   2503,   2521,   2531, 
   2539,   2543,   2549,   2551,   2557,   2579,   2591,   2593,   2609,   2617, 
   2621,   2633,   2647,   2657,   2659,   2663,   2671,   2677,   2683,   2687, 
   2689,   2693,   2699,   2707,   2711,   2713,   2719,   2729,   2731,   2741, 
   2749,   2753,   2767,   2777,   2789,   2791,   2797,   2801,   2803,   2819, 
   2833,   2837,   2843,   2851,   2857,   2861,   2879,   2887,   2897,   2903, 
   2909,   2917,   2927,   2939,   2953,   2957,   2963,   2969,   2971,   2999, 
   3001,   3011,   3019,   3023,   3037,   3041,   3049,   3061,   3067,   3079, 
   3083,   3089,   3109,   3119,   3121,   3137,   3163,   3167,   3169,   3181, 
   3187,   3191,   3203,   3209,   3217,   3221,   3229,   3251,   3253,   3257, 
   3259,   3271,   3299,   3301,   3307,   3313,   3319,   3323,   3329,   3331, 
   3343,   3347,   3359,   3361,   3371,   3373,   3389,   3391,   3407,   3413, 
   3433,   3449,   3457,   3461,   3463,   3467,   3469,   3491,   3499,   3511, 
   3517,   3527,   3529,   3533,   3539,   3541,   3547,   3557,   3559,   3571, 
   3581,   3583,   3593,   3607,   3613,   3617,   3623,   3631,   3637,   3643, 
   3659,   3671,   3673,   3677,   3691,   3697,   3701,   3709,   3719,   3727, 
   3733,   3739,   3761,   3767,   3769,   3779,   3793,   3797,   3803,   3821, 
   3823,   3833,   3847,   3851,   3853,   3863,   3877,   3881,   3889,   3907, 
   3911,   3917,   3919,   3923,   3929,   3931,   3943,   3947,   3967,   3989, 
   4001,   4003,   4007,   4013,   4019,   4021,   4027,   4049,   4051,   4057, 
   4073,   4079,   4091,   4093,   4099,   4111,   4127,   4129,   4133,   4139, 
   4153,   4157,   4159,   4177,   4201,   4211,   4217,   4219,   4229,   4231, 
   4241,   4243,   4253,   4259,   4261,   4271,   4273,   4283,   4289,   4297, 
   4327,   4337,   4339,   4349,   4357,   4363,   4373,   4391,   4397,   4409, 
   4421,   4423,   4441,   4447,   4451,   4457,   4463,   4481,   4483,   4493, 
   4507,   4513,   4517,   4519,   4523,   4547,   4549,   4561,   4567,   4583, 
   4591,   4597,   4603,   4621,   4637,   4639,   4643,   4649,   4651,   4657, 
   4663,   4673,   4679,   4691,   4703,   4721,   4723,   4729,   4733,   4751, 
   4759,   4783,   4787,   4789,   4793,   4799,   4801,   4813,   4817,   4831, 
   4861,   4871,   4877,   4889,   4903,   4909,   4919,   4931,   4933,   4937, 
   4943,   4951,   4957,   4967,   4969,   4973,   4987,   4993,   4999,   5003, 
   5009,   5011,   5021,   5023,   5039,   5051,   5059,   5077,   5081,   5087, 
   5099,   5101,   5107,   5113,   5119,   5147,   5153,   5167,   5171,   5179, 
   5189,   5197,   5209,   5227,   5231,   5233,   5237,   5261,   5273,   5279, 
   5281,   5297,   5303,   5309,   5323,   5333,   5347,   5351,   5381,   5387, 
   5393,   5399,   5407,   5413,   5417,   5419,   5431,   5437,   5441,   5443, 
   5449,   5471,   5477,   5479,   5483,   5501,   5503,   5507,   5519,   5521, 
   5527,   5531,   5557,   5563,   5569,   5573,   5581,   5591,   5623,   5639, 
   5641,   5647,   5651,   5653,   5657,   5659,   5669,   5683,   5689,   5693, 
   5701,   5711,   5717,   5737,   5741,   5743,   5749,   5779,   5783,   5791, 
   5801,   5807,   5813,   5821,   5827,   5839,   5843,   5849,   5851,   5857, 
   5861,   5867,   5869,   5879,   5881,   5897,   5903,   5923,   5927,   5939, 
   5953,   5981,   5987,   6007,   6011,   6029,   6037,   6043,   6047,   6053, 
   6067,   6073,   6079,   6089,   6091,   6101,   6113,   6121,   6131,   6133, 
   6143,   6151,   6163,   6173,   6197,   6199,   6203,   6211,   6217,   6221, 
   6229,   6247,   6257,   6263,   6269,   6271,   6277,   6287,   6299,   6301, 
   6311,   6317,   6323,   6329,   6337,   6343,   6353,   6359,   6361,   6367, 
   6373,   6379,   6389,   6397,   6421,   6427,   6449,   6451,   6469,   6473, 
   6481,   6491,   6521,   6529,   6547,   6551,   6553,   6563,   6569,   6571, 
   6577,   6581,   6599,   6607,   6619,   6637,   6653,   6659,   6661,   6673, 
   6679,   6689,   6691,   6701,   6703,   6709,   6719,   6733,   6737,   6761, 
   6763,   6779,   6781,   6791,   6793,   6803,   6823,   6827,   6829,   6833, 
   6841,   6857,   6863,   6869,   6871,   6883,   6899,   6907,   6911,   6917, 
   6947,   6949,   6959,   6961,   6967,   6971,   6977,   6983,   6991,   6997, 
   7001,   7013,   7019,   7027,   7039,   7043,   7057,   7069,   7079,   7103, 
   7109,   7121,   7127,   7129,   7151,   7159,   7177,   7187,   7193,   7207,
   7211,   7213,   7219,   7229,   7237,   7243,   7247,   7253,   7283,   7297, 
   7307,   7309,   7321,   7331,   7333,   7349,   7351,   7369,   7393,   7411, 
   7417,   7433,   7451,   7457,   7459,   7477,   7481,   7487,   7489,   7499, 
   7507,   7517,   7523,   7529,   7537,   7541,   7547,   7549,   7559,   7561, 
   7573,   7577,   7583,   7589,   7591,   7603,   7607,   7621,   7639,   7643, 
   7649,   7669,   7673,   7681,   7687,   7691,   7699,   7703,   7717,   7723, 
   7727,   7741,   7753,   7757,   7759,   7789,   7793,   7817,   7823,   7829, 
   7841,   7853,   7867,   7873,   7877,   7879,   7883,   7901,   7907,   7919
};

namespace router {

static constexpr bool upsize_eth_buffers_only = true;

std::vector<unique_id_t> collect_buffers_of_type(const Router &router, RouterBufferType type) {

    auto ids = std::vector<unique_id_t>{};
    for (const auto &[id, buf_info] : router.get_buffer_map()) {
        bool is_relay_buffer = !buf_info.is_queue() && buf_info.info().type() == type;
        bool upsize_buffer = is_relay_buffer;
        if (upsize_buffer and upsize_eth_buffers_only) {
            const auto &buf_location = router.get_buffer_location(id);
            
            const auto &soc_desc = router.get_soc_descriptor(buf_location.chip);
            upsize_buffer = upsize_buffer && soc_desc.is_ethernet_core(buf_location);
        }
        if (upsize_buffer) {
            ids.push_back(id);
        }
    }

    return ids;
}


std::unordered_map<unique_id_t, int> build_relay_buffer_consumer_tile_clear_granularities(const Router &router, const netlist_parser &parsed_netlist) {
    auto buf_tile_clear_granularities = std::unordered_map<unique_id_t, int>();

    auto traversal_queue = std::deque<unique_id_t>{};
    for (const auto &[op_name, grid_operand_buffers] : router.get_op_input_buf_map()) {
        const auto &op_info = router.get_op_info_map().at(op_name);
        for (const auto &[row_index, row_operand_buffers] : grid_operand_buffers) {
            for (const auto &[col_index, operand_buffers] : row_operand_buffers) {
                for (const auto &[operand_index, buffer_id] : operand_buffers) {
                    log_trace(tt::LogRouter, "op input buffer: {}", buffer_id);
                    int tile_clear_granularity = router.get_op_kernel_input_tile_clear_granularity(op_name, operand_index);
                    TT_ASSERT(tile_clear_granularity > 0);
                    buf_tile_clear_granularities.insert({buffer_id, tile_clear_granularity});
                    traversal_queue.push_back(buffer_id);

                    if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
                        unique_id_t eth_datacopy_buffer_id = buffer_id;
                        while (router.get_buffer_output_pipes().find(eth_datacopy_buffer_id) !=
                               router.get_buffer_output_pipes().end()) {
                            const auto &output_pipes = router.get_buffer_output_pipes().at(eth_datacopy_buffer_id);
                            TT_ASSERT(output_pipes.size() == 1);
                            const pipe_t &output_pipe = router.get_pipe(*(output_pipes.begin()));
                            eth_datacopy_buffer_id = output_pipe.output_buffer_ids().at(0);
                            buf_tile_clear_granularities.insert({eth_datacopy_buffer_id, tile_clear_granularity});
                        }
                    }
                }
            }
        }
    }
    const auto &buffer_output_pipe_map = router.get_buffer_output_pipes();
    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (buffer.is_queue()) {
            bool is_output_queue = buffer_output_pipe_map.find(id) == buffer_output_pipe_map.end();
            if (is_output_queue) {
                traversal_queue.push_back(id);
            }
        }

    }

    auto get_all_consumer_buffers_of_buffer = [](const Router &router, unique_id_t buffer_id) {
        auto consumers = std::vector<unique_id_t>{};
        const auto &buffer_output_pipes_map = router.get_buffer_output_pipes();
        if (buffer_output_pipes_map.find(buffer_id) != buffer_output_pipes_map.end()) {
            const auto &output_pipes = router.get_buffer_output_pipes().at(buffer_id);
            for (auto pipe_id : output_pipes) {
                const auto &pipe = router.get_pipe(pipe_id);
                if (pipe.has_multiple_timesteps()) {
                    for (const auto &output_buffer_ids : pipe.time_multiplexed_output_buffer_ids()) {
                        std::copy(output_buffer_ids.begin(), output_buffer_ids.end(), std::back_inserter(consumers));
                    }
                } else {
                    std::copy(pipe.output_buffer_ids().begin(), pipe.output_buffer_ids().end(), std::back_inserter(consumers));
                }
            }
        }
        return consumers;
    };

    auto compute_tile_clear_granularity = [&buf_tile_clear_granularities](const std::vector<unique_id_t> &buffer_consumers) -> int {
        if (buffer_consumers.size() == 0) {
            return 1;
        }
        auto tcg = buf_tile_clear_granularities.at(buffer_consumers.at(0));
        for (std::size_t i = 1; i < buffer_consumers.size(); i++) {
            tcg = std::lcm(tcg, buf_tile_clear_granularities.at(buffer_consumers[i]));
        }
        return tcg;
    };

    const auto &buffer_input_pipes = router.get_buffer_input_pipes();
    while (traversal_queue.size() > 0) {
        unique_id_t buffer_id = traversal_queue.front();
        traversal_queue.pop_front();

        const auto &buffer_consumers = get_all_consumer_buffers_of_buffer(router, buffer_id);
        bool all_output_buffers_computed = std::all_of(buffer_consumers.begin(), buffer_consumers.end(), [&buf_tile_clear_granularities](auto id) { return buf_tile_clear_granularities.find(id) != buf_tile_clear_granularities.end(); });
        bool already_computed = buf_tile_clear_granularities.find(buffer_id) != buf_tile_clear_granularities.end();
        if (!already_computed && all_output_buffers_computed ) {
            auto tile_clear_granularity = compute_tile_clear_granularity(buffer_consumers);
            TT_ASSERT(tile_clear_granularity > 0);
            log_trace(tt::LogRouter, "Setting tile clear granularity for {} as {}", buffer_id, tile_clear_granularity);
            buf_tile_clear_granularities.insert({buffer_id, tile_clear_granularity});
        }
        if (buffer_input_pipes.find(buffer_id) != buffer_input_pipes.end()) {
            unique_id_t input_pipe_id = buffer_input_pipes.at(buffer_id);
            const auto &input_pipe = router.get_pipe(input_pipe_id);
            std::copy(input_pipe.input_buffer_ids.begin(), input_pipe.input_buffer_ids.end(), std::back_inserter(traversal_queue));
        }
    }

    return buf_tile_clear_granularities;
}

// Returns true if upsizing was successful
bool upsize_relay_buffer_if_enough_space(unique_id_t id, int target_size_in_tiles, Router &router, std::unordered_map<unique_id_t, std::pair<int,int>> &buffer_size_changes) {
    const auto &buffer = router.get_buffer(id);

    const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(buffer.core_location());
    const auto old_size_tiles = buffer.info().allocated_size_in_tiles();
    auto tile_size_in_bytes = buffer.info().allocated_size_in_bytes() / buffer.info().allocated_size_in_tiles();

    auto new_tile_count_delta = target_size_in_tiles - old_size_tiles;

    int available_upsize_space_in_bytes = core_attrs.available_l1_bytes();
    auto available_tile_capacity_in_l1 = available_upsize_space_in_bytes / tile_size_in_bytes;

    log_debug(tt::LogRouter, "Available L1 space {}B {} tiles. Target size in tiles: {}", core_attrs.available_l1_bytes(), available_tile_capacity_in_l1, target_size_in_tiles);

    if (available_tile_capacity_in_l1 >= new_tile_count_delta && target_size_in_tiles > old_size_tiles) {
        TT_ASSERT(core_attrs.can_allocate_bytes(available_tile_capacity_in_l1 * tile_size_in_bytes));
        int upsize_amount_in_tiles =  target_size_in_tiles - old_size_tiles;
        TT_ASSERT(upsize_amount_in_tiles >= 0);

        log_debug(tt::LogRouter, "Upsized buffer {} by {} tiles from {} to {}", id, upsize_amount_in_tiles, old_size_tiles, target_size_in_tiles);
        auto new_buffer_size = buffer.info().allocated_size_in_bytes() + upsize_amount_in_tiles * tile_size_in_bytes;
        router.change_buffer_size(id, new_buffer_size);

        if (buffer_size_changes.find(id) == buffer_size_changes.end()) {
            buffer_size_changes[id] = {upsize_amount_in_tiles, upsize_amount_in_tiles * tile_size_in_bytes};
        } else {
            const auto &[old_tiles_added, old_bytes_added] = buffer_size_changes[id];
            buffer_size_changes[id] = {upsize_amount_in_tiles + old_tiles_added, (upsize_amount_in_tiles * tile_size_in_bytes) + old_bytes_added};
        }
        return true;
    } else {
        return false;
    }
}


std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> build_core_to_relay_buffer_map(Router &router, const std::vector<unique_id_t> &relay_buffer_ids) {
    auto core_relay_buffers = std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>();

    for (auto buf_id : relay_buffer_ids) {
        const auto &core = router.get_buffer_location(buf_id);
        auto producer_pipe_id = router.get_buffer_input_pipes().at(buf_id);
        const auto &producer_pipe = router.get_pipe(producer_pipe_id);

        bool is_relay_for_scatter_bufs = std::find_if(
            producer_pipe.input_buffer_ids.begin(), producer_pipe.input_buffer_ids.end(),
            [&router] (auto id) { return router.is_buffer_scatter(id); }) != producer_pipe.input_buffer_ids.end();
        bool is_dram_read_relay = std::find_if(
            producer_pipe.input_buffer_ids.begin(), producer_pipe.input_buffer_ids.end(),
            [&router](auto id) { return router.get_buffer(id).is_queue(); }
        ) != producer_pipe.input_buffer_ids.end();

        bool can_upsize = true;
        if (can_upsize) {
            core_relay_buffers[core].push_back(buf_id);
        }
    }

    return core_relay_buffers;
}


std::unordered_map<unique_id_t, std::vector<int>> compute_valid_buffer_sizes(
    Router const& router,
    std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> const& core_relay_buffer_ids) {

    // Organize the buffers by scatter_gather_num_tiles because lots of them will end up sharing the same
    // size so we don't want to recompute the valid sizes for each one because that may end up being slow
    // (O(n^2) to do that)
    std::unordered_map<int, std::unordered_set<unique_id_t>> scatter_gather_size_buffers = {};
    for (auto const& [core, buffers] : core_relay_buffer_ids) {
        for (unique_id_t buf_id : buffers) {
            const auto &buf = router.get_buffer(buf_id);
            int buf_scatter_gather_num_tiles = buf.info().scatter_gather_num_tiles();
            scatter_gather_size_buffers[buf_scatter_gather_num_tiles].insert(buf_id);
        }
    }

    // Compute all valid multiples of the scatter_gather_num_tiles for each buffer_group
    std::unordered_map<unique_id_t, std::vector<int>> per_buffer_valid_sizes = {};
    for (auto const& [scatter_gather_num_tiles, buffer_ids] : scatter_gather_size_buffers) {

        std::vector<int> multiples = {};
        multiples.reserve(static_cast<int>(sqrt(scatter_gather_num_tiles)));
        // First we compute the valid sizes <= scatter_gather_num_tiles then we compute all the valid sizes
        // above that amount. 
        {
            int divisor = scatter_gather_num_tiles - 1;
            if (divisor != 0) {
                int multiple = scatter_gather_num_tiles / divisor;
                
                while (divisor > 0) {
                    multiple = scatter_gather_num_tiles / divisor;
                    if (scatter_gather_num_tiles % divisor == 0) {
                        multiples.push_back(multiple);
                    }
                    divisor--;
                }
            }
        }
        
        for (unique_id_t id : buffer_ids) {
            per_buffer_valid_sizes[id] = {};
            per_buffer_valid_sizes[id].reserve(multiples.size());
            std::copy(multiples.begin(),multiples.end(), std::back_inserter(per_buffer_valid_sizes[id]));
            auto const& relay_buffer = router.get_buffer(id);
            int total_epoch_tiles = relay_buffer.info().total_epoch_tiles();
            int incoming_bw_bytes_per_cycle = 13; // round up (assuming 1GHz clock, and 100Gbps link, we have 12.5 B/cycle)
            int min_size_bytes_full_latency_hiding = KERNEL_INPUT_MIN_LATENCY_CYCLES *  incoming_bw_bytes_per_cycle;
            int latency_covering_num_tiles = min_size_bytes_full_latency_hiding / relay_buffer.info().tile_size_in_bytes();

            int max_buffer_size_tiles = std::min(latency_covering_num_tiles, total_epoch_tiles);
            {
                int multiple = 2;
                int size = scatter_gather_num_tiles * multiple;
                while (size <= max_buffer_size_tiles) {
                    if (max_buffer_size_tiles % size == 0) {
                        per_buffer_valid_sizes.at(id).push_back(size);
                    }
                    multiple++;
                    size = scatter_gather_num_tiles * multiple;
                }
            }
            log_debug(tt::LogRouter, "{}",multiples);
            TT_ASSERT((per_buffer_valid_sizes.at(id).size() == 0 && max_buffer_size_tiles == 1) || per_buffer_valid_sizes.at(id).front() <= per_buffer_valid_sizes.at(id).back());
        }
    }

    log_debug(tt::LogRouter, "Valid buffer size targets:");
    for (auto const& [buffer_id, valid_sizes] : per_buffer_valid_sizes) {
        log_debug(tt::LogRouter, "Buffer {}: {}", buffer_id, valid_sizes);
    }
    return per_buffer_valid_sizes;
}

std::pair<std::vector<int>::const_iterator, std::vector<int>::const_iterator> get_divisor_iterators_in_range(std::vector<int> const& divisors, int range_start_inclusive, int range_end_inclusive) {
    for (std::vector<int>::const_iterator i = divisors.begin(); i != divisors.end(); i++) {
        if (*i >= range_start_inclusive) {
            auto j = i;
            while (j != divisors.end() && *j <= range_end_inclusive) {
                j++;
            }
            return {i, j};
        }
    }
    return {divisors.end(), divisors.end()};
}


std::optional<int> compute_next_divisor(const std::vector<int> &sorted_divisors, int current) {
    // Cache this if needed
    TT_ASSERT(sorted_divisors.front() <= sorted_divisors.back());
    for (auto d : sorted_divisors) {
        if (d > current) {
            return d;
        }
    }
    return std::nullopt;
}

void upsize_sorted_buffers_by_attribute_on_core(
    Router &router, 
    const std::vector<unique_id_t> &relay_buffer_ids, 
    std::unordered_map<unique_id_t, std::pair<int,int>> &buffer_size_change_in_tiles_and_bytes, 
    // const std::unordered_map<int, std::vector<int>> &integer_divisors,
    const std::unordered_map<unique_id_t, std::vector<int>> &valid_buffer_sizes,
    std::function<int(const Router&,unique_id_t)> attribute_getter
) {
    // auto size_getter = std::bind(attribute_getter, router, std::placeholders::_1);
    auto num_buffers = relay_buffer_ids.size();
    if (num_buffers == 1) {
        bool upsize_successful = false;
        auto id = relay_buffer_ids.at(0);
        auto size = attribute_getter(router, id);
        const auto &valid_sizes = valid_buffer_sizes.at(relay_buffer_ids.at(0));
        log_debug(tt::LogRouter, "\tSingle buffer scenario for id: {} which has scatter_gather granularity of {}", id, size);
        for (int i = valid_sizes.size() - 1; !upsize_successful && i >= 0; i--) {
            upsize_successful = upsize_relay_buffer_if_enough_space(id, valid_sizes[i], router, buffer_size_change_in_tiles_and_bytes);
            if (upsize_successful) {
                log_debug(tt::LogRouter, "\tSuccesfully upsized to {} tiles", valid_sizes[i]);
            }
        }
    } else {

        std::vector<int> buffer_sizes = {};
        std::transform(relay_buffer_ids.begin(), relay_buffer_ids.end(), std::back_inserter(buffer_sizes), [&attribute_getter,&router](unique_id_t id) { return std::max<int>(4, attribute_getter(router,id)); });
        if (env_var("PROPORTIONAL_RELAY_BUFFER_UPSIZING", 0)) {
            auto size_total = std::reduce(buffer_sizes.begin(), buffer_sizes.end(), 0, std::plus<>());

            std::vector<float> normalized_size_targets_in_bytes = {};
            const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(router.get_buffer_location(relay_buffer_ids.at(0)));
            int available_upsize_space_in_bytes = core_attrs.available_l1_bytes();
            auto normalize = [size_total, available_upsize_space_in_bytes](int size) { return (static_cast<float>(size) / static_cast<float>(size_total)) * available_upsize_space_in_bytes; };
            std::transform(buffer_sizes.begin(), buffer_sizes.end(), std::back_inserter(normalized_size_targets_in_bytes), normalize);

            log_debug(tt::LogRouter, "buffer_sizes: {}", buffer_sizes);
            log_debug(tt::LogRouter, "normalized_size_targets_in_bytes: {}", normalized_size_targets_in_bytes);

            bool did_something = true;
            while (did_something) {
                did_something = false;

                for (int i = 0; i < num_buffers; i++) {
                    const auto &buffer = router.get_buffer(relay_buffer_ids[i]).info();
                    float current_utilization = static_cast<float>(buffer.allocated_size_in_tiles() / static_cast<float>(attribute_getter(router,relay_buffer_ids[i])));
                    if (normalized_size_targets_in_bytes[i] > current_utilization) {

                        auto const [devisors_begin, devisors_end] = get_divisor_iterators_in_range(valid_buffer_sizes.at(relay_buffer_ids[i]), buffer.allocated_size_in_tiles(), (normalized_size_targets_in_bytes[i] / buffer.tile_size_in_bytes()) + 1);
                        for (auto it = devisors_end; it != devisors_begin; it--) {
                            bool successfully_upsized = upsize_relay_buffer_if_enough_space(relay_buffer_ids[i], *it, router, buffer_size_change_in_tiles_and_bytes);
                            if (successfully_upsized) {
                                did_something = true;
                                break;
                            }
                        }
                    }
                }
            }
        } else {
            bool did_something = true;
            int largest_size_in_tiles = 1;
            std::vector<int> divisor_index = std::vector(num_buffers, 1);
            while (did_something) {
                did_something = false;
                int new_largest_size = std::numeric_limits<int>::max();
                for (int i = 0; i < num_buffers; i++) {
                    const auto &buffer = router.get_buffer(relay_buffer_ids[i]).info();

                    auto const& valid_sizes = valid_buffer_sizes.at(relay_buffer_ids[i]);
                    if (valid_sizes.size() > divisor_index[i] && largest_size_in_tiles >= buffer.allocated_size_in_tiles()) {
                        int target_size = valid_sizes.at(divisor_index[i]);
                        bool successfully_upsized = upsize_relay_buffer_if_enough_space(relay_buffer_ids[i], target_size, router, buffer_size_change_in_tiles_and_bytes);
                        if (successfully_upsized) {
                            did_something = true;
                            divisor_index[i]++;

                            bool has_next_size_up = valid_sizes.size() > divisor_index[i];
                            if (has_next_size_up) {
                                new_largest_size = std::min(new_largest_size, valid_sizes.at(divisor_index[i]));
                            }
                        }
                    }
                }
                largest_size_in_tiles = new_largest_size;
            }

        }

    }
}


void upsize_buffers_by_attribute(
    Router &router, 
    std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> &core_relay_buffer_ids, 
    std::unordered_map<unique_id_t, std::pair<int,int>> &buffer_size_change_in_tiles_and_bytes,
    std::function<int(const Router&,unique_id_t)> attribute_getter
) {

    auto compute_utilization = [&router, &attribute_getter](auto id) -> float { return static_cast<float>(router.get_buffer(id).info().allocated_size_in_tiles()) / static_cast<float>(attribute_getter(router, id)); };
    auto buffer_id_size_attribute_compare = [&router,&compute_utilization](auto id0, auto id1) { return compute_utilization(id0) < compute_utilization(id1); };


    auto gather_scatter_sizes = std::unordered_set<int>();
    for (auto &[core, relay_buffer_ids] : core_relay_buffer_ids) {
        std::sort(relay_buffer_ids.begin(), relay_buffer_ids.end(), buffer_id_size_attribute_compare);
        TT_ASSERT(compute_utilization(relay_buffer_ids.front()) <= compute_utilization(relay_buffer_ids.back()), "Sorted in ascending order - reverse compare direction");
        for (auto id : relay_buffer_ids) {
            auto size = attribute_getter(router, id);
            TT_ASSERT(size > 0);
            gather_scatter_sizes.insert(size);
        }
    }

    auto valid_buffer_sizes = compute_valid_buffer_sizes(router, core_relay_buffer_ids);
    
    for (auto &[core, relay_buffer_ids] : core_relay_buffer_ids) {
        log_debug(tt::LogRouter, "Upsizing buffers on core(c={},y={},x={})", core.chip, core.y, core.x);
        upsize_sorted_buffers_by_attribute_on_core(router, relay_buffer_ids, buffer_size_change_in_tiles_and_bytes, valid_buffer_sizes, attribute_getter);
    }

}

void dump_buffer_upsize_report(Router &router, const std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> &core_relay_buffers, const std::unordered_map<unique_id_t, std::pair<int,int>> &buffer_size_change_in_tiles_and_bytes) {
    

    log_debug(tt::LogRouter, "");
    log_debug(tt::LogRouter, "====================================================================");
    log_debug(tt::LogRouter, "Upsize Buffers Pass Report: ");
    log_debug(tt::LogRouter, "{:16} {:16} -> ({:12}, {:12})", "buffer_id", "core_loc", "#added_tiles", "added_bytes");
    log_debug(tt::LogRouter, "--------------------------------------------------------------------");
    for (const auto &[core, relay_buffer_ids] : core_relay_buffers) {
        for (auto id : relay_buffer_ids) {
            std::stringstream core_str;
            core_str << "(c=" << core.chip << ",y=" << core.y << ",x=" << core.x << ")";
            const auto &[added_tiles, added_bytes] = buffer_size_change_in_tiles_and_bytes.find(id) != buffer_size_change_in_tiles_and_bytes.end() ? buffer_size_change_in_tiles_and_bytes.at(id) : std::pair<int,int>{0, 0};
            log_debug(tt::LogRouter, "{:16} {:16} -> (+{:11}, +{:10}B)", id, core_str.str(), added_tiles, added_bytes);
        }
    }
    log_debug(tt::LogRouter, "--------------------------------------------------------------------");


    const auto bfp8_tile_size = tt::size::get_tile_size_in_bytes(DataFormat::Bfp8, true);
    const auto fp16_tile_size = tt::size::get_tile_size_in_bytes(DataFormat::Float16, true);
    const auto fp32_tile_size = tt::size::get_tile_size_in_bytes(DataFormat::Float32, true);
    log_debug(tt::LogRouter, "Available L1 Capacity relay buffer cores:");
    log_debug(tt::LogRouter, "{:16} -> ({:12}, {:12}, {:12}, {:12})", "core", "#Bytes", "#bfp8 tiles", "#fp16 tiles", "#fp32 tiles");
    log_debug(tt::LogRouter, "--------------------------------------------------------------------");
    for (const auto &[core, relay_buffer_ids] : core_relay_buffers) {
        const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(core);
        const auto available_bytes = core_attrs.available_l1_bytes();
        const auto available_bfp8_tiles = available_bytes / bfp8_tile_size;
        const auto available_fp16_tiles = available_bytes / fp16_tile_size;
        const auto available_fp32_tiles = available_bytes / fp32_tile_size;
        std::stringstream core_str;
        core_str << "(c=" << core.chip << ",y=" << core.y << ",x=" << core.x << ")";
        log_debug(tt::LogRouter, "{:16} -> ({:11}B, {:12}, {:12}, {:12})", core_str.str(), available_bytes, available_bfp8_tiles, available_fp16_tiles, available_fp32_tiles);
    }

}


void upsize_relay_buffers_pass(Router &router, const netlist_parser &parsed_netlist) {

    const auto &relay_buffer_ids = collect_buffers_of_type(router, RouterBufferType::Relay);
    if (relay_buffer_ids.size() > 0) {
        auto buffer_size_change_in_tiles_and_bytes = std::unordered_map<unique_id_t, std::pair<int,int>>();
    
        // upsize buffers in two passes. Each time, trying to roughly equally upsize buffers so that their allocation sizes are achieve a similar utilization
        // 1st by balancing utilization of allocation size as a percentage of the buffer's scatter-gather num tiles,
        // 2nd next by balancing utilizationn of allocation size as a percentage of the buffer's total epoch tiles.
        auto core_relay_buffer_ids = build_core_to_relay_buffer_map(router, relay_buffer_ids);
        if (core_relay_buffer_ids.size() == 0) {
            return;
        }

        auto epoch_tiles_getter = [](const Router &router, unique_id_t id) -> int { 
            return router.get_buffer(id).info().scatter_gather_num_tiles();
        };
        upsize_buffers_by_attribute(router, core_relay_buffer_ids, buffer_size_change_in_tiles_and_bytes, epoch_tiles_getter);

        
        dump_buffer_upsize_report(router, core_relay_buffer_ids, buffer_size_change_in_tiles_and_bytes);
    }
}

}; // namespace router
