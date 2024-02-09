// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "device/tt_xy_pair.h"
#include "noc_parameters.h"

#include "device/soc_info_constants.h"
#include "mocks/device/soc_info_mocks.h"
#include "soc_info_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;

/**********************************************************************************************************************
    Test fixtures used for tests in this file.
**********************************************************************************************************************/

class Pipegen2_SoCInfo : public testing::Test
{
protected:
    void SetUp() override
    {
        create_soc_info_mock();
    }

    // Creates SoCDescriptorFileMock instead of reading from disk and based on it creates SoCInfoMock object.
    void create_soc_info_mock()
    {
        m_soc_descriptor_file_mock = SoCDescriptorFileMock::get_instance(unit_test_utils::get_build_arch());
        m_soc_info = std::make_unique<SoCInfoMock>(m_chip_id, m_soc_descriptor_file_mock.get());
    }

    // Use only one dummy chip.
    const ChipId m_chip_id = 0;

    // Pointer to a mock of SoC descriptor file. In this object "true" values are stored.
    std::unique_ptr<SoCDescriptorFileMock> m_soc_descriptor_file_mock;

    // SoCInfo mock object through which SoCInfo functionality is tested.
    std::unique_ptr<SoCInfoMock> m_soc_info;
};

class Pipegen2_SoCInfo_GS : public Pipegen2_SoCInfo
{
protected:
    void SetUp() override
    {
        if (unit_test_utils::get_build_arch() != tt::ARCH::GRAYSKULL)
        {
            // Test skipped since it is only valid for grayskull arch.
            GTEST_SKIP();
        }

        Pipegen2_SoCInfo::SetUp();
    }
};

class Pipegen2_SoCInfo_WH : public Pipegen2_SoCInfo
{
protected:
    void SetUp() override
    {
        tt::ARCH arch = unit_test_utils::get_build_arch();

        if (arch != tt::ARCH::WORMHOLE_B0 && arch != tt::ARCH::WORMHOLE)
        {
            // Test skipped since it is only valid for wormhole arch.
            GTEST_SKIP();
        }

        Pipegen2_SoCInfo::SetUp();
    }
};

class Pipegen2_SoCInfo_BH : public Pipegen2_SoCInfo
{
protected:
    void SetUp() override
    {
        if (unit_test_utils::get_build_arch() != tt::ARCH::BLACKHOLE)
        {
            // Test skipped since it is only valid for blackhole arch.
            GTEST_SKIP();
        }

        Pipegen2_SoCInfo::SetUp();
    }
};

/**********************************************************************************************************************
    Tests for function: get_device_arch
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetDeviceArch_AlwaysThrowsExc)
{
    // Use default constructor provided to fetch an empty SoCInfo object with no information about SoC.
    SoCInfoMock soc_info;
    EXPECT_THROW(soc_info.get_device_arch(), std::runtime_error);
}

TEST_F(Pipegen2_SoCInfo, GetDeviceArch_ExpectingExactReturnValue)
{
    // Expecting that we created SoCInfo for arch for which project was built.
    tt::ARCH arch;
    EXPECT_NO_THROW(arch = m_soc_info->get_device_arch());
    EXPECT_EQ(arch, unit_test_utils::get_build_arch());
}

/**********************************************************************************************************************
    Tests for function: get_chip_ids
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetChipIds_ExpectingExactReturnValue)
{
    // Expecting that we created SoCInfo just for dummy chip.
    EXPECT_EQ(m_soc_info->get_chip_ids(), std::vector<ChipId>{m_chip_id});
}

/**********************************************************************************************************************
    Tests for function: get_worker_cores_physical_locations
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetWorkerCoresPhysicalLocations_ExpectingExactReturnValue)
{
    // Expecting that SoCInfo object contains same worker cores as in soc descriptor.
    expect_equivalent_vectors(
        m_soc_info->get_worker_cores_physical_locations(m_chip_id),
        m_soc_descriptor_file_mock->get_worker_cores());
}

/**********************************************************************************************************************
    Tests for function: get_ethernet_cores_physical_locations
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetEthernetCoresPhysicalLocations_ExpectingExactReturnValue)
{
    // Expecting that SoCInfo object contains same ethernet cores as in soc descriptor.
    expect_equivalent_vectors(
        m_soc_info->get_ethernet_cores_physical_locations(m_chip_id),
        m_soc_descriptor_file_mock->get_ethernet_cores());
}

/**********************************************************************************************************************
    Tests for function: get_ethernet_channel_core_physical_location
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetEthernetChannelCorePhysicalLocation_AlwaysThrowsExc)
{
    // Ethernet channel must be within [0, num_eth_cores-1] range. Everything outside of that will throw an error.
    EXPECT_THROW(
        m_soc_info->get_ethernet_channel_core_physical_location(m_chip_id, -1),
        std::runtime_error);

    std::size_t expected_number_eth_cores = m_soc_descriptor_file_mock->get_ethernet_cores().size();

    EXPECT_THROW(
        m_soc_info->get_ethernet_channel_core_physical_location(m_chip_id, expected_number_eth_cores),
        std::runtime_error);
}

TEST_F(Pipegen2_SoCInfo, GetEthernetChannelCorePhysicalLocation_ExpectingExactReturnValue)
{
    std::vector<tt_xy_pair> expected_eth_cores = m_soc_descriptor_file_mock->get_ethernet_cores();

    // Entire valid range of eth channels should return exactly the same cores as in soc descriptor.
    for (unsigned int eth_channel = 0; eth_channel < expected_eth_cores.size(); ++eth_channel)
    {
        tt_cxy_pair eth_channel_core_location;
        EXPECT_NO_THROW(
            eth_channel_core_location = m_soc_info->get_ethernet_channel_core_physical_location(
                m_chip_id, eth_channel));

        EXPECT_EQ(eth_channel_core_location, tt_cxy_pair(m_chip_id, expected_eth_cores[eth_channel]));
    }
}

/**********************************************************************************************************************
    Tests for function: get_dram_cores_physical_locations
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetDramCoresPhysicalLocations_ExpectingExactReturnValue)
{
    // Expecting that SoCInfo object contains same dram cores as in soc descriptor.
    expect_equivalent_vectors(
        m_soc_info->get_dram_cores_physical_locations(m_chip_id),
        m_soc_descriptor_file_mock->get_dram_cores());
}

/**********************************************************************************************************************
    Tests for function: get_dram_cores_physical_locations_of_first_subchannel
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetDramCoresPhysicalLocationsOfFirstSubchannel_ExpectingExactReturnValue)
{
    std::vector<tt_cxy_pair> dram_cores_in_first_subchannel;
    EXPECT_NO_THROW(
        dram_cores_in_first_subchannel = m_soc_info->get_dram_cores_physical_locations_of_first_subchannel(m_chip_id));

    expect_equivalent_vectors(
        dram_cores_in_first_subchannel,
        m_soc_descriptor_file_mock->get_dram_cores_physical_locations_of_first_subchannel());
}

/**********************************************************************************************************************
    Tests for function: convert_logical_to_physical_worker_core_coords
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo_GS, ConvertLogicalToPhysicalWorkerCoreCoords_ExpectingExactReturnValue)
{
    std::vector<tt_cxy_pair> logical_locations =
        {tt_cxy_pair(m_chip_id, 0, 0), tt_cxy_pair(m_chip_id, 0, 5), tt_cxy_pair(m_chip_id, 4, 0)};

    std::vector<tt_cxy_pair> expected_physical_coordinates =
        {tt_cxy_pair(m_chip_id, 1, 1), tt_cxy_pair(m_chip_id, 1, 7), tt_cxy_pair(m_chip_id, 5, 1)};

    compare_logical_and_physical_worker_core_coords_helper(
        m_soc_info.get(), logical_locations, expected_physical_coordinates);
}

TEST_F(Pipegen2_SoCInfo_WH, ConvertLogicalToPhysicalWorkerCoreCoords_ExpectingExactReturnValue)
{
    std::vector<tt_cxy_pair> logical_locations =
        {tt_cxy_pair(m_chip_id, 0, 0), tt_cxy_pair(m_chip_id, 0, 5), tt_cxy_pair(m_chip_id, 4, 0)};

    std::vector<tt_cxy_pair> expected_physical_coordinates =
        {tt_cxy_pair(m_chip_id, 1, 1), tt_cxy_pair(m_chip_id, 1, 7), tt_cxy_pair(m_chip_id, 6, 1)};

    compare_logical_and_physical_worker_core_coords_helper(
        m_soc_info.get(), logical_locations, expected_physical_coordinates);
}

TEST_F(Pipegen2_SoCInfo_BH, ConvertLogicalToPhysicalWorkerCoreCoords_ExpectingExactReturnValue)
{
    std::vector<tt_cxy_pair> logical_locations =
        {tt_cxy_pair(m_chip_id, 0, 0), tt_cxy_pair(m_chip_id, 0, 5), tt_cxy_pair(m_chip_id, 4, 0)};

    std::vector<tt_cxy_pair> expected_physical_coordinates =
        {tt_cxy_pair(m_chip_id, 1, 1), tt_cxy_pair(m_chip_id, 1, 7), tt_cxy_pair(m_chip_id, 6, 1)};

    compare_logical_and_physical_worker_core_coords_helper(
        m_soc_info.get(), logical_locations, expected_physical_coordinates);
}

/**********************************************************************************************************************
    Tests for function: get_dram_buffer_noc_address
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetDramBufferNocAddress_ExpectingExactReturnValue)
{
    std::vector<std::vector<tt_xy_pair>> expected_dram_cores = m_soc_descriptor_file_mock->get_dram_cores();

    // Take any DRAM core, for example the last one.
    std::size_t channel = expected_dram_cores.size() - 1;
    std::size_t subchannel = expected_dram_cores[0].size() - 1;
    tt_xy_pair noc_dram_core = expected_dram_cores.at(channel).at(subchannel);

    // Dummy address.
    const std::uint64_t dram_buf_addr = 0x1234;
    const std::uint64_t expected_noc_address = NOC_XY_ADDR(noc_dram_core.x, noc_dram_core.y, dram_buf_addr);

    EXPECT_EQ(
        m_soc_info->get_dram_buffer_noc_address(dram_buf_addr, m_chip_id, channel, subchannel),
        expected_noc_address);
}

/**********************************************************************************************************************
    Tests for function: get_buffer_noc_address_through_pcie
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetBufferNocAddressThroughPcie_ExpectingExactReturnValue)
{
    tt_xy_pair noc_pcie_core = m_soc_descriptor_file_mock->get_pcie_cores().front();
    // Dummy address.
    const std::uint64_t pcie_buf_addr = 0x1234;
    const std::uint64_t expected_noc_address = NOC_XY_ADDR(noc_pcie_core.x, noc_pcie_core.y, pcie_buf_addr);

    EXPECT_EQ(
        m_soc_info->get_buffer_noc_address_through_pcie(pcie_buf_addr, m_chip_id),
        expected_noc_address);
}

/**********************************************************************************************************************
    Tests for function: get_first_pcie_core_physical_location
    (which also tests get_soc_descriptor)
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetFirstPcieCorePhysicalLocation_ExpectingExactReturnValue)
{
    tt_xy_pair expected_first_pcie_core = m_soc_descriptor_file_mock->get_pcie_cores().front();
    tt_cxy_pair first_pcie_core = m_soc_info->get_first_pcie_core_physical_location(m_chip_id);

    EXPECT_EQ(first_pcie_core, tt_cxy_pair(m_chip_id, expected_first_pcie_core));
}

/**********************************************************************************************************************
    Tests for function: get_host_noc_address_through_pcie
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo_GS, GetHostNocAddressThroughPcie_ExpectingExactReturnValue)
{
    tt_xy_pair expected_first_pcie_core = m_soc_descriptor_file_mock->get_pcie_cores().front();

    compare_host_noc_addresses_helper(m_soc_info.get(), expected_first_pcie_core, m_chip_id);
}

TEST_F(Pipegen2_SoCInfo_WH, GetHostNocAddressThroughPcie_ExpectingExactReturnValue)
{
    tt_xy_pair expected_first_pcie_core = m_soc_descriptor_file_mock->get_pcie_cores().front();

    compare_host_noc_addresses_helper(
        m_soc_info.get(),
        expected_first_pcie_core,
        m_chip_id,
        soc_info_constants::wh_pcie_host_noc_address_offset);
}

TEST_F(Pipegen2_SoCInfo_BH, GetHostNocAddressThroughPcie_ExpectingExactReturnValue)
{
    tt_xy_pair expected_first_pcie_core = m_soc_descriptor_file_mock->get_pcie_cores().front();

    compare_host_noc_addresses_helper(m_soc_info.get(), expected_first_pcie_core, m_chip_id);
}

/**********************************************************************************************************************
    Tests for function: get_local_dram_buffer_noc_address
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetLocalDramBufferNocAddress_ExpectingExactReturnValue)
{
    // Dummy addresses.
    const std::uint64_t expected_local_dram_buf_noc_addr = 0x1234;
    // Bits left from NOC_ADDR_LOCAL_BITS should get zeroed out after the function call.
    const std::uint64_t dram_buf_noc_addr = expected_local_dram_buf_noc_addr + (3ULL << (NOC_ADDR_LOCAL_BITS + 2));

    EXPECT_EQ(
        m_soc_info->get_local_dram_buffer_noc_address(dram_buf_noc_addr),
        expected_local_dram_buf_noc_addr);
}

/**********************************************************************************************************************
    Tests for function: get_local_pcie_buffer_address
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo_GS, GetLocalPcieBufferAddress_ExpectingExactReturnValue)
{
    const tt_cxy_pair worker_core = tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    compare_local_pcie_buffer_addresses_helper(
        m_soc_info.get(),
        worker_core,
        0x80e01234 /* is_pcie_downstream == true */,
        0x40e01234 /* is_pcie_downstream == false */);
}

TEST_F(Pipegen2_SoCInfo_WH, GetLocalPcieBufferAddress_ExpectingExactReturnValue)
{
    const tt_cxy_pair worker_core = tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    compare_local_pcie_buffer_addresses_helper(
        m_soc_info.get(),
        worker_core,
        0x81c01234 /* is_pcie_downstream == true */,
        0x41c01234 /* is_pcie_downstream == false */);
}

TEST_F(Pipegen2_SoCInfo_BH, GetLocalPcieBufferAddress_ExpectingExactReturnValue)
{
    const tt_cxy_pair worker_core = tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    compare_local_pcie_buffer_addresses_helper(
        m_soc_info.get(),
        worker_core,
        0x81c01234 /* is_pcie_downstream == true */,
        0x41c01234 /* is_pcie_downstream == false */);
}

/**********************************************************************************************************************
    Tests for function: get_soc_descriptor
    (through public get_worker_cores_physical_locations)
    NOTE: only throwing an error is tested here, in case of a `SoCInfo` object with no `buda_SocDescriptor` associated
          with a particular chip. Valid behaviour of this function is expected (and tested) through
          `get_..._core_physical_location` public methods.
**********************************************************************************************************************/

TEST_F(Pipegen2_SoCInfo, GetSocDescriptor_AlwaysThrowsExc)
{
    // Use default constructor provided to fetch an empty SoCInfo object with no information about SoC.
    SoCInfoMock soc_info;
    // Expected to throw an error since no soc descriptors have been created when object was instantiated.
    EXPECT_THROW(soc_info.get_worker_cores_physical_locations(m_chip_id), std::runtime_error);
}
