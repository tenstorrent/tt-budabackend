// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "soc_info_mocks.h"

#include <memory>
#include <set>
#include <unordered_map>

// clang-format off
#include "common/buda_soc_descriptor.h"
#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
#include "utils/logger.hpp"

#include "src/pipegen2/unit_tests/mocks/device/soc_info_mocks.h"
// clang-format on

namespace pipegen2
{

SoCInfoMock::SoCInfoMock() = default;

SoCInfoMock::SoCInfoMock(const ChipId chip, const SoCDescriptorFileMock* soc_descriptor_file_mock)
{
    m_soc_descriptors.emplace(chip, SoCInfoMock::create_buda_soc_descriptor_mock(soc_descriptor_file_mock));
}

SoCInfoMock::~SoCInfoMock() {}

std::unique_ptr<buda_SocDescriptor> SoCInfoMock::create_buda_soc_descriptor_mock(
    const SoCDescriptorFileMock* soc_descriptor_file_mock)
{
    std::unique_ptr<buda_SocDescriptor> buda_soc_descriptor = std::make_unique<buda_SocDescriptor>();

    buda_soc_descriptor->arch = soc_descriptor_file_mock->get_arch();
    buda_soc_descriptor->workers = soc_descriptor_file_mock->get_worker_cores();
    buda_soc_descriptor->ethernet_cores = soc_descriptor_file_mock->get_ethernet_cores();
    buda_soc_descriptor->pcie_cores = soc_descriptor_file_mock->get_pcie_cores();
    buda_soc_descriptor->dram_cores = soc_descriptor_file_mock->get_dram_cores();
    SoCDescriptorFileMock::map_worker_logical_to_routing_coordinates(
        buda_soc_descriptor->workers,
        buda_soc_descriptor->worker_log_to_routing_x,
        buda_soc_descriptor->worker_log_to_routing_y,
        buda_soc_descriptor->routing_x_to_worker_x,
        buda_soc_descriptor->routing_y_to_worker_y);

    return buda_soc_descriptor;
}

std::unique_ptr<SoCDescriptorFileMock> SoCDescriptorFileMock::get_instance(const tt::ARCH arch)
{
    switch (arch)
    {
        case tt::ARCH::GRAYSKULL:
            return std::make_unique<GSSoCDescriptorFileMock>();
        case tt::ARCH::WORMHOLE:
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WHSoCDescriptorFileMock>();
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BHSoCDescriptorFileMock>();
        default:
            log_assert(false, "Unsupported device architecture");
    }
}

void SoCDescriptorFileMock::map_worker_logical_to_routing_coordinates(
    const std::vector<tt_xy_pair>& worker_cores,
    std::unordered_map<int, int>& worker_logical_to_routing_coordinates_x,
    std::unordered_map<int, int>& worker_logical_to_routing_coordinates_y,
    std::unordered_map<int, int>& routing_coordinates_x_to_worker_logical_x,
    std::unordered_map<int, int>& routing_coordinates_y_to_worker_logical_y)
{
    std::set<std::size_t> worker_x_coordinates;
    std::set<std::size_t> worker_y_coordinates;

    for (const tt_xy_pair& worker_core : worker_cores)
    {
        worker_x_coordinates.insert(worker_core.x);
        worker_y_coordinates.insert(worker_core.y);
    }

    unsigned int idx = 0;
    for (auto it = worker_x_coordinates.begin(); it != worker_x_coordinates.end(); ++it)
    {
        worker_logical_to_routing_coordinates_x[idx] = *it;
        routing_coordinates_x_to_worker_logical_x[*it] = idx;
        idx++;
    }

    idx = 0;
    for (auto it = worker_y_coordinates.begin(); it != worker_y_coordinates.end(); ++it)
    {
        worker_logical_to_routing_coordinates_y[idx] = *it;
        routing_coordinates_y_to_worker_logical_y[*it] = idx;
        idx++;
    }
}

std::vector<tt_xy_pair> GSSoCDescriptorFileMock::get_worker_cores() const
{
    std::vector<tt_xy_pair> worker_cores;

    for (int i = 1; i <= 12; ++i)
    {
        for (int j = 1; j <= 11; ++j)
        {
            if (j == 6)
            {
                continue;
            }

            worker_cores.push_back(tt_xy_pair(i, j));
        }
    }

    return worker_cores;
}

std::vector<tt_xy_pair> GSSoCDescriptorFileMock::get_ethernet_cores() const
{
    // No ethernet cores on GS.
    return {};
}

std::vector<tt_xy_pair> GSSoCDescriptorFileMock::get_pcie_cores() const { return {tt_xy_pair(0, 4)}; }

std::vector<std::vector<tt_xy_pair>> GSSoCDescriptorFileMock::get_dram_cores() const
{
    std::vector<std::vector<tt_xy_pair>> dram_cores;

    for (int i : {1, 4, 7, 10})
    {
        for (int j : {0, 6})
        {
            dram_cores.push_back({tt_xy_pair(i, j)});
        }
    }

    return dram_cores;
}

std::vector<tt_xy_pair> GSSoCDescriptorFileMock::get_dram_cores_physical_locations_of_first_subchannel() const
{
    std::vector<tt_xy_pair> dram_cores;

    for (int i : {1, 4, 7, 10})
    {
        for (int j : {0, 6})
        {
            dram_cores.push_back(tt_xy_pair(i, j));
        }
    }

    return dram_cores;
}

std::vector<tt_xy_pair> WHSoCDescriptorFileMock::get_worker_cores() const
{
    std::vector<tt_xy_pair> worker_cores;

    for (int j = 1; j <= 11; ++j)
    {
        for (int i = 1; i <= 9; ++i)
        {
            if (i == 5 || j == 6)
            {
                continue;
            }

            worker_cores.push_back(tt_xy_pair(i, j));
        }
    }

    return worker_cores;
}

std::vector<tt_xy_pair> WHSoCDescriptorFileMock::get_ethernet_cores() const
{
    return {
        tt_xy_pair(9, 0),
        tt_xy_pair(1, 0),
        tt_xy_pair(8, 0),
        tt_xy_pair(2, 0),
        tt_xy_pair(7, 0),
        tt_xy_pair(3, 0),
        tt_xy_pair(6, 0),
        tt_xy_pair(4, 0),
        tt_xy_pair(9, 6),
        tt_xy_pair(1, 6),
        tt_xy_pair(8, 6),
        tt_xy_pair(2, 6),
        tt_xy_pair(7, 6),
        tt_xy_pair(3, 6),
        tt_xy_pair(6, 6),
        tt_xy_pair(4, 6)};
}

std::vector<tt_xy_pair> WHSoCDescriptorFileMock::get_pcie_cores() const { return {tt_xy_pair(0, 3)}; }

std::vector<std::vector<tt_xy_pair>> WHSoCDescriptorFileMock::get_dram_cores() const
{
    return {
        {tt_xy_pair(0, 0), tt_xy_pair(0, 1), tt_xy_pair(0, 11)},
        {tt_xy_pair(0, 5), tt_xy_pair(0, 6), tt_xy_pair(0, 7)},
        {tt_xy_pair(5, 0), tt_xy_pair(5, 1), tt_xy_pair(5, 11)},
        {tt_xy_pair(5, 2), tt_xy_pair(5, 9), tt_xy_pair(5, 10)},
        {tt_xy_pair(5, 3), tt_xy_pair(5, 4), tt_xy_pair(5, 8)},
        {tt_xy_pair(5, 5), tt_xy_pair(5, 6), tt_xy_pair(5, 7)}};
}

std::vector<tt_xy_pair> WHSoCDescriptorFileMock::get_dram_cores_physical_locations_of_first_subchannel() const
{
    return {tt_xy_pair(0, 0), tt_xy_pair(0, 5), tt_xy_pair(5, 0), tt_xy_pair(5, 2), tt_xy_pair(5, 3), tt_xy_pair(5, 5)};
}

}  // namespace pipegen2