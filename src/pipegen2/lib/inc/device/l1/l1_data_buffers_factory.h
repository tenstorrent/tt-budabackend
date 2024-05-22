// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "device/tt_xy_pair.h"

namespace pipegen2
{

class TileHeaderL1Buffer;
class StreamL1Buffer;
class NcriscFallbackL1Buffer;
class ExtraOverlayBlobL1Buffer;
class StreamNode;

class L1DataBuffersFactory
{
public:
    // Creates memory allocation object representing allocation of tile header buffer on some core's L1.
    static std::unique_ptr<TileHeaderL1Buffer> create_tile_header_buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_address, const unsigned int tile_size);

    // Creates memory allocation object representing allocation of stream buffer on some core's L1.
    static std::unique_ptr<StreamL1Buffer> create_stream_buffer(
        const StreamNode* stream_node, const unsigned int buffer_address, const unsigned int size_in_bytes);

    // Creates memory allocation object representing allocation of NCRISC fallback buffer on some core's L1.
    static std::unique_ptr<NcriscFallbackL1Buffer> create_ncrisc_fallback_buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_address, const unsigned int size_in_bytes);

    // Creates memory allocation object representing extra overlay blob buffer on some core's L1.
    static std::unique_ptr<ExtraOverlayBlobL1Buffer> create_extra_overlay_blob_buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_address, const unsigned int size_in_bytes);
};

}  // namespace pipegen2