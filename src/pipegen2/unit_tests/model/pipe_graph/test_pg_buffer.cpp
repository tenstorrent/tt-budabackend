// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "model/pipe_graph/pg_buffer.h"

#include <gtest/gtest.h>

#include "model/pipe_graph/pg_pipe.h"

#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Tests for function: PGBuffer::replace_output_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, ReplaceOutputPipe_ReplaceOnePipe)
{
    PGBuffer pg_buffer;
    PGPipe pg_pipe;
    pg_pipe.set_id(100);
    std::unique_ptr<PGPipe> pg_pipe_replacement = std::make_unique<PGPipe>(PGPipe());
    pg_pipe_replacement->set_id(200);
    pg_buffer.add_output_pipe(&pg_pipe);
    EXPECT_EQ(pg_buffer.get_single_output_pipe()->get_id(), 100);

    pg_buffer.replace_output_pipe(&pg_pipe, pg_pipe_replacement.get());
    EXPECT_EQ(pg_buffer.get_single_output_pipe()->get_id(), 200);
}

TEST(Pipegen2_PGBuffer, ReplaceOutputPipe_ReplaceNonExistingPipe)
{
    PGBuffer pg_buffer;
    PGPipe pg_pipe;
    pg_pipe.set_id(100);
    std::unique_ptr<PGPipe> pg_pipe_replacement = std::make_unique<PGPipe>(PGPipe());
    pg_pipe_replacement->set_id(200);
    verify_log_assert(
        [&]() { pg_buffer.replace_output_pipe(&pg_pipe, pg_pipe_replacement.get()); },
        "^The output pipe with id [0-9]+ does not exist!.*");
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::get_single_output_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, GetSingleOutputPipe_HasOneOuptutPipe)
{
    PGBuffer pg_buffer;
    PGPipe pg_pipe;
    pg_pipe.set_id(100);
    pg_buffer.add_output_pipe(&pg_pipe);
    EXPECT_EQ(pg_buffer.get_single_output_pipe()->get_id(), 100);
}

TEST(Pipegen2_PGBuffer, GetSingleOutputPipe_HasTwoOuptutPipes)
{
    PGBuffer pg_buffer;
    PGPipe pg_pipe;
    pg_pipe.set_id(100);
    PGPipe second_pg_pipe;
    second_pg_pipe.set_id(200);
    pg_buffer.add_output_pipe(&pg_pipe);
    pg_buffer.add_output_pipe(&second_pg_pipe);
    verify_log_assert([&]() { pg_buffer.get_single_output_pipe(); }, "^Buffer does not have only one output pipe!.*");
}

TEST(Pipegen2_PGBuffer, GetSingleOutputPipe_HasZeroOuptutPipes)
{
    PGBuffer pg_buffer;
    verify_log_assert([&]() { pg_buffer.get_single_output_pipe(); }, "^Buffer does not have only one output pipe!.*");
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::is_dram
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, IsDram_IsDramIsFalse)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_io_flag(false);
    EXPECT_FALSE(pg_buffer.is_dram());

    pg_buffer.set_dram_io_flag(true);
    pg_buffer.set_num_queue_slots(0);
    EXPECT_FALSE(pg_buffer.is_dram());

    pg_buffer.set_num_queue_slots(1);
    pg_buffer.set_dram_buf_streaming(true);
    EXPECT_FALSE(pg_buffer.is_dram());
}

TEST(Pipegen2_PGBuffer, IsDram_IsDramIsTrue)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_io_flag(true);
    pg_buffer.set_num_queue_slots(1);
    EXPECT_TRUE(pg_buffer.is_dram());
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::is_dram_prefetch
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, IsDramPrefetch_IsDramPrefetchIsTrue)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_buf_flag(true);
    pg_buffer.set_dram_buf_streaming(false);
    pg_buffer.set_operand_id(23);  // Intermediate operands start at 24
    EXPECT_TRUE(pg_buffer.is_dram_prefetch());
}

TEST(Pipegen2_PGBuffer, IsDramPrefetch_IsDramPrefetchIsFalse)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_buf_flag(false);
    EXPECT_FALSE(pg_buffer.is_dram_prefetch());

    pg_buffer.set_dram_buf_flag(true);
    pg_buffer.set_dram_buf_streaming(true);
    EXPECT_FALSE(pg_buffer.is_dram_prefetch());

    pg_buffer.set_dram_buf_streaming(false);
    pg_buffer.set_operand_id(24);  // Intermediate operands start at 24
    EXPECT_FALSE(pg_buffer.is_dram_prefetch());
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::is_dram_input
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, IsDramInput_IsDramInputIsFalse)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_io_flag(true);
    pg_buffer.set_num_queue_slots(1);
    pg_buffer.set_moves_raw_data(true);
    EXPECT_FALSE(pg_buffer.is_dram_input());
}

TEST(Pipegen2_PGBuffer, IsDramInput_IsDramInputIsTrue)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_io_flag(true);
    pg_buffer.set_num_queue_slots(1);
    pg_buffer.set_moves_raw_data(false);
    EXPECT_TRUE(pg_buffer.is_dram_input());
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::is_dram_prefetch_post_tm
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, IsScatterPrefetchPostTm_IsScatterPrefetchPostTmIsFalse)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_buf_flag(true);
    pg_buffer.set_dram_buf_streaming(false);
    pg_buffer.set_operand_id(23);  // Intermediate operands start at 24
    pg_buffer.set_prefetch_type(PrefetchType::POST_TM);
    EXPECT_TRUE(pg_buffer.is_dram_prefetch_post_tm());
}

TEST(Pipegen2_PGBuffer, IsScatterPrefetchPostTm_IsScatterPrefetchPostTmIsTrue)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_buf_flag(true);
    pg_buffer.set_dram_buf_streaming(false);
    pg_buffer.set_operand_id(23);  // Intermediate operands start at 24
    pg_buffer.set_prefetch_type(PrefetchType::PRE_TM);
    EXPECT_FALSE(pg_buffer.is_dram_prefetch_post_tm());
}

/**********************************************************************************************************************
    Tests for function: PGBuffer::is_located_in_l1
**********************************************************************************************************************/

TEST(Pipegen2_PGBuffer, IsLocatedInL1_IsLocatedInL1IsFalse)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_io_flag(true);
    pg_buffer.set_num_queue_slots(1);
    pg_buffer.set_moves_raw_data(false);
    pg_buffer.set_prefetch_type(PrefetchType::POST_TM);
    EXPECT_FALSE(pg_buffer.is_located_in_l1());
}

TEST(Pipegen2_PGBuffer, IsLocatedInL1_IsLocatedInL1IsTrue)
{
    PGBuffer pg_buffer;
    pg_buffer.set_dram_buf_flag(true);
    pg_buffer.set_dram_buf_streaming(false);
    pg_buffer.set_operand_id(23);  // Intermediate operands start at 24
    EXPECT_TRUE(pg_buffer.is_located_in_l1());
}