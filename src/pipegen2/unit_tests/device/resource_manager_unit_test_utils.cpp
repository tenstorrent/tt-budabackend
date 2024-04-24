#include "resource_manager_unit_test_utils.h"

#include <gtest/gtest.h>

#include "device/tt_xy_pair.h"
#include "stream_io_map.h"

#include "device/core_resources_constants.h"
#include "device/l1/l1_buffer.h"
#include "device/resource_manager_internal.h"
#include "model/typedefs.h"

std::unique_ptr<pipegen2::WorkerCoreResources> verify_create_worker_core_resources(tt::ARCH arch)
{
    tt_cxy_pair dummy_worker_physical_location = tt_cxy_pair(0, 0, 0);
    tt_cxy_pair dummy_worker_logical_location = tt_cxy_pair(0, 0, 0);

    std::unique_ptr<pipegen2::WorkerCoreResources> worker_core_resources;

    EXPECT_NO_THROW(
        worker_core_resources = pipegen2::resource_manager_internal::create_worker_core_resources(
            arch, dummy_worker_physical_location, dummy_worker_logical_location));

    return worker_core_resources;
}

void verify_allocate_packer_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    tt_cxy_pair core_location,
    pipegen2::StreamId expected_return_value)
{
    EXPECT_EQ(
        resource_manager->allocate_packer_stream(core_location, OPERAND_OUTPUT_START_INDEX),
        expected_return_value);
}

void verify_allocate_gather_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager)
{
    tt_cxy_pair core_location =
        tt_cxy_pair(chip_id, soc_descriptor_file_mock->get_ethernet_cores().front());

    pipegen2::StreamId expected_return_value =
        pipegen2::ethernet_core_resources_constants::gather_multicast_streams_id_range_start;

    EXPECT_EQ(resource_manager->allocate_gather_stream(core_location), expected_return_value);
}

void verify_allocate_multicast_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    pipegen2::StreamId expected_return_value)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(chip_id, soc_descriptor_file_mock->get_worker_cores().front());

    EXPECT_EQ(resource_manager->allocate_multicast_stream(worker_core_location), expected_return_value);
}

void verify_allocate_packer_multicast_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    int operand_id,
    pipegen2::StreamId expected_return_value)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(chip_id, soc_descriptor_file_mock->get_worker_cores().front());

    EXPECT_EQ(
        resource_manager->allocate_packer_multicast_stream(worker_core_location, operand_id),
        expected_return_value);
}

void verify_allocate_ethernet_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager)
{
    tt_cxy_pair eth_core_location =
        tt_cxy_pair(chip_id, soc_descriptor_file_mock->get_ethernet_cores().front());

    pipegen2::StreamId expected_return_value =
        pipegen2::ethernet_core_resources_constants::ethernet_stream_id_range_start;

    EXPECT_EQ(resource_manager->allocate_ethernet_stream(eth_core_location), expected_return_value);
}

void verify_allocate_l1_extra_overlay_blob_space(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    tt_cxy_pair core_location,
    unsigned int min_blob_size)
{
    unsigned int blob_size_in_bytes = 0;

    // Expecting no extra space allocated.
    EXPECT_EQ(
        resource_manager->allocate_l1_extra_overlay_blob_space(core_location, blob_size_in_bytes),
        nullptr);

    // Allocate extra 60KB of blob space.
    unsigned int extra_blob_space = 0xF000;
    blob_size_in_bytes = min_blob_size + extra_blob_space;

    EXPECT_EQ(
        resource_manager->allocate_l1_extra_overlay_blob_space(core_location, blob_size_in_bytes)->get_size(),
        extra_blob_space);
}

void verify_get_multicast_streams_count(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    unsigned int expected_multicast_streams_count)
{
    tt_cxy_pair worker_core_location =
        tt_cxy_pair(chip_id, soc_descriptor_file_mock->get_worker_cores().front());

    EXPECT_EQ(resource_manager->get_multicast_streams_count(worker_core_location), expected_multicast_streams_count);
}
