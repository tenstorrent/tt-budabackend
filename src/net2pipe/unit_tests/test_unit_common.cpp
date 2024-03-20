// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/tt_cluster_descriptor.h"
#include "netlist_basic_info_types.hpp"
#include "router.hpp"
#include "common/buda_soc_descriptor.h"

#include "net2pipe.h"

#include "netlist/netlist_info_types.hpp"
#include "src/net2pipe/inc/router_types.h"
#include "test_unit_common.hpp"
#include "unique_id_generator.h"
#include <memory>
#include <vector>
#include <unordered_set>

using n2p::UNIQUE_ID_ALIGN;
using router::unique_id_t;
using router::pipe_t;
using router::router_buffer_info_t;
using router::buffer_queue_info_t;
using router::Router;

n2p::UniqueIdGenerator unique_id_generator;

std::unique_ptr<buda_SocDescriptor> load_test_wh_soc_descriptor() {
    return load_soc_descriptor_from_yaml("device/wormhole_8x10.yaml");
}

const std::unique_ptr<buda_SocDescriptor> wh_soc_desc = load_test_wh_soc_descriptor();
const std::unique_ptr<tt_ClusterDescriptor> wh_2chip_cluster = tt_ClusterDescriptor::create_from_yaml("wormhole_2chip_cluster.yaml");
const std::unique_ptr<tt_ClusterDescriptor> wh_4chip_cluster = tt_ClusterDescriptor::create_from_yaml("src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_linear_cluster.yaml");

tt::buffer_info generate_dummy_buffer_info(int size_tiles, tt::RouterBufferType type) {
    return tt::buffer_info(
        type,
        0,
        size_tiles,
        size_tiles, 
        size_tiles,
        size_tiles,
        1,
        DataFormat::Float16,
        tt_xy_pair(32, 32)
    );
}

std::tuple<unique_id_t, tt_queue_info, unique_id_t, router::router_buffer_info_t> generate_dummy_router_queue(int channel, int subchannel, const buda_SocDescriptor &soc_descriptor, int size_tiles) {
    
    auto queue_info_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);

    const tt_cxy_pair &core = tt_cxy_pair(0, soc_descriptor.dram_cores.at(channel).at(subchannel));
    const auto &buffer_info = generate_dummy_buffer_info(size_tiles, tt::RouterBufferType::DRAM);
    const auto &alloc_info = router::router_queue_allocation_info{tt_queue_allocation_info{.channel = static_cast<std::uint32_t>(channel), .address = 0x1000}};
    const auto queue_info = tt_queue_info {
        .name=std::to_string(queue_info_id),
        .grid_size = tt::tt_grid_shape{
            .r = 1,
            .c = 1
        },
        .input_count = 1,
        .dim=tt_dim_info{
            .t = buffer_info.t(),
            .ublock_rt = buffer_info.ublock_rt(),
            .ublock_ct = buffer_info.ublock_ct(),
            .mblock_m = buffer_info.mblock_m(),
            .mblock_n = buffer_info.mblock_n()
        },
        .data_format = buffer_info.data_format(),
        .loc = QUEUE_LOCATION::DRAM,
        .alloc_info = {alloc_info}
    };
    auto queue_buffer_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
    bool is_prolog = false;
    return {
        queue_info_id,
        queue_info,
        queue_buffer_id,
        router::router_buffer_info_t::create_mutable(
            core,
            buffer_queue_info_t (
                queue_info_id,
                alloc_info,
                is_prolog
            )
        )
    };
}


std::tuple<unique_id_t, router::router_buffer_info_t> generate_dummy_test_router_buffer(const tt_cxy_pair &core, tt::RouterBufferType type, int size_tiles) {
    
    return {
        unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN),
        router_buffer_info_t::create_mutable(
            core,
            generate_dummy_buffer_info(size_tiles, type)
        )
    };
}



std::tuple<
    std::vector<test_queue_entry_t>,
    std::unordered_map<std::uint64_t, tt_queue_info>,
    std::map<std::string, bool>,
    std::map<std::string, QueueSettings>
> generate_multiple_dummy_router_queues(
    const buda_SocDescriptor &soc_descriptor, 
    std::map<unique_id_t, router_buffer_info_t> &buffer_map,
    const std::vector<chan_subchan_ntiles_t> &specs
) {
    std::map<std::string, bool> op_queue_output_scatter;
    std::map<std::string, QueueSettings> queue_settings_map;
    auto router_queues = std::vector<test_queue_entry_t>{};
    router_queues.reserve(specs.size());
    auto queue_map = std::unordered_map<std::uint64_t, tt_queue_info>{};
    queue_map.reserve(specs.size());
    for (const auto &[channel, subchannel, ntiles] : specs) {
        const auto &[q_id, q, q_buf_id, q_buf] = generate_dummy_router_queue(channel, subchannel, soc_descriptor, ntiles);
        queue_map.insert({q_id, q});
        router_queues.push_back(test_queue_entry_t{.q_id=q_id, .q=q, .q_buf_id=q_buf_id, .q_buf=q_buf});
        buffer_map.insert({q_buf_id, q_buf});
        op_queue_output_scatter[q.name] = false;
        queue_settings_map[q.name] = {};
    }

    return {router_queues, queue_map, op_queue_output_scatter, queue_settings_map};
}

std::vector<test_buf_entry_t> generate_multiple_dummy_router_buffers(
    const buda_SocDescriptor &soc_descriptor, std::map<unique_id_t, router_buffer_info_t> &buffer_map, const std::vector<loc_buftype_ntiles_t> &specs
) {
    auto router_buffers = std::vector<test_buf_entry_t>{};
    router_buffers.reserve(specs.size());
    for (const auto &[location, type, ntiles] : specs) {
        const auto &[id, buf] = generate_dummy_test_router_buffer(location, type, ntiles);
        log_debug(tt::LogRouter, "id: {}", id);
        buffer_map.insert({id, buf});
        router_buffers.push_back(test_buf_entry_t{.id=id, .buf=buf});
    }

    return router_buffers;
}

std::vector<unique_id_t> generate_test_pipes(
    const buda_SocDescriptor &soc_descriptor, const std::vector<pipe_t> &pipes, eth_datacopy_test_info_t &test_info
) {
    auto ids = std::vector<unique_id_t>{};
    for (const auto &pipe : pipes) {
        auto id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
        ids.push_back(id);
        test_info.pipes.insert({id, pipe});
        for (auto buffer_id : pipe.input_buffer_ids) {
            test_info.buffer_output_pipes[buffer_id].insert(id);
        }
        if (pipe.has_multiple_timesteps()) {
            for (auto buffer_ids : pipe.time_multiplexed_output_buffer_ids()) {
                for (auto buffer_id : buffer_ids) {
                    test_info.buffer_input_pipes.insert({buffer_id, id});
                }
            }
        } else {
            for (auto buffer_id : pipe.output_buffer_ids()) {
                test_info.buffer_input_pipes.insert({buffer_id, id});
            }
        }
    }

    return ids;
}


TestRouter::TestRouter(
    const buda_SocDescriptor &soc_desc, 
    const tt_ClusterDescriptor &cluster_description, 
    const eth_datacopy_test_info_t &test_info
) :
    soc_desc([&soc_desc,&cluster_description]() {
        std::unordered_map<chip_id_t, buda_SocDescriptor> soc_descs;
        for (chip_id_t chip_id : cluster_description.get_all_chips()) {
            soc_descs[chip_id] = soc_desc;
        }
        return soc_descs;
        }()),
    cluster_description(cluster_description),
    buffer_map(test_info.buffer_map),
    queue_map(test_info.queue_map),
    op_queue_output_scatter(test_info.op_queue_output_scatter),
    queue_settings_map(test_info.queue_settings_map),
    pipes(test_info.pipes),
    buffer_input_pipes(test_info.buffer_input_pipes),
    buffer_output_pipes(test_info.buffer_output_pipes),
    op_input_buf_map(test_info.op_input_buf_map),
    op_intermediate_buf_map(test_info.op_intermediate_buf_map),
    op_output_buf_map(test_info.op_output_buf_map),
    fused_op_schedule_map(test_info.fused_op_schedule_map),
    op_input_kernel_tile_clear_granularity(test_info.op_input_kernel_tile_clear_granularity),
    op_info_map(test_info.op_map)
{
    const auto &chip_ids_set = cluster_description.get_all_chips();
    this->chip_ids = std::vector<int>(chip_ids_set.size());
    std::iota(std::begin(this->chip_ids), std::end(this->chip_ids), 0);
    for (auto i : this->chip_ids) {
        TT_ASSERT(chip_ids_set.find(i) != chip_ids_set.end());
    }
    for (auto const& [q_id, queue_info] : queue_map) {
        op_queue_output_buf_granularity[queue_info.name] = 1;
    }
    TT_ASSERT(chip_ids.size() > 0);
}

void TestRouter::initialize_router() {
    // !!!!!
    // If you get a compile error here because of changed interface between net2pipe/router
    // then I recommend you add the field into TestRouter as a member and just pass it in here
    // directly. It's not ideal but atleast not tests should be more robust to router API changes
    // without requiring a lot of work to feed these datastructures through all the tests down
    // to the router.
    // !!!!!
    this->router = std::unique_ptr<Router>(new Router(
        soc_desc,
        this->chip_ids,
        ClusterGraph(cluster_description),

        UNIQUE_ID_ALIGN,
        1,
        unique_id_generator,

        this->queue_map,
        this->buffer_map,
        this->pipes,
        this->buffer_output_pipes,
        this->buffer_input_pipes,
        this->op_input_buf_map,
        this->op_intermediate_buf_map,
        this->op_output_buf_map,
        this->fused_op_schedule_map,

        this->op_queue_output_scatter,          // std::map<std::string, bool> &_op_queue_output_scatter
        this->op_queue_output_buf_granularity,  // std::map<std::string, int> &op_queue_output_buf_granularity
        this->op_input_kernel_tile_clear_granularity,
        this->op_info_map,
        this->queue_settings_map,
        this->op_input_name_map,
        this->prolog_buffers_per_op,
        this->prolog_post_tm_operand,
        this->prolog_queue_name_fork_buf_unique_id_map,
        this->op_queue_output_map,
        this->hronological_unique_id_list
        )
    );
}


std::unique_ptr<TestRouter> TestRouter::generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    const tt_ClusterDescriptor &cluster_description,
    const eth_datacopy_test_info_t &test_info
) 
{
    auto test_info_copy = test_info;
    for (const auto &[id, buf] : test_info_copy.buffer_map) {
        if (test_info_copy.buffer_output_pipes.find(id) == test_info_copy.buffer_output_pipes.end()) {
            test_info_copy.buffer_output_pipes[id] = {};
        }
    }

    for (const auto &[id, q] : test_info_copy.queue_map) {
        if (test_info_copy.op_queue_output_scatter.find(q.name) == test_info_copy.op_queue_output_scatter.end()) {
            test_info_copy.op_queue_output_scatter.insert({q.name, false});
            log_debug(tt::LogTest, "op_queue_output_scatter[{}]=false", q.name);
        }
    }

    std::unique_ptr<TestRouter> test_router = std::unique_ptr<TestRouter>(
        new TestRouter(  
            soc_desc,
            cluster_description,
            test_info_copy
        )
    );

    test_router->initialize_router();

    return test_router;
}

std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    const tt_ClusterDescriptor &cluster_description,
    const eth_datacopy_test_info_t &test_info
) {
    return TestRouter::generate_test_router(soc_desc, cluster_description, test_info);
}

std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    int num_chips, 
    const eth_datacopy_test_info_t &test_info
    ) {
    tt_ClusterDescriptor *cluster_descriptor = nullptr;
    switch(num_chips) {
        case 1: cluster_descriptor = wh_2chip_cluster.get(); break;
        case 2: cluster_descriptor = wh_2chip_cluster.get(); break;
        case 4: cluster_descriptor = wh_4chip_cluster.get(); break;

        default: TT_ASSERT(false, "Unsupported num_chips in test flow. Add a new cluster description file to support this # chips");
    }
    return generate_test_router(
        soc_desc, 
        *cluster_descriptor, 
        test_info
    );
}



std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    const std::string cluster_description_path, 
    const eth_datacopy_test_info_t &test_info
) {

    auto cluster_description_ptr = tt_ClusterDescriptor::create_from_yaml(cluster_description_path);
    auto cluster_description = *(cluster_description_ptr.get());
    return generate_test_router(
        soc_desc,
        cluster_description,
        test_info
    );

}
