// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>

#include "device/soc_info.h"
#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"

#include "model/typedefs.h"

// Forward declaration.
class buda_SocDescriptor;

namespace pipegen2
{

class SoCDescriptorFileMock;

// Mocks SoCInfo object in order to circumvent reading from device descriptor file.
class SoCInfoMock : public SoCInfo
{
public:
    // Default constructor. Does nothing. Provides initialized but empty SoCInfo object access.
    SoCInfoMock();

    // This constructor is replacement for SoCInfo::parse_from_yaml.
    SoCInfoMock(const ChipId chip, const SoCDescriptorFileMock* soc_descriptor_file_mock);

    // Empty destructor.
    ~SoCInfoMock();

private:
    // Creates buda_SocDescriptor and manually fills in data usually read from mock SoC descriptor file in order to
    // avoid IO in UTs.
    static std::unique_ptr<buda_SocDescriptor> create_buda_soc_descriptor_mock(
        const SoCDescriptorFileMock* soc_descriptor_file_mock);
};

// Mocks reading from a soc descriptor file for all info we need.
class SoCDescriptorFileMock
{
public:
    // Factory method providing a concrete instance of file mock based on architecture.
    static std::unique_ptr<SoCDescriptorFileMock> get_instance(const tt::ARCH arch);

    virtual ~SoCDescriptorFileMock() = default;

    // Simulates reading "arch_name" field from SoC descriptor yaml.
    virtual tt::ARCH get_arch() const = 0;

    // Simulates reading "functional_workers" field from SoC descriptor yaml.
    virtual std::vector<tt_xy_pair> get_worker_cores() const = 0;

    // Simulates reading "eth" field from SoC descriptor yaml.
    virtual std::vector<tt_xy_pair> get_ethernet_cores() const = 0;

    // Simulates reading "pcie" field from SoC descriptor yaml.
    virtual std::vector<tt_xy_pair> get_pcie_cores() const = 0;

    // Simulates reading "dram" field from SoC descriptor yaml.
    virtual std::vector<std::vector<tt_xy_pair>> get_dram_cores() const = 0;

    // Helper method returning just first DRAM subchannel.
    virtual std::vector<tt_xy_pair> get_dram_cores_physical_locations_of_first_subchannel() const = 0;

    // Helper method providing mapping from logical to routing coordinates of worker cores.
    static void map_worker_logical_to_routing_coordinates(
        const std::vector<tt_xy_pair>& worker_cores,
        std::unordered_map<int, int>& worker_logical_to_routing_coordinates_x,
        std::unordered_map<int, int>& worker_logical_to_routing_coordinates_y);
};

class GSSoCDescriptorFileMock : public SoCDescriptorFileMock
{
public:
    GSSoCDescriptorFileMock() = default;

    ~GSSoCDescriptorFileMock() = default;

    tt::ARCH get_arch() const override { return tt::ARCH::GRAYSKULL; }

    std::vector<tt_xy_pair> get_worker_cores() const override;

    std::vector<tt_xy_pair> get_ethernet_cores() const override;

    std::vector<tt_xy_pair> get_pcie_cores() const override;

    std::vector<std::vector<tt_xy_pair>> get_dram_cores() const override;

    std::vector<tt_xy_pair> get_dram_cores_physical_locations_of_first_subchannel() const override;
};

class WHSoCDescriptorFileMock : public SoCDescriptorFileMock
{
public:
    WHSoCDescriptorFileMock() = default;

    ~WHSoCDescriptorFileMock() = default;

    virtual tt::ARCH get_arch() const override { return tt::ARCH::WORMHOLE_B0; }

    std::vector<tt_xy_pair> get_worker_cores() const override;

    std::vector<tt_xy_pair> get_ethernet_cores() const override;

    std::vector<tt_xy_pair> get_pcie_cores() const override;

    std::vector<std::vector<tt_xy_pair>> get_dram_cores() const override;

    std::vector<tt_xy_pair> get_dram_cores_physical_locations_of_first_subchannel() const override;
};

// Currently Blackhole behaves the same as Wormhole.
class BHSoCDescriptorFileMock : public WHSoCDescriptorFileMock
{
public:
    BHSoCDescriptorFileMock() = default;

    ~BHSoCDescriptorFileMock() = default;

    tt::ARCH get_arch() const override { return tt::ARCH::BLACKHOLE; }
};

} // namespace pipegen2
