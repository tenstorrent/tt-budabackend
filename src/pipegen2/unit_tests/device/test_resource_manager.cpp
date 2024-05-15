#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
#include "eth_l1_address_map.h"
#include "l1_address_map.h"
#include "stream_io_map.h"

#include "device/core_resources_constants.h"
#include "device/l1_memory_allocation.h"
#include "device/operand_stream_map.h"
#include "device/resource_manager.h"
#include "device/resource_manager_internal.h"
#include "device/soc_info.h"
#include "device/worker_core_resources.h"
#include "device/worker_core_resources_bh.h"
#include "device/worker_core_resources_gs.h"
#include "device/worker_core_resources_wh.h"
#include "mocks/device/soc_info_mocks.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"
#include "pipegen2_constants.h"
#include "resource_manager_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;

/**********************************************************************************************************************
    Test fixtures used for tests in this file.
**********************************************************************************************************************/

class Pipegen2_ResourceManager : public testing::Test
{
protected:
    virtual void SetUp() override
    {
        create_resource_manager();
    }

    // Creates ResourceManager passing a SoCInfoMock object to its constructor in which all info about SoC project is
    // built for is contained.
    void create_resource_manager()
    {
        m_arch = unit_test_utils::get_build_arch();
        m_soc_descriptor_file_mock = SoCDescriptorFileMock::get_instance(m_arch);
        m_resource_manager = std::make_unique<ResourceManager>(
            std::make_unique<SoCInfoMock>(m_chip_id, m_soc_descriptor_file_mock.get()));
    }

    std::unique_ptr<WorkerCoreResources> verify_create_worker_core_resources() const
    {
        return ::verify_create_worker_core_resources(m_arch);
    }

    void verify_allocate_packer_stream(tt_cxy_pair core_location, StreamId expected_return_value) const
    {
        ::verify_allocate_packer_stream(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get(),
            core_location,
            expected_return_value);
    }

    void verify_allocate_gather_stream() const
    {
        ::verify_allocate_gather_stream(m_chip_id, m_soc_descriptor_file_mock.get(), m_resource_manager.get());
    }

    void verify_allocate_multicast_stream(StreamId expected_return_value) const
    {
        ::verify_allocate_multicast_stream(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get(),
            expected_return_value);
    }

    void verify_allocate_packer_multicast_stream(int operand_id, StreamId expected_return_value) const
    {
        ::verify_allocate_packer_multicast_stream(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get(),
            operand_id,
            expected_return_value);
    }

    void verify_allocate_ethernet_stream() const
    {
        ::verify_allocate_ethernet_stream(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get());
    }

    void verify_allocate_l1_extra_overlay_blob_space(tt_cxy_pair core_location, unsigned int min_blob_size) const
    {
        ::verify_allocate_l1_extra_overlay_blob_space(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get(),
            core_location,
            min_blob_size);
    }

    void verify_get_multicast_streams_count(unsigned int expected_multicast_streams_count) const
    {
        ::verify_get_multicast_streams_count(
            m_chip_id,
            m_soc_descriptor_file_mock.get(),
            m_resource_manager.get(),
            expected_multicast_streams_count);
    }

    // Use only one dummy chip.
    const ChipId m_chip_id = 0;

    // Architecture for which project is built.
    tt::ARCH m_arch;

    // Holds constructed SoCDescriptorFileMock.
    std::unique_ptr<SoCDescriptorFileMock> m_soc_descriptor_file_mock;

    // Hold constructed ResourceManager.
    std::unique_ptr<ResourceManager> m_resource_manager;
};

class Pipegen2_ResourceManager_GS : public Pipegen2_ResourceManager
{
protected:
    void SetUp() override
    {
        if (unit_test_utils::get_build_arch() != tt::ARCH::GRAYSKULL)
        {
            // Test skipped since it is only valid for grayskull arch.
            GTEST_SKIP();
        }

        Pipegen2_ResourceManager::SetUp();
    }
};

class Pipegen2_ResourceManager_WH : public Pipegen2_ResourceManager
{
protected:
    void SetUp() override
    {
        tt::ARCH arch = unit_test_utils::get_build_arch();

        if (arch != tt::ARCH::WORMHOLE_B0 &&
            arch != tt::ARCH::WORMHOLE)
        {
            // Test skipped since it is only valid for wormhole arch.
            GTEST_SKIP();
        }

        Pipegen2_ResourceManager::SetUp();
    }
};

class Pipegen2_ResourceManager_BH : public Pipegen2_ResourceManager
{
protected:
    void SetUp() override
    {
        if (unit_test_utils::get_build_arch() != tt::ARCH::BLACKHOLE)
        {
            // Test skipped since it is only valid for blackhole arch.
            GTEST_SKIP();
        }

        Pipegen2_ResourceManager::SetUp();
    }
};

/**********************************************************************************************************************
    Tests for function: resource_manager_internal::create_worker_core_resources
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, CreateWorkerCoreResources_AlwaysThrowsExc)
{
    tt_cxy_pair dummy_worker_physical_location = tt_cxy_pair(0, 0, 0);
    tt_cxy_pair dummy_worker_logical_location = tt_cxy_pair(0, 0, 0);

    // Expecting errors for unsupported archs.
    EXPECT_THROW(
        resource_manager_internal::create_worker_core_resources(
            tt::ARCH::Invalid, dummy_worker_physical_location, dummy_worker_logical_location),
        std::runtime_error);
}

TEST_F(Pipegen2_ResourceManager_GS, CreateWorkerCoreResources_ConstructsValidObject)
{
    EXPECT_NE(dynamic_cast<WorkerCoreResourcesGS*>(verify_create_worker_core_resources().get()), nullptr);
}

TEST_F(Pipegen2_ResourceManager_WH, CreateWorkerCoreResources_ConstructsValidObject)
{
    EXPECT_NE(dynamic_cast<WorkerCoreResourcesWH*>(verify_create_worker_core_resources().get()), nullptr);
}

TEST_F(Pipegen2_ResourceManager_BH, CreateWorkerCoreResources_ConstructsValidObject)
{
    EXPECT_NE(dynamic_cast<WorkerCoreResourcesBH*>(verify_create_worker_core_resources().get()), nullptr);
}

/**********************************************************************************************************************
    Tests for function: resource_manager_internal::create_ethernet_core_resources
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager_GS, CreateEthernetCoreResources_AlwaysThrowsExc)
{
    tt_cxy_pair dummy_worker_location = tt_cxy_pair(0, 0, 0);

    EXPECT_THROW(
        resource_manager_internal::create_ethernet_core_resources(m_arch, dummy_worker_location),
        std::runtime_error);
}

TEST_F(Pipegen2_ResourceManager_WH, CreateEthernetCoreResources_ConstructsValidObject)
{
    tt_cxy_pair dummy_worker_location = tt_cxy_pair(0, 0, 0);

    EXPECT_NO_THROW(resource_manager_internal::create_ethernet_core_resources(m_arch, dummy_worker_location));
}

TEST_F(Pipegen2_ResourceManager_BH, CreateEthernetCoreResources_ConstructsValidObject)
{
    tt_cxy_pair dummy_worker_location = tt_cxy_pair(0, 0, 0);

    EXPECT_NO_THROW(resource_manager_internal::create_ethernet_core_resources(m_arch, dummy_worker_location));
}

/**********************************************************************************************************************
    Tests for function: get_worker_core_resources
    (through public allocate_unpacker_stream)
    NOTE: only throwing an error is tested here, when there is no core at a particular location. Valid behaviour of this
          function is expected (and tested) through `allocate_...` public methods.
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, GetWorkerCoreResources_AlwaysThrowsExc)
{
    // Try to allocate worker core on dram core location.
    tt_cxy_pair dram_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_dram_cores().front().front());

    EXPECT_THROW(
        m_resource_manager->allocate_unpacker_stream(dram_core_location, OPERAND_INPUT_START_INDEX),
        std::runtime_error);
}

/**********************************************************************************************************************
    Tests for function: get_ethernet_core_resources
    (through public allocate_ethernet_stream)
    NOTE: only throwing an error is tested here, when there is no core at a particular location. Valid behaviour of this
          function is expected (and tested) through `allocate_...` public methods.
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, GetEthernetCoreResources_AlwaysThrowsExc)
{
    // Try to allocate ethernet core on dram core location.
    tt_cxy_pair dram_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_dram_cores().front().front());

    EXPECT_THROW(m_resource_manager->allocate_ethernet_stream(dram_core_location),std::runtime_error);
}

/**********************************************************************************************************************
    Tests for function: get_core_resources
    (through public allocate_gather_stream)
    NOTE: only throwing an error is tested here, when there is no core at a particular location. Valid behaviour of this
          function is expected (and tested) through `allocate_...` public methods.
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, GetCoreResources_AlwaysThrowsExc)
{
    // Try to get core resources for core location which is neither worker nor ethernet.
    tt_cxy_pair dram_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_dram_cores().front().front());

    EXPECT_THROW(m_resource_manager->allocate_gather_stream(dram_core_location), std::runtime_error);
}

/**********************************************************************************************************************
    Tests for function: allocate_packer_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocatePackerStream_ExpectingExactReturnValue)
{
    verify_allocate_packer_stream(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front()),
        OperandStreamMap::get_operand_stream_id(OPERAND_OUTPUT_START_INDEX));
}

TEST_F(Pipegen2_ResourceManager_WH, AllocatePackerStream_ExpectingExactReturnValue)
{
    verify_allocate_packer_stream(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front()),
        ethernet_core_resources_constants::ethernet_stream_id_range_end + 1);
}

TEST_F(Pipegen2_ResourceManager_BH, AllocatePackerStream_ExpectingExactReturnValue)
{
    verify_allocate_packer_stream(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front()),
        ethernet_core_resources_constants::ethernet_stream_id_range_end + 1);
}

/**********************************************************************************************************************
    Tests for function: allocate_unpacker_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateUnpackerStream_ExpectingExactReturnValue)
{
    int operand_id = OPERAND_INPUT_START_INDEX;
    // Test unpacker stream allocation on worker core.
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    StreamId expected_return_value = OperandStreamMap::get_operand_stream_id(operand_id);

    EXPECT_EQ(m_resource_manager->allocate_unpacker_stream(worker_core_location, operand_id), expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: allocate_intermed_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateIntermedStream_ExpectingExactReturnValue)
{
    int operand_id = OPERAND_INTERMEDIATES_START_INDEX;
    // Test intermediate stream allocation on worker core.
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    StreamId expected_return_value = OperandStreamMap::get_operand_stream_id(operand_id);

    EXPECT_EQ(m_resource_manager->allocate_intermed_stream(worker_core_location, operand_id), expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: allocate_gather_stream
    NOTE: this test has two cases just to cover both parts of get_core_resources. If we built for WH or BH, ethernet
          core is also used to test gather stream allocation.
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateGatherStream_ExpectingExactReturnValue)
{
    // Test gather stream allocation on worker core.
    tt_cxy_pair core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    StreamId expected_return_value = worker_core_resources_gs_constants::gather_multicast_streams_id_range_start;

    EXPECT_EQ(m_resource_manager->allocate_gather_stream(core_location), expected_return_value);
}

TEST_F(Pipegen2_ResourceManager_WH, AllocateGatherStream_ExpectingExactReturnValue)
{
    verify_allocate_gather_stream();
}

TEST_F(Pipegen2_ResourceManager_BH, AllocateGatherStream_ExpectingExactReturnValue)
{
    verify_allocate_gather_stream();
}

/**********************************************************************************************************************
    Tests for function: allocate_multicast_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager_GS, AllocateMulticastStream_ExpectingExactReturnValue)
{
    verify_allocate_multicast_stream(worker_core_resources_gs_constants::gather_multicast_streams_id_range_start);
}

TEST_F(Pipegen2_ResourceManager_WH, AllocateMulticastStream_ExpectingExactReturnValue)
{
    verify_allocate_multicast_stream(worker_core_resources_wh_constants::gather_multicast_streams_id_range_start);
}

TEST_F(Pipegen2_ResourceManager_BH, AllocateMulticastStream_ExpectingExactReturnValue)
{
    verify_allocate_multicast_stream(worker_core_resources_bh_constants::gather_multicast_streams_id_range_start);
}

/**********************************************************************************************************************
    Tests for function: allocate_packer_multicast_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager_GS, AllocatePackerMulticastStream_AlwaysThrowsExc)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    int operand_id = worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start;

    // No packer-multicast streams available on GS.
    EXPECT_THROW(
        m_resource_manager->allocate_packer_multicast_stream(worker_core_location, operand_id),
        std::runtime_error);
}

TEST_F(Pipegen2_ResourceManager_WH, AllocatePackerMulticastStream_ExpectingExactReturnValue)
{
    int operand_id = worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start;

    StreamId expected_return_value =
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
        OperandStreamMap::get_output_index(operand_id);

    verify_allocate_packer_multicast_stream(operand_id, expected_return_value);
}

TEST_F(Pipegen2_ResourceManager_BH, AllocatePackerMulticastStream_ExpectingExactReturnValue)
{
    int operand_id = worker_core_resources_bh_constants::packer_multicast_stream_operand_id_range_start;

    StreamId expected_return_value =
        worker_core_resources_bh_constants::gather_multicast_streams_id_range_end -
        OperandStreamMap::get_output_index(operand_id);

    verify_allocate_packer_multicast_stream(operand_id, expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: allocate_general_purpose_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateGeneralPurposeStream_ExpectingExactReturnValue)
{
    // Test general purpose stream allocation on worker core.
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    StreamId expected_return_value = static_cast<StreamId>(END_IO_STREAM + 1);

    EXPECT_EQ(m_resource_manager->allocate_general_purpose_stream(worker_core_location), expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: allocate_ethernet_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager_WH, AllocateEthernetStream_ExpectingExactReturnValue)
{
    verify_allocate_ethernet_stream();
}

TEST_F(Pipegen2_ResourceManager_BH, AllocateEthernetStream_ExpectingExactReturnValue)
{
    verify_allocate_ethernet_stream();
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_extra_tile_headers_space
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateL1ExtraTileHeadersSpace_ExpectingExactReturnValue)
{
    // TODO once logic around tile headers is changed, write this test. See issue tenstorrent/budabackend#2532.
    GTEST_SKIP();
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_extra_overlay_blob_space
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateL1ExtraOverlayBlobSpace_ExpectingExactReturnValue)
{
    verify_allocate_l1_extra_overlay_blob_space(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front()),
        l1_mem::address_map::OVERLAY_BLOB_SIZE);
}

TEST_F(Pipegen2_ResourceManager_WH, AllocateL1ExtraOverlayBlobSpace_ExpectingExactReturnValue)
{
    verify_allocate_l1_extra_overlay_blob_space(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front()),
        eth_l1_mem::address_map::OVERLAY_BLOB_SIZE);
}

TEST_F(Pipegen2_ResourceManager_BH, AllocateL1ExtraOverlayBlobSpace_ExpectingExactReturnValue)
{
    verify_allocate_l1_extra_overlay_blob_space(
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front()),
        eth_l1_mem::address_map::OVERLAY_BLOB_SIZE);
}

/**********************************************************************************************************************
    Tests for function: allocate_core_l1_stream_buffer
    // NOTE: this is just sanity test. Underlying call to CoreResources::allocate_l1_data_buffer and 
    CoreResources::track_stream_buffer_allocation is thorougly tested in UTs for core resources.
    // Also tests CoreResources::get_l1_memory_allocations
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateCoreL1StreamBuffer_ExpectingExactReturnValue)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int l1_data_buffers_space_end_address = l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;
    unsigned int half_of_available_space =
        (l1_data_buffers_space_end_address - l1_data_buffers_space_start_address) / 2;

    // Allocate almost entire space, just leave one byte unallocated.
    unsigned int l1_current_data_buffers_space_address;

    std::unique_ptr<StreamNode> stream_node = std::make_unique<StreamNode>(
        StreamNode(StreamType::Unpacker, worker_core_location, 1));

    EXPECT_EQ(m_resource_manager->get_l1_memory_allocations(worker_core_location).size(), 0);
    EXPECT_NO_THROW(
        l1_current_data_buffers_space_address = m_resource_manager->allocate_core_l1_stream_buffer(
            stream_node.get(), half_of_available_space));

    EXPECT_EQ(l1_current_data_buffers_space_address, l1_data_buffers_space_start_address + (half_of_available_space));
    EXPECT_EQ(m_resource_manager->get_l1_memory_allocations(worker_core_location).size(), 1);
    EXPECT_EQ(m_resource_manager->get_l1_memory_allocations(worker_core_location)[0]->get_size(), half_of_available_space);
}

/**********************************************************************************************************************
    Tests for function: allocate_kernel_input
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateKernelInput_ExpectingExactReturnValue)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    unsigned int expected_return_value = 0;

    EXPECT_EQ(m_resource_manager->allocate_kernel_input(worker_core_location), expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: allocate_kernel_output
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, AllocateKernelOutput_ExpectingExactReturnValue)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());

    unsigned int expected_return_value = 0;

    EXPECT_EQ(m_resource_manager->allocate_kernel_output(worker_core_location), expected_return_value);
}

/**********************************************************************************************************************
    Tests for function: get_soc_info
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, GetSocInfo_ExpectingExactReturnValue)
{
    // Expecting that valid SoCInfo object will be fetched.
    EXPECT_NE(dynamic_cast<const SoCInfo*>(m_resource_manager->get_soc_info()), nullptr);
}

/**********************************************************************************************************************
    Tests for function: is_ethernet_core
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager, IsEthernetCore_ExpectingExactReturnValue)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_worker_cores().front());
    EXPECT_EQ(m_resource_manager->is_ethernet_core(worker_core_location), false);
}

TEST_F(Pipegen2_ResourceManager_WH, IsEthernetCore_ExpectingExactReturnValue)
{
    tt_cxy_pair eth_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front());
    EXPECT_EQ(m_resource_manager->is_ethernet_core(eth_core_location), true);
}

TEST_F(Pipegen2_ResourceManager_BH, IsEthernetCore_ExpectingExactReturnValue)
{
    tt_cxy_pair eth_core_location =
        tt_cxy_pair(m_chip_id, m_soc_descriptor_file_mock->get_ethernet_cores().front());
    EXPECT_EQ(m_resource_manager->is_ethernet_core(eth_core_location), true);
}

/**********************************************************************************************************************
    Tests for function: get_multicast_streams_count
**********************************************************************************************************************/

TEST_F(Pipegen2_ResourceManager_GS, GetMulticastStreamsCount_ExpectingExactReturnValue)
{
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1);

    verify_get_multicast_streams_count(expected_multicast_streams_count);
}

TEST_F(Pipegen2_ResourceManager_WH, GetMulticastStreamsCount_ExpectingExactReturnValue)
{
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_wh_constants::gather_multicast_streams_id_range_start + 1);

    verify_get_multicast_streams_count(expected_multicast_streams_count);
}

TEST_F(Pipegen2_ResourceManager_BH, GetMulticastStreamsCount_ExpectingExactReturnValue)
{
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_bh_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_bh_constants::gather_multicast_streams_id_range_start + 1);

    verify_get_multicast_streams_count(expected_multicast_streams_count);
}
