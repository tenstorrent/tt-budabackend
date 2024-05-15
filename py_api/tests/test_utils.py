# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import torch

from eager_backend import DataFormat
from eager_backend.backend_api import PytorchTensorDesc

def py_desc(t: torch.Tensor) -> "PytorchTensorDesc":

    if not t.is_contiguous():
        raise RuntimeError("Pytorch tensor must be contiguous before being sent to TT device, but this one is not")

    if t.dtype == torch.float32:
        format = DataFormat.Float32
    elif t.dtype == torch.bfloat16:
        format = DataFormat.Float16_b
    elif t.dtype == torch.float16:
        format = DataFormat.Float16
    elif t.dtype == torch.int32:
        format = DataFormat.RawUInt32
    else:
        raise RuntimeError("Unsupported torch tensor type for tilization: " + str(t.dtype))

    tilize_ndim = 4
    shape = list(t.shape)
    dim = len(shape)
    if (dim == 2):
        dim = 3
    while len(shape) > tilize_ndim:
        if shape[0] != 1:
            raise RuntimeError("Dropping a dimension that's not 1 to reduce shape to 4D: " + str(t.shape))
        shape = shape[1:]

    while len(shape) < tilize_ndim:
        shape = [1] + shape

    strides = list(t.stride())
    while len(strides) > tilize_ndim:
        strides = strides[1:]

    while len(strides) < tilize_ndim:
        strides = [strides[0]] + strides

    strides = [s * t.element_size() for s in strides]
    desc = PytorchTensorDesc(
        t,
        t.element_size(),
        format,
        dim,
        shape,
        strides,
    )

    return desc

def py_tensor(desc: "PytorchTensorDesc") -> torch.Tensor:
    if desc.format == DataFormat.Float32:
        dtype = torch.float32
    elif desc.format == DataFormat.Float16_b:
        dtype = torch.bfloat16
    elif desc.format == DataFormat.Float16:
        dtype = torch.float16
    else:
        raise RuntimeError(f"Unsupported tensor type({desc.format}) for untilization")

    t = torch.frombuffer(desc, dtype=dtype)
    t = torch.reshape(t, desc.shape)
    
    return t

def block_tensor(tensor, block_dim = 128, ublock_dim = 64, tile_dim = 32, face_dim = 16):
    blocks_r = int(tensor.shape[-2] / block_dim)
    blocks_c = int(tensor.shape[-1] / block_dim)
    ublocks_r = int(block_dim / ublock_dim)
    ublocks_c = int(block_dim / ublock_dim)
    tiles_r = int(ublock_dim / tile_dim)
    tiles_c = int(ublock_dim / tile_dim)
    faces_r = int(tile_dim / face_dim)
    faces_c = int(tile_dim / face_dim)
    blocked_tensor = tensor.reshape(blocks_r,ublocks_r,tiles_r,faces_r,face_dim,blocks_c,ublocks_c,tiles_c,faces_c,face_dim)
    permuted = blocked_tensor.permute(-10,-5,-9,-4,-8,-3,-7,-2,-6,-1)
    flattened = permuted.flatten(start_dim=-10,end_dim=-1)
    back_2d = flattened.reshape(tensor.shape[-2], tensor.shape[-1])
    return back_2d

def unblock_tensor(tensor, block_dim = 128, ublock_dim = 64, tile_dim = 32, face_dim = 16):
    blocks_r = int(tensor.shape[-2] / block_dim)
    blocks_c = int(tensor.shape[-1] / block_dim)
    ublocks_r = int(block_dim / ublock_dim)
    ublocks_c = int(block_dim / ublock_dim)
    tiles_r = int(ublock_dim / tile_dim)
    tiles_c = int(ublock_dim / tile_dim)
    faces_r = int(tile_dim / face_dim)
    faces_c = int(tile_dim / face_dim)
    blocked_tensor = tensor.reshape(blocks_r,blocks_c,ublocks_r,ublocks_c,tiles_r,tiles_c,faces_r,faces_c,face_dim,face_dim)
    permuted = blocked_tensor.permute(-10,-8,-6,-4,-2,-9,-7,-5,-3,-1)
    flattened = permuted.flatten(start_dim=-10,end_dim=-1)
    back_2d = flattened.reshape(tensor.shape[-2], tensor.shape[-1])
    return back_2d
