// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "device/tt_arch_types.h"

#include "device/resource_manager.h"
#include "device/worker_core_resources.h"
#include "mocks/device/soc_info_mocks.h"
#include "model/typedefs.h"

std::unique_ptr<pipegen2::WorkerCoreResources> verify_create_worker_core_resources(tt::ARCH arch);

void verify_allocate_packer_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    tt_cxy_pair core_location,
    pipegen2::StreamId expected_return_value);

void verify_allocate_gather_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager);

void verify_allocate_multicast_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    pipegen2::StreamId expected_return_value);

void verify_allocate_packer_multicast_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    int operand_id,
    pipegen2::StreamId expected_return_value);

void verify_allocate_ethernet_stream(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager);

void verify_allocate_l1_extra_overlay_blob_space(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    tt_cxy_pair core_location,
    unsigned int min_blob_size);

void verify_get_multicast_streams_count(
    pipegen2::ChipId chip_id,
    const pipegen2::SoCDescriptorFileMock* soc_descriptor_file_mock,
    pipegen2::ResourceManager* resource_manager,
    unsigned int expected_multicast_streams_count);
