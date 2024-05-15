// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "net2pipe.h"
#include "validators.h"
#include "test_unit_common.hpp"

#include "gtest/gtest.h"

TEST(Net2PipeUniqueIdValidator, QueueBuffer_TooManyTilesForScatterOffsetRang_ExpectThrow) {
    assert(wh_soc_desc.get() != nullptr);
    auto const& [q_id, q_info, q_buf_id, q_buffer] = generate_dummy_router_queue(0, 0, *(wh_soc_desc.get()), n2p::UNIQUE_ID_ALIGN + 1);
    ASSERT_THROW(validate::unique_id_range_covers_all_buffers({{q_buf_id, q_buffer}}, {{q_id, q_info}}), std::runtime_error);
    }

TEST(Net2PipeUniqueIdValidator, QueueBuffer_FitsInScatterOffsetRange_ExpectPass) {
    assert(wh_soc_desc.get() != nullptr);
    auto const& [q_id, q_info, q_buf_id, q_buffer] = generate_dummy_router_queue(0, 0, *(wh_soc_desc.get()), n2p::UNIQUE_ID_ALIGN);
    validate::unique_id_range_covers_all_buffers({{q_buf_id, q_buffer}}, {{q_id, q_info}});
}