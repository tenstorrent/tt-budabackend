// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/pipes/direct/unicast_pipe.h"
#include "model/rational_graph/pipes/fork/parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"

namespace pipegen2
{
class UnicastPipeMock : public UnicastPipe
{
public:
    UnicastPipeMock(NodeId pipe_id) : UnicastPipe(RGPipeProperties(pipe_id), tt_cxy_pair()) {}
};

class ParallelForkPipeMock : public ParallelForkPipe
{
public:
    ParallelForkPipeMock(NodeId pipe_id) : ParallelForkPipe(tt_cxy_pair()) { m_pipe_properties.set_pipe_id(pipe_id); }
};

class SerialForkPipeMock : public SerialForkPipe
{
public:
    SerialForkPipeMock(NodeId pipe_id, unsigned int num_serial_outputs, unsigned int num_serial_padding_outputs) :
        SerialForkPipe(RGPipeProperties(pipe_id), tt_cxy_pair(), num_serial_outputs, num_serial_padding_outputs)
    {
    }
};

// RG pipe class only used to model structure of rational graph. All fields apart from pipe id are irrelevant.
class RGPipeStructuralMock : public RGBasePipe
{
public:
    RGPipeStructuralMock(NodeId pipe_id = -1) :
        RGBasePipe(
            RGPipeType::Union /* pipe_type */,
            DataFlowType::Parallel /* data_flow_type */,
            RGPipeProperties(pipe_id),
            tt_cxy_pair() /* physical_location */)
    {
    }
};
}  // namespace pipegen2