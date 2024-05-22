// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "model/pipe_graph/pg_pipe.h"

#include <gtest/gtest.h>

#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Tests for function: PGPipe::get_inputs
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetInputs_GetInputsOfNonScatterIndex)
{
    PGPipe pg_pipe;
    PGBuffer pg_buffer1;
    PGBuffer pg_buffer2;
    pg_buffer1.set_id(100);
    pg_buffer2.set_id(200);
    pg_pipe.add_input_buffer(&pg_buffer1);
    pg_pipe.add_input_buffer(&pg_buffer2);
    const std::vector<PGPipe::Input>& inputs = pg_pipe.get_inputs(0);
    EXPECT_EQ(inputs.size(), 2);
    EXPECT_EQ(inputs[0].get_buffer()->get_id(), 100);
    EXPECT_EQ(inputs[1].get_buffer()->get_id(), 200);
}

TEST(Pipegen2_PGPipe, GetInputs_GetInputsOfScatterIndex)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer;
    PGBuffer input_buffer;
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer.set_scatter_gather_num_tiles(12);
    input_buffer.set_id(100);
    output_padding_buffer.set_id(200);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer, 1);
    const std::vector<PGPipe::Input>& inputs = pg_pipe.get_inputs(1);
    EXPECT_EQ(inputs.size(), 1);
    EXPECT_EQ(inputs[0].get_buffer()->get_id(), 200);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::remove_all_inputs
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, RemoveAllInputs_RemoveExistingInputs)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_id(100);
    pg_pipe.add_input_buffer(&input_buffer);
    EXPECT_EQ(pg_pipe.get_inputs().size(), 1);
    pg_pipe.remove_all_inputs();
    EXPECT_EQ(pg_pipe.get_inputs().size(), 0);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::is_output_padding
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, IsOutputPadding_IsOutputPaddingIsFalse)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_id(100);
    pg_pipe.add_input_buffer(&input_buffer);
    EXPECT_FALSE(pg_pipe.is_output_padding(0));
}

TEST(Pipegen2_PGPipe, IsOutputPadding_IsOutputPaddingIsTrue)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer;
    PGBuffer input_buffer;
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer.set_scatter_gather_num_tiles(12);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer, 1);
    EXPECT_TRUE(pg_pipe.is_output_padding(1));
}

/**********************************************************************************************************************
    Tests for function: PGPipe::get_unique_input_buffers
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetUniqueInputBuffers_MoreThanOneOfTheSameBuffer)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_id(100);
    pg_pipe.add_input_buffer(&input_buffer, 0);
    pg_pipe.add_input_buffer(&input_buffer, 64);
    const std::vector<const PGBuffer*>& unique_input_buffers = pg_pipe.get_unique_input_buffers();
    EXPECT_EQ(unique_input_buffers.size(), 1);
    EXPECT_EQ(unique_input_buffers[0]->get_id(), 100);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::get_single_output_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetSingleOutputBuffer_HasOuputBuffer)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer;
    output_buffer.set_id(100);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_EQ(pg_pipe.get_single_output_buffer()->get_id(), 100);
}

TEST(Pipegen2_PGPipe, GetSingleOutputBuffer_HasNoOuputBuffers)
{
    PGPipe pg_pipe;
    verify_log_assert([&]() { pg_pipe.get_single_output_buffer(); }, "^Pipe does not have only one output buffer!.*");
}

TEST(Pipegen2_PGPipe, GetSingleOutputBuffer_HasMultipleOutputBuffers)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer1;
    PGBuffer output_buffer2;
    pg_pipe.add_output_buffer(&output_buffer1, 0);
    pg_pipe.add_output_buffer(&output_buffer2, 1);
    verify_log_assert([&]() { pg_pipe.get_single_output_buffer(); }, "^Pipe does not have only one output buffer!.*");
}

/**********************************************************************************************************************
    Tests for function: PGPipe::get_unique_output_padding_buffers
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetUniqueOutputPaddingBuffers_HasMultipleOutputBuffers)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer1;
    PGBuffer input_buffer;
    output_padding_buffer1.set_id(200);
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer1.set_scatter_gather_num_tiles(12);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer1, 1);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer1, 2);
    EXPECT_EQ(pg_pipe.get_unique_output_padding_buffers().size(), 1);
    EXPECT_EQ(pg_pipe.get_unique_output_padding_buffers()[0]->get_id(), 200);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::get_unique_input_buffers_including_output_padding
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetUniqueInputBuffersIncludingOutputPadding_HasMultipleBuffers)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer1;
    PGBuffer input_buffer;
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer1.set_scatter_gather_num_tiles(12);
    input_buffer.set_id(100);
    output_padding_buffer1.set_id(200);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer1, 2);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer1, 3);
    EXPECT_EQ(pg_pipe.get_unique_input_buffers_including_output_padding().size(), 2);
    EXPECT_EQ(pg_pipe.get_unique_input_buffers_including_output_padding()[0]->get_id(), 100);
    EXPECT_EQ(pg_pipe.get_unique_input_buffers_including_output_padding()[1]->get_id(), 200);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::has_output_list_duplicates
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, HasOutputListDuplicates_HasOutputListDuplicates)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer;
    pg_pipe.set_id(100);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    pg_pipe.add_output_buffer(&output_buffer, 1);
    pg_pipe.add_output_buffer_id(pg_pipe.get_id());
    pg_pipe.add_output_buffer_id(pg_pipe.get_id());
    EXPECT_TRUE(pg_pipe.has_output_list_duplicates());
}

TEST(Pipegen2_PGPipe, HasOutputListDuplicates_HasNoOutputListDuplicates)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer;
    output_buffer.set_id(100);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    pg_pipe.add_output_buffer_id(output_buffer.get_id());
    EXPECT_FALSE(pg_pipe.has_output_list_duplicates());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::add_output_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, AddOutputBuffer_AddOneOutputBuffer)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer;
    output_buffer.set_id(100);
    pg_pipe.add_output_buffer(&output_buffer, 3);
    EXPECT_EQ(pg_pipe.get_output_buffers().size(), 4);
    EXPECT_EQ(pg_pipe.get_output_buffers()[3][0]->get_id(), 100);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::add_output_padding_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, AddOutputPaddingBuffer_AddOneOutputPaddingBuffer)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer;
    PGBuffer input_buffer;
    output_padding_buffer.set_id(200);
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer.set_scatter_gather_num_tiles(12);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_padding_buffer(&output_padding_buffer, 2);
    EXPECT_EQ(pg_pipe.get_unique_output_padding_buffers().size(), 1);
}

TEST(Pipegen2_PGPipe, AddOutputPaddingBuffer_WrongScatterGatherNumTiles)
{
    PGPipe pg_pipe;
    PGBuffer output_padding_buffer;
    PGBuffer input_buffer;
    output_padding_buffer.set_id(200);
    input_buffer.set_scatter_gather_num_tiles(12);
    output_padding_buffer.set_scatter_gather_num_tiles(11);
    pg_pipe.add_input_buffer(&input_buffer);
    verify_log_assert(
        [&]() { pg_pipe.add_output_padding_buffer(&output_padding_buffer, 2); },
        "^Expecting output padding buffer to have scatter_gather_num_tiles which divides total number of"
        "messages pipe transfers per scatter index.*");
}

/**********************************************************************************************************************
    Tests for function: PGPipe::replace_output_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, ReplaceOutputBuffer_ReplaceOneBuffer)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer1;
    PGBuffer output_buffer2;
    output_buffer1.set_id(100);
    output_buffer2.set_id(200);
    pg_pipe.add_output_buffer(&output_buffer1, 0);
    EXPECT_EQ(pg_pipe.get_output_buffers().size(), 1);
    EXPECT_EQ(pg_pipe.get_output_buffers()[0][0]->get_id(), 100);
    pg_pipe.replace_output_buffer(&output_buffer1, &output_buffer2);
    EXPECT_EQ(pg_pipe.get_output_buffers().size(), 1);
    EXPECT_EQ(pg_pipe.get_output_buffers()[0][0]->get_id(), 200);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::get_num_msgs_per_scatter_index
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, GetNumMsgsPerScatterIndex_GetNumMsgsHasInputs)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_scatter_gather_num_tiles(4);
    input_buffer2.set_scatter_gather_num_tiles(8);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_input_buffer(&input_buffer2);
    EXPECT_EQ(pg_pipe.get_num_msgs_per_scatter_index(), 12);
}

TEST(Pipegen2_PGPipe, GetNumMsgsPerScatterIndex_GetNumMsgsHasNoInputs)
{
    PGPipe pg_pipe;
    EXPECT_EQ(pg_pipe.get_num_msgs_per_scatter_index(), 0);
}

/**********************************************************************************************************************
    Tests for function: PGPipe::is_connecting_l1_buffers
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, IsConnectingL1Buffers_IsConnectingL1Buffers)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    PGBuffer output_buffer;
    input_buffer.set_dram_buf_flag(true);
    input_buffer.set_dram_buf_streaming(false);
    input_buffer.set_operand_id(22);
    output_buffer.set_dram_buf_flag(true);
    output_buffer.set_dram_buf_streaming(false);
    output_buffer.set_operand_id(23);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_TRUE(pg_pipe.is_connecting_l1_buffers());
}

TEST(Pipegen2_PGPipe, IsConnectingL1Buffers_IsNotConnectingL1Buffers)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    PGBuffer output_buffer;
    input_buffer.set_dram_io_flag(true);
    input_buffer.set_num_queue_slots(1);
    input_buffer.set_moves_raw_data(false);
    input_buffer.set_prefetch_type(PrefetchType::POST_TM);
    output_buffer.set_dram_buf_flag(true);
    output_buffer.set_dram_buf_streaming(false);
    output_buffer.set_operand_id(23);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_FALSE(pg_pipe.is_connecting_l1_buffers());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::has_non_prefetch_pre_tm_dram_input
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, HasNonPrefetchPreTmDramInput_HasNonPrefetchPreTmDramInput)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_dram_buf_flag(true);
    input_buffer1.set_dram_buf_streaming(false);
    input_buffer1.set_operand_id(22);
    input_buffer1.set_prefetch_type(PrefetchType::POST_TM);
    input_buffer2.set_dram_buf_flag(true);
    input_buffer2.set_dram_buf_streaming(false);
    input_buffer2.set_operand_id(23);
    input_buffer2.set_prefetch_type(PrefetchType::POST_TM);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_input_buffer(&input_buffer2);
    EXPECT_TRUE(pg_pipe.has_non_prefetch_pre_tm_dram_input());
}

TEST(Pipegen2_PGPipe, HasNonPrefetchPreTmDramInput_DoesNotHaveNonPrefetchPreTmDramInput)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_dram_buf_flag(true);
    input_buffer1.set_dram_buf_streaming(false);
    input_buffer1.set_operand_id(22);
    input_buffer1.set_prefetch_type(PrefetchType::POST_TM);
    input_buffer2.set_dram_buf_flag(true);
    input_buffer2.set_dram_buf_streaming(false);
    input_buffer2.set_operand_id(23);
    input_buffer2.set_prefetch_type(PrefetchType::PRE_TM);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_input_buffer(&input_buffer2);
    EXPECT_FALSE(pg_pipe.has_non_prefetch_pre_tm_dram_input());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::is_direct_intermediate_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, IsDirectIntermediatePipe_IsDirectIntermediatePipe)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_operand_id(24);
    PGBuffer output_buffer;
    output_buffer.set_operand_id(25);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_TRUE(pg_pipe.is_direct_intermediate_pipe());
}

TEST(Pipegen2_PGPipe, IsDirectIntermediatePipe_IsNotDirectIntermediatePipe)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_operand_id(24);
    PGBuffer output_buffer;
    output_buffer.set_operand_id(23);
    pg_pipe.add_input_buffer(&input_buffer);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_FALSE(pg_pipe.is_direct_intermediate_pipe());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::is_join_intermediate_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, IsJoinIntermediatePipe_IsJoinIntermediatePipe)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_operand_id(24);
    input_buffer2.set_operand_id(25);
    PGBuffer output_buffer;
    output_buffer.set_operand_id(26);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_input_buffer(&input_buffer2);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_TRUE(pg_pipe.is_join_intermediate_pipe());
}

TEST(Pipegen2_PGPipe, IsJoinIntermediatePipe_PipeWithSingleInput)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    input_buffer1.set_operand_id(24);
    PGBuffer output_buffer;
    output_buffer.set_operand_id(26);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_FALSE(pg_pipe.is_join_intermediate_pipe());
}

TEST(Pipegen2_PGPipe, IsJoinIntermediatePipe_IsNotJoinIntermediatePipe)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_operand_id(24);
    input_buffer2.set_operand_id(25);
    PGBuffer output_buffer;
    output_buffer.set_operand_id(23);
    pg_pipe.add_input_buffer(&input_buffer1);
    pg_pipe.add_input_buffer(&input_buffer2);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    EXPECT_FALSE(pg_pipe.is_join_intermediate_pipe());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::is_dram_prefetch_post_tm
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, IsScatterPrefetchPostTm_IsScatterPrefetchPostTm)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_dram_buf_flag(true);
    input_buffer.set_dram_buf_streaming(false);
    input_buffer.set_operand_id(23);
    input_buffer.set_prefetch_type(PrefetchType::POST_TM);
    pg_pipe.add_input_buffer(&input_buffer);
    EXPECT_TRUE(pg_pipe.is_dram_prefetch_post_tm());
}

TEST(Pipegen2_PGPipe, IsScatterPrefetchPostTm_IsNotScatterPrefetchPostTm)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_dram_buf_flag(false);
    pg_pipe.add_input_buffer(&input_buffer);
    EXPECT_FALSE(pg_pipe.is_dram_prefetch_post_tm());
}

TEST(Pipegen2_PGPipe, IsScatterPrefetchPostTm_IsScatterPrefetchPreTm)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_dram_buf_flag(true);
    input_buffer.set_dram_buf_streaming(false);
    input_buffer.set_operand_id(23);
    input_buffer.set_prefetch_type(PrefetchType::PRE_TM);
    pg_pipe.add_input_buffer(&input_buffer);
    EXPECT_FALSE(pg_pipe.is_dram_prefetch_post_tm());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::has_consumer_duplicates
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, HasConsumerDuplicates_ConsumerRepeatGreaterThanOne)
{
    PGPipe pg_pipe;
    pg_pipe.set_consumer_repeat(2);
    EXPECT_TRUE(pg_pipe.has_consumer_duplicates());
}

TEST(Pipegen2_PGPipe, HasConsumerDuplicates_HasOutputListDuplicates)
{
    PGPipe pg_pipe;
    PGBuffer output_buffer;
    output_buffer.set_id(100);
    pg_pipe.add_output_buffer(&output_buffer, 0);
    pg_pipe.add_output_buffer(&output_buffer, 1);
    pg_pipe.add_output_buffer_id(pg_pipe.get_id());
    pg_pipe.add_output_buffer_id(pg_pipe.get_id());
    EXPECT_TRUE(pg_pipe.has_consumer_duplicates());
}

/**********************************************************************************************************************
    Tests for function: PGPipe::has_single_buffer_input
**********************************************************************************************************************/

TEST(Pipegen2_PGPipe, HasSingleBufferInput_HasSingleBufferInputIsTrue)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer;
    input_buffer.set_id(100);
    pg_pipe.add_input_buffer(&input_buffer, 0);
    pg_pipe.add_input_buffer(&input_buffer, 64);
    EXPECT_TRUE(pg_pipe.has_single_buffer_input());
}

TEST(Pipegen2_PGPipe, HasSingleBufferInput_HasSingleBufferInputIsFalse)
{
    PGPipe pg_pipe;
    PGBuffer input_buffer1;
    PGBuffer input_buffer2;
    input_buffer1.set_id(100);
    input_buffer2.set_id(200);
    pg_pipe.add_input_buffer(&input_buffer1, 0);
    pg_pipe.add_input_buffer(&input_buffer2, 64);
    EXPECT_FALSE(pg_pipe.has_single_buffer_input());
}
