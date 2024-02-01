// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router.hpp"
#include "common/buda_soc_descriptor.h"
#include "device/tt_cluster_descriptor.h"
#include "net2pipe_common.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

using router::unique_id_t;
using router::router_buffer_info_t;
using router::pipe_t;
using chan_subchan_ntiles_t = std::tuple<int,int,int>;
using loc_buftype_ntiles_t = std::tuple<tt_cxy_pair, tt::RouterBufferType, int >;
using ClusterDescription = tt_ClusterDescriptor;

using op_map_t = std::unordered_map<unique_id_t, tt_op_info>;
using buffer_map_t = std::map<unique_id_t, router_buffer_info_t>;
using pipe_map_t = std::map<unique_id_t, pipe_t>;
//                                  name       ,          r   ,           c ,  buffer_id
using op_output_buf_map_t = std::map<std::string, std::map<int, std::map<int, unique_id_t>>>;
//                                  name       ,          r  ,            c,      operand, buffer_id
using op_input_buf_map_t = std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>>;
using op_intermediate_buf_map_t = std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>>;

struct test_queue_entry_t {
    unique_id_t q_id;
    tt_queue_info q;
    unique_id_t q_buf_id;
    router::router_buffer_info_t q_buf;
};
struct test_buf_entry_t {
    unique_id_t id;
    router::router_buffer_info_t buf;
};

struct eth_datacopy_test_info_t {
    std::map<std::string, tt_op_info> op_map;
    std::vector<unique_id_t> input_buffers;
    std::vector<unique_id_t> output_buffers;
    buffer_map_t buffer_map;
    std::vector<test_queue_entry_t> queues;
    std::unordered_map<std::uint64_t, tt_queue_info> queue_map;
    std::map<std::string, bool> op_queue_output_scatter;
    std::map<std::string, QueueSettings> queue_settings_map;
    op_input_buf_map_t op_input_buf_map;
    op_output_buf_map_t op_output_buf_map;
    op_intermediate_buf_map_t op_intermediate_buf_map;
    std::map<unique_id_t, pipe_t> pipes;
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes;
    std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> fused_op_schedule_map;
    const std::map<std::string, std::map<int, int> > op_input_kernel_tile_clear_granularity;
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes;
};

/*
 * light wrapper around router object to simplify constructor argument lifetimes
 * since some arguments are taken by const reference.
 * Usage should be same as plain old router
 */
struct TestRouter {
  private:
    TestRouter(
        const buda_SocDescriptor &soc_desc, 
        const ClusterDescription &cluster_description, 
        const eth_datacopy_test_info_t &test_info
    );

    void initialize_router();

  public:

    static std::unique_ptr<TestRouter> generate_test_router(
        const buda_SocDescriptor &soc_desc, 
        const ClusterDescription &cluster_description, 
        const eth_datacopy_test_info_t &test_info
    );

    router::Router &get() { return *(router.get()); }
    std::unique_ptr<router::Router> router;
    const std::unordered_map<chip_id_t, buda_SocDescriptor> soc_desc;
    const ClusterDescription &cluster_description;
    buffer_map_t buffer_map;
    std::unordered_map<unique_id_t, tt_queue_info> queue_map;
    std::map<std::string, bool> op_queue_output_scatter;
    std::map<std::string, QueueSettings> queue_settings_map;
    std::map<unique_id_t, pipe_t> pipes;
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes;
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes;
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> op_input_buf_map;
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> op_intermediate_buf_map;
    std::map<std::string, std::map<int, std::map<int, unique_id_t>>> op_output_buf_map;
    std::map<std::string, int> op_queue_output_buf_granularity;
    const std::map<std::string, std::map<int, int>> op_input_kernel_tile_clear_granularity;
    std::map<std::string, tt_op_info> op_info_map;
    std::vector<int> chip_ids;

    std::map<std::string, std::map<int, std::string>> op_input_name_map;
    std::unordered_map<std::string, std::vector<n2p::prolog_buffer>> prolog_buffers_per_op;
    std::map<std::string, std::map<std::string, bool>> prolog_post_tm_operand;
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> prolog_queue_name_fork_buf_unique_id_map;
    std::map<std::string, std::vector<std::string>> op_queue_output_map;
    std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> fused_op_schedule_map;
    std::vector<std::uint64_t>  hronological_unique_id_list;
};


std::unique_ptr<buda_SocDescriptor> load_test_wh_soc_descriptor();
extern n2p::UniqueIdGenerator unique_id_generator;
extern const std::unique_ptr<buda_SocDescriptor> wh_soc_desc;
extern const std::unique_ptr<ClusterDescription> wh_2chip_cluster;
extern const std::unique_ptr<ClusterDescription> wh_4chip_cluster;
std::tuple<unique_id_t, tt_queue_info, unique_id_t, router::router_buffer_info_t> generate_dummy_router_queue(int channel, int subchannel, const buda_SocDescriptor &soc_descriptor, int size_tiles);

std::tuple<
    std::vector<test_queue_entry_t>,
    std::unordered_map<std::uint64_t, tt_queue_info>,
    std::map<std::string, bool>,
    std::map<std::string, QueueSettings>
> generate_multiple_dummy_router_queues(const buda_SocDescriptor &soc_descriptor, std::map<unique_id_t, router_buffer_info_t> &buffer_map, const std::vector<chan_subchan_ntiles_t> &specs);

std::vector<unique_id_t> generate_test_pipes(const buda_SocDescriptor &soc_descriptor, const std::vector<pipe_t> &pipes, eth_datacopy_test_info_t &test_info);

std::vector<test_buf_entry_t>  generate_multiple_dummy_router_buffers(const buda_SocDescriptor &soc_descriptor, std::map<unique_id_t, router_buffer_info_t> &buffer_map, const std::vector<loc_buftype_ntiles_t> &specs);

tt::buffer_info generate_dummy_buffer_info(int size_tiles, tt::RouterBufferType type);

std::tuple<router::unique_id_t, router::router_buffer_info_t> generate_dummy_test_router_buffer(const tt_cxy_pair &core, tt::RouterBufferType type, int size_tiles = 1);

std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    const std::string cluster_description_path, 
    const eth_datacopy_test_info_t &test_info
);
std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    const tt_ClusterDescriptor &cluster_description, 
    const eth_datacopy_test_info_t &test_info
);
std::unique_ptr<TestRouter> generate_test_router(
    const buda_SocDescriptor &soc_desc, 
    int num_chips, 
    const eth_datacopy_test_info_t &test_info
);