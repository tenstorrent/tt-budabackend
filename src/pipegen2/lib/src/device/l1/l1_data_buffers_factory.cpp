// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/l1/l1_data_buffers_factory.h"

#include <memory>

#include "device/l1/extra_overlay_blob_l1_buffer.h"
#include "device/l1/ncrisc_fallback_l1_buffer.h"
#include "device/l1/stream_l1_buffer.h"
#include "device/l1/tile_header_l1_buffer.h"

namespace pipegen2 {

std::unique_ptr<TileHeaderL1Buffer> L1DataBuffersFactory::create_tile_header_buffer(
    const tt_cxy_pair &core_physical_location, const unsigned int buffer_address, const unsigned int tile_size) {
    return std::make_unique<TileHeaderL1Buffer>(core_physical_location, buffer_address, tile_size);
}

std::unique_ptr<StreamL1Buffer> L1DataBuffersFactory::create_stream_buffer(const StreamNode *stream_node,
                                                                           const unsigned int buffer_address,
                                                                           const unsigned int size_in_bytes) {
    return std::make_unique<StreamL1Buffer>(stream_node, buffer_address, size_in_bytes);
}

std::unique_ptr<NcriscFallbackL1Buffer> L1DataBuffersFactory::create_ncrisc_fallback_buffer(
    const tt_cxy_pair &core_physical_location, const unsigned int buffer_address, const unsigned int size_in_bytes) {
    return std::make_unique<NcriscFallbackL1Buffer>(core_physical_location, buffer_address, size_in_bytes);
}

std::unique_ptr<ExtraOverlayBlobL1Buffer> L1DataBuffersFactory::create_extra_overlay_blob_buffer(
    const tt_cxy_pair &core_physical_location, const unsigned int buffer_address, const unsigned int size_in_bytes) {
    return std::make_unique<ExtraOverlayBlobL1Buffer>(core_physical_location, buffer_address, size_in_bytes);
}

}  // namespace pipegen2