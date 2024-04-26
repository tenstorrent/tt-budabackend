// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/pipe_graph/pipe_graph.h"

#include "device/tt_xy_pair.h"
#include "model/pipe_graph/pg_buffer.h"

#include <gtest/gtest.h>
#include <memory>

using namespace pipegen2;

/**********************************************************************************************************************
    Tests for function: PipeGraph::add_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, AddBuffer_AddOneBuffer)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(100);
    
    pipe_graph.add_buffer(std::move(pg_buffer));
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);

    const std::unique_ptr<PGBuffer>& pg_buffer_test = pipe_graph.get_buffers()[0];
    EXPECT_EQ(pipe_graph.get_buffers()[0]->get_id(), 100);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::add_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, AddBuffer_AddOnePipe)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGPipe> pg_pipe = std::make_unique<PGPipe>();
    pg_pipe->set_id(100);

    pipe_graph.add_pipe(std::move(pg_pipe));
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);

    EXPECT_EQ(pipe_graph.get_pipes()[0]->get_id(), 100);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::remove_buffers
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, RemoveBuffers_RemoveExistingBuffer)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(100);
    std::unordered_set<PGBuffer*> buffers_to_remove;
    buffers_to_remove.insert(pg_buffer.get());

    pipe_graph.add_buffer(std::move(pg_buffer));
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);
    EXPECT_EQ(pipe_graph.get_buffers()[0]->get_id(), 100);

    pipe_graph.remove_buffers(buffers_to_remove);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 0);
}

TEST(Pipegen2_PipeGraph, RemoveBuffers_RemoveNonExistingBuffer)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(100);
    std::unordered_set<PGBuffer*> buffers_to_remove;
    buffers_to_remove.insert(pg_buffer.get());

    std::unique_ptr<PGBuffer> pg_buffer_not_in_graph = std::make_unique<PGBuffer>();
    buffers_to_remove.insert(pg_buffer_not_in_graph.get());
    std::unique_ptr<PGBuffer> pg_buffer_only_in_graph = std::make_unique<PGBuffer>();
    pipe_graph.add_buffer(std::move(pg_buffer));
    pipe_graph.add_buffer(std::move(pg_buffer_only_in_graph));

    EXPECT_EQ(pipe_graph.get_buffers().size(), 2);
    EXPECT_EQ(pipe_graph.get_buffers()[0]->get_id(), 100);

    pipe_graph.remove_buffers(buffers_to_remove);

    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::remove_pipes
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, RemovePipes_RemoveExistingPipe)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGPipe> pg_pipe = std::make_unique<PGPipe>();
    pg_pipe->set_id(100);
    std::unordered_set<PGPipe*> pipes_to_remove;
    pipes_to_remove.insert(pg_pipe.get());

    pipe_graph.add_pipe(std::move(pg_pipe));
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);
    EXPECT_EQ(pipe_graph.get_pipes()[0]->get_id(), 100);

    pipe_graph.remove_pipes(pipes_to_remove);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 0);
}

TEST(Pipegen2_PipeGraph, RemoveBuffers_RemoveNonExistingPipe)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGPipe> pg_pipe = std::make_unique<PGPipe>();
    pg_pipe->set_id(100);
    std::unordered_set<PGPipe*> pipes_to_remove;
    pipes_to_remove.insert(pg_pipe.get());

    std::unique_ptr<PGPipe> pg_pipe_not_in_graph = std::make_unique<PGPipe>();
    pipes_to_remove.insert(pg_pipe_not_in_graph.get());
    std::unique_ptr<PGPipe> pg_pipe_only_in_graph = std::make_unique<PGPipe>();
    pipe_graph.add_pipe(std::move(pg_pipe));
    pipe_graph.add_pipe(std::move(pg_pipe_only_in_graph));

    EXPECT_EQ(pipe_graph.get_pipes().size(), 2);
    EXPECT_EQ(pipe_graph.get_pipes()[0]->get_id(), 100);

    pipe_graph.remove_pipes(pipes_to_remove);

    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::get_all_node_ids
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, GetAllNodeIds_GetBufferAndPipeId)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGPipe> pg_pipe = std::make_unique<PGPipe>();
    pg_pipe->set_id(100);
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(200);
    pipe_graph.add_pipe(std::move(pg_pipe));
    pipe_graph.add_buffer(std::move(pg_buffer));
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);
    EXPECT_EQ(pipe_graph.get_pipes()[0]->get_id(), 100);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);
    EXPECT_EQ(pipe_graph.get_buffers()[0]->get_id(), 200);

    EXPECT_EQ(pipe_graph.get_all_node_ids().size(), 2);
    EXPECT_EQ(pipe_graph.get_all_node_ids()[0], 200);
    EXPECT_EQ(pipe_graph.get_all_node_ids()[1], 100);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::get_all_chip_ids
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, GetAllChipIds_GetMultipleChips)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(200);
    pg_buffer->set_chip_id(0);
    pipe_graph.add_buffer(std::move(pg_buffer));
    std::unique_ptr<PGPipe> pg_pipe = std::make_unique<PGPipe>();
    pg_pipe->set_id(100);
    pg_pipe->add_mcast_core_logical_location(tt_cxy_pair(1, 0, 0));
    pg_pipe->add_mcast_core_logical_location(tt_cxy_pair(2, 0, 0));
    pipe_graph.add_pipe(std::move(pg_pipe));

    std::vector<ChipId> chip_ids = pipe_graph.get_all_chip_ids();
    EXPECT_EQ(chip_ids.size(), 3);
    EXPECT_EQ(std::count(chip_ids.begin(),chip_ids.end(), 0), 1);
    EXPECT_EQ(std::count(chip_ids.begin(),chip_ids.end(), 1), 1);
    EXPECT_EQ(std::count(chip_ids.begin(),chip_ids.end(), 2), 1);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::get_shared_output_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, GetSharedOutputBuffer_GetOneBuffer)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(100);
    pg_buffer->set_shared_space_buffer_id(200);
    pipe_graph.add_buffer(std::move(pg_buffer));

    EXPECT_EQ(pipe_graph.get_shared_output_buffer(200)->get_id(), 100);
}

TEST(Pipegen2_PipeGraph, GetSharedOutputBuffer_NoSharedBuffers)
{
    PipeGraph pipe_graph;
    std::unique_ptr<PGBuffer> pg_buffer = std::make_unique<PGBuffer>();
    pg_buffer->set_id(100);
    pg_buffer->set_shared_space_buffer_id(200);
    pipe_graph.add_buffer(std::move(pg_buffer));

    EXPECT_EQ(pipe_graph.get_shared_output_buffer(300), nullptr);
}

/**********************************************************************************************************************
    Tests for function: PipeGraph::is_unmapped_location
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraph, IsUnmappedLocation_CheckUnmappedLocation)
{
    tt_cxy_pair unmapped_location(0, 255, 255);
    EXPECT_TRUE(PipeGraph::is_unmapped_location(unmapped_location));
}

TEST(Pipegen2_PipeGraph, IsUnmappedLocation_CheckMappedLocation)
{
    tt_cxy_pair mapped_location(0, 0, 0);
    EXPECT_FALSE(PipeGraph::is_unmapped_location(mapped_location));
}
