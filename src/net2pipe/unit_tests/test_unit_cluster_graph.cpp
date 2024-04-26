// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router/router_multichip.h"
#include "test_unit_common.hpp"
#include "router/router_passes_common.h"
#include "gtest/gtest.h"

#include "common/tt_cluster_graph.hpp"
#include <unordered_map>

static const std::string four_chip_2d_cluster_descriptor_file = "src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_square_cluster.yaml";
        


TEST(ClusterGraph, API_TestWith_FourChip2D) {
    auto cd = ClusterDescription::create_from_yaml(four_chip_2d_cluster_descriptor_file);
    auto cg = ClusterGraph(*(cd.get()));

    const auto &eth_connections = cd->get_ethernet_connections();
    ASSERT_EQ(eth_connections.at(0).size(), 8);
    ASSERT_EQ(eth_connections.at(1).size(), 8);
    ASSERT_EQ(eth_connections.at(2).size(), 8);
    ASSERT_EQ(eth_connections.at(3).size(), 8);

    ASSERT_TRUE(cd->is_chip_mmio_capable(0));
    ASSERT_FALSE(cd->is_chip_mmio_capable(1));
    ASSERT_FALSE(cd->is_chip_mmio_capable(2));
    ASSERT_FALSE(cd->is_chip_mmio_capable(3));

    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(0)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(0)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(1)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(1)), 1);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(2)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(2)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(3)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(3)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(4)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(4)), 4);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(5)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(5)), 5);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(6)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(6)), 6);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(7)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(7)), 7);

    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(0)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(0)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(1)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(1)), 1);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(2)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(2)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(3)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(3)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(4)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(4)), 4);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(5)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(5)), 5);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(6)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(6)), 6);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(7)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(7)), 7);

    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(0)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(0)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(1)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(1)), 1);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(2)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(2)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(3)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(3)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(4)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(4)), 4);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(5)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(5)), 5);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(6)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(6)), 6);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(7)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(7)), 7);

    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(0)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(0)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(1)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(1)), 1);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(2)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(2)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(3)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(3)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(4)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(4)), 4);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(5)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(5)), 5);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(6)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(6)), 6);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(7)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(7)), 7);

    const auto &chip_0_neighbours = cg.get_neighbours(0);
    ASSERT_TRUE(chip_0_neighbours.find(1) != chip_0_neighbours.end());
    ASSERT_TRUE(chip_0_neighbours.find(2) != chip_0_neighbours.end());
    ASSERT_TRUE(chip_0_neighbours.find(3) == chip_0_neighbours.end());
    ASSERT_TRUE(chip_0_neighbours.find(0) == chip_0_neighbours.end());

    const auto &chip_1_neighbours = cg.get_neighbours(1);
    ASSERT_TRUE(chip_1_neighbours.find(0) != chip_1_neighbours.end());
    ASSERT_TRUE(chip_1_neighbours.find(3) != chip_1_neighbours.end());
    ASSERT_TRUE(chip_1_neighbours.find(2) == chip_1_neighbours.end());
    ASSERT_TRUE(chip_1_neighbours.find(1) == chip_1_neighbours.end());

    const auto &chip_2_neighbours = cg.get_neighbours(2);
    ASSERT_TRUE(chip_2_neighbours.find(0) != chip_2_neighbours.end());
    ASSERT_TRUE(chip_2_neighbours.find(3) != chip_2_neighbours.end());
    ASSERT_TRUE(chip_2_neighbours.find(2) == chip_2_neighbours.end());
    ASSERT_TRUE(chip_2_neighbours.find(1) == chip_2_neighbours.end());

    const auto &chip_3_neighbours = cg.get_neighbours(3);
    ASSERT_TRUE(chip_3_neighbours.find(1) != chip_3_neighbours.end());
    ASSERT_TRUE(chip_3_neighbours.find(2) != chip_3_neighbours.end());
    ASSERT_TRUE(chip_3_neighbours.find(3) == chip_3_neighbours.end());
    ASSERT_TRUE(chip_3_neighbours.find(0) == chip_3_neighbours.end());

}


TEST(ClusterGraph, API_TestWith_TwoChip) {
    auto cd = *(wh_2chip_cluster.get());
    auto cg = ClusterGraph(cd);

    const auto &eth_connections = cd.get_ethernet_connections();
    ASSERT_EQ(eth_connections.at(0).size(), 4);
    ASSERT_EQ(eth_connections.at(1).size(), 4);

    ASSERT_TRUE(cd.is_chip_mmio_capable(0));
    ASSERT_FALSE(cd.is_chip_mmio_capable(1));

    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(0)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(0)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(1)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(1)), 1);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(2)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(2)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(3)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(3)), 3);

    const auto &chip_0_neighbours = cg.get_neighbours(0);
    ASSERT_TRUE(chip_0_neighbours.find(1) != chip_0_neighbours.end());

    const auto &chip_1_neighbours = cg.get_neighbours(1);
    ASSERT_TRUE(chip_1_neighbours.find(0) != chip_1_neighbours.end());

}

TEST(ClusterGraph, API_TestWith_3x3Cluster) {
    auto cd = ClusterDescription::create_from_yaml("src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml");
    auto cg = ClusterGraph(*(cd.get()));

    const auto &eth_connections = cd->get_ethernet_connections();
    ASSERT_EQ(eth_connections.at(0).size(), 2);
    ASSERT_EQ(eth_connections.at(1).size(), 3);
    ASSERT_EQ(eth_connections.at(2).size(), 2);
    ASSERT_EQ(eth_connections.at(3).size(), 3);
    ASSERT_EQ(eth_connections.at(4).size(), 4);
    ASSERT_EQ(eth_connections.at(5).size(), 3);
    ASSERT_EQ(eth_connections.at(6).size(), 2);
    ASSERT_EQ(eth_connections.at(7).size(), 3);
    ASSERT_EQ(eth_connections.at(8).size(), 2);

    ASSERT_TRUE(cd->is_chip_mmio_capable(0));
    ASSERT_FALSE(cd->is_chip_mmio_capable(1));
    ASSERT_FALSE(cd->is_chip_mmio_capable(2));
    ASSERT_FALSE(cd->is_chip_mmio_capable(3));
    ASSERT_FALSE(cd->is_chip_mmio_capable(4));
    ASSERT_FALSE(cd->is_chip_mmio_capable(5));
    ASSERT_FALSE(cd->is_chip_mmio_capable(6));
    ASSERT_FALSE(cd->is_chip_mmio_capable(7));
    ASSERT_FALSE(cd->is_chip_mmio_capable(8));

    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(1)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(1)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(2)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(2)), 0);
    
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(1)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(1)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(2)), 4);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(2)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(1).at(3)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(1).at(3)), 1);

    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(2)), 5);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(2)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(2).at(3)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(2).at(3)), 1);

    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(0)), 0);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(1)), 4);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(1)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(3).at(2)), 6);
    ASSERT_EQ(std::get<1>(eth_connections.at(3).at(2)), 0);

    ASSERT_EQ(std::get<0>(eth_connections.at(4).at(0)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(4).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(4).at(1)), 5);
    ASSERT_EQ(std::get<1>(eth_connections.at(4).at(1)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(4).at(2)), 7);
    ASSERT_EQ(std::get<1>(eth_connections.at(4).at(2)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(4).at(3)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(4).at(3)), 1);

    ASSERT_EQ(std::get<0>(eth_connections.at(5).at(0)), 2);
    ASSERT_EQ(std::get<1>(eth_connections.at(5).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(5).at(2)), 8);
    ASSERT_EQ(std::get<1>(eth_connections.at(5).at(2)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(5).at(3)), 4);
    ASSERT_EQ(std::get<1>(eth_connections.at(5).at(3)), 1);

    ASSERT_EQ(std::get<0>(eth_connections.at(6).at(0)), 3);
    ASSERT_EQ(std::get<1>(eth_connections.at(6).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(6).at(1)), 7);
    ASSERT_EQ(std::get<1>(eth_connections.at(6).at(1)), 3);

    ASSERT_EQ(std::get<0>(eth_connections.at(7).at(0)), 4);
    ASSERT_EQ(std::get<1>(eth_connections.at(7).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(7).at(1)), 8);
    ASSERT_EQ(std::get<1>(eth_connections.at(7).at(1)), 3);
    ASSERT_EQ(std::get<0>(eth_connections.at(7).at(3)), 6);
    ASSERT_EQ(std::get<1>(eth_connections.at(7).at(3)), 1);

    ASSERT_EQ(std::get<0>(eth_connections.at(8).at(0)), 5);
    ASSERT_EQ(std::get<1>(eth_connections.at(8).at(0)), 2);
    ASSERT_EQ(std::get<0>(eth_connections.at(8).at(3)), 7);
    ASSERT_EQ(std::get<1>(eth_connections.at(8).at(3)), 1);

    auto validate_neighbours = [](const auto &cg, int chip, const auto &neighbours_expected, int num_chips) {
        const auto &computed_neighbours = cg.get_neighbours(chip);
        std::vector<chip_id_t> missing_neighbours = {};
        std::vector<chip_id_t> extra_neighbours = {};
        for (int i = 0; i < num_chips; i++) {
            bool expect_in_neighbours_list = (neighbours_expected.find(i) != neighbours_expected.end());
            if (expect_in_neighbours_list) {
                if(computed_neighbours.find(i) == computed_neighbours.end()) {
                    missing_neighbours.push_back(i);
                }
            } else {
                if (computed_neighbours.find(i) != computed_neighbours.end()) {
                    extra_neighbours.push_back(i);
                }
            }
        }
        if (missing_neighbours.size() > 0) {
            log_error("Missing Neighbour for chip {}", chip);
            for (auto n : missing_neighbours) {
                log_error("\t{}", n);
            }
            log_error("Missing Neighbour for chip {}", chip);
        }
        if (extra_neighbours.size() > 0) {
            for (auto n : extra_neighbours) {
                log_error("\t{}", n);
            }
        }
        ASSERT_TRUE(extra_neighbours.size() == 0 && missing_neighbours.size() == 0);
    };

    validate_neighbours(cg, 0, std::unordered_set<int>{1,3}, 9); 
    validate_neighbours(cg, 1, std::unordered_set<int>{0,4,2}, 9); 
    validate_neighbours(cg, 2, std::unordered_set<int>{1,5}, 9); 
    validate_neighbours(cg, 3, std::unordered_set<int>{0,4,6}, 9); 
    validate_neighbours(cg, 4, std::unordered_set<int>{1,3,5,7}, 9); 
    validate_neighbours(cg, 5, std::unordered_set<int>{2,4,8}, 9); 
    validate_neighbours(cg, 6, std::unordered_set<int>{3,7}, 9); 
    validate_neighbours(cg, 7, std::unordered_set<int>{6,4,8}, 9); 
    validate_neighbours(cg, 8, std::unordered_set<int>{7,5}, 9);
}

TEST(ClusterGraph, API_TestWith_Bidirectional_EthConnections) {
    auto cd = ClusterDescription::create_from_yaml(
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_2chip_bidirectional_cluster.yaml");
    auto cg = ClusterGraph(*(cd.get()));

    const auto &eth_connections = cd->get_ethernet_connections();
    ASSERT_EQ(eth_connections.at(0).size(), 2);
    ASSERT_EQ(eth_connections.at(1).size(), 2);

    ASSERT_TRUE(cd->is_chip_mmio_capable(0));
    ASSERT_FALSE(cd->is_chip_mmio_capable(1));

    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(8)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(8)), 0);
    ASSERT_EQ(std::get<0>(eth_connections.at(0).at(9)), 1);
    ASSERT_EQ(std::get<1>(eth_connections.at(0).at(9)), 1);

    const auto &chip_0_neighbours = cg.get_neighbours(0);
    ASSERT_TRUE(chip_0_neighbours.find(1) != chip_0_neighbours.end());
    ASSERT_TRUE(chip_0_neighbours.find(0) == chip_0_neighbours.end());

    const auto &chip_1_neighbours = cg.get_neighbours(1);
    ASSERT_TRUE(chip_1_neighbours.find(0) != chip_1_neighbours.end());
    ASSERT_TRUE(chip_1_neighbours.find(1) == chip_1_neighbours.end());
}

TEST(ClusterGraph, API_TestWith_Invalid_Bidirectional_EthConnections) {
    setenv("LOGGER_LEVEL", "disabled", 1);
    EXPECT_THROW(
        {
            try {
                auto cd = ClusterDescription::create_from_yaml(
                    "src/net2pipe/unit_tests/cluster_descriptions/wormhole_2chip_invalid_bidirectional_cluster.yaml");
            } catch (const std::runtime_error &e) {
                std::string err(e.what());
                ASSERT_TRUE(err.find("Duplicate eth connection found in cluster desc yaml") != std::string::npos);
                throw;
            }
        },
        std::runtime_error);
    unsetenv("LOGGER_LEVEL");
}
