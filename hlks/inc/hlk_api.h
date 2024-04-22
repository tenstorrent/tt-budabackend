// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

enum class Dim : std::uint8_t
 {
    None      = 0,
    R         = 1,
    C         = 2,
    Z         = 3,
    RC        = 4,
    ZR        = 5,
    RC_custom = 6,
    Invalid   = 0xFF,
};

struct hlk_relu_config_t {
    std::uint32_t
        ApplyRelu : 16;  // 0 ? no relu, 1 ? val<0=>val=0, 2 ? val<threshold=>val=0, 3 - val>threshold=>val=threshold
    std::uint32_t Threshold : 16;  // fp16
};

union hlk_relu_config_u {
    hlk_relu_config_t f;
    std::uint32_t val;
};

enum class ReluMode : std::uint8_t
{
    None      = 0,
    Min       = 1,
    Max       = 2,
    Invalid   = 0xFF,
};

enum class SfpuExecutionThread : std::uint8_t
{
    Unpack      = 0, // currently unused
    Math        = 1,
    Pack        = 2,

    Invalid     = 3,
    Default     = Math,
};

enum class SfpuOp : std::uint8_t
{
    Exp,
    Log,
    Sqrt,
    Gelu,
    GeluDerivative,
    Reciprocal,
    Sigmoid,
    Dropout,
    Tanh,
    Square,
    Power,
    Sine, 
    Cosine,
    ReluMax, // Not specifyable in a netlist 
    ReluMin, // Not specifyable in a netlist
    LRelu,
    Abs,
    Max, // Not specifyable in a netlist
    CastFp32ToFp16a, // Not specifyable in a netlist
    TopK_lsort, // Not specifyable in a netlist
    TopK_merge, // Not specifyable in a netlist
    TopK_rebuild, // Not specifyable in a netlist
    Invalid,
};

enum class BinaryOp : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    // Quantization converts Float32 inputs to Int8 outputs.
    // Takes first input Float32 (inputs) and second input Float32 (scalars).
    // output[Int8 | Int32] = (inputs[Float32] * scalars[Float32]) + zero_point[Float32])
    Quantization,
    // Dequantization converts Int32 inputs to Float32 outputs.
    // Takes first input Int32 (inputs) and second input Float32 (scalars).
    // output[Float32] = (inputs[Int32] - zero_point[Float32]) * scalars[Float32]
    Dequantization,
    // Requantization converts Int32 inputs to Int8 outputs.
    // Takes first input Int32 (inputs) and second input Float32 (scalars).
    // output[Int8 | Int32] = (inputs[Int32] * scalars[Float32]) + zero_point[Float32])
    Requantization,
    Maximum,
    Invalid,
};

enum class NaryOp : std::uint8_t
{
    Splice,
    Invalid,
};

//! UnaryOp -- Different from SfpuOp (elementwise Unary), this can shuffle data around, but single input
enum class UnaryOp : std::uint8_t
{
    Datacopy, // Passthrough
    Invalid,
};

enum class DrainerOp : std::uint8_t
{
    Drainer,
    Invalid,
};

enum class EmbeddingOp : std::uint8_t
{
    Embedding, // Encoded indices cause GW to pull from dram directly
    Invalid,
};

enum class TilizerOp : std::uint8_t
{
    Tilizer,
    Invalid,
};

enum class DepthwiseOp : std::uint8_t {
    Matmul,
    Invalid,
};

enum class TopKOp : std::uint8_t {
    TopK,
    Invalid,
};

enum class SpliceMode : std::uint8_t {
  Ublock  = 0, 
  T       = 1,
  Invalid = 0xFF,
};

enum class TmOp : std::uint8_t 
{
    rBroadcast,
    cBroadcast,
    zBroadcast,
    hSlice,
    hStack,
    vSlice,
    vStack,
    Transpose,
    TileBroadcast,
    Pad,
    Invalid,
};

enum class ReduceFunc : std::uint8_t
{
    Sum,
    Avg,  // Needed only on tensor level to compute correct coefficient. Kernel uses Sum.
    Max,
    Invalid,
};

enum DstMode : std::uint8_t
{
    Full          = 0,
    Half          = 1,
    Tile          = 2,
    NUM_DST_MODES = 3,
};

enum DstSize : std::uint8_t
{
    FullSize     = 16,
    HalfSize     = 8,
    TileSize     = 1,
};

enum class Action {
    None,
    Slice,
    Stack,
};

enum class BinaryOpDstReuse : std::uint8_t {
    DstToSrcA = 0,
    DstToSrcB = 1,
};

enum HlkOperand : std::uint8_t {
    in0 = 0,
    in1 = 1,
    in2 = 2,
    in3 = 3,
    in4 = 4,
    in5 = 5,
    in6 = 6,
    in7 = 7,

    param0 = 8,
    param1 = 9,
    param2 = 10,
    param3 = 11,
    param4 = 12,
    param5 = 13,
    param6 = 14,
    param7 = 15,

    out0 = 16,
    out1 = 17,
    out2 = 18,
    out3 = 19,
    out4 = 20,
    out5 = 21,
    out6 = 22,
    out7 = 23,

    intermed0 = 24,
    intermed1 = 25,
    intermed2 = 26,
    intermed3 = 27,
    intermed4 = 28,
    intermed5 = 29,
    intermed6 = 30,
    intermed7 = 31,
};

/*
Stochastic rounding modes:
    None: No stochastic rounding enabled, default rounding is round to nearest even.
    Fpu: Enables stochastic rounding for every accumulation in the fpu
    Pack: Enables stochastic rounding in both gasket and packer. Gasket rounding is in 
    data format conversion stage from dest format to pack_src_format. Packer rounding
    is in data format conversion stage from pack_src_format to pack_dst_format.
    All: Enables fpu, pack and gasket rounding. 
*/
enum StochRndMode : std::uint8_t
{
    None    = 0,
    Fpu     = 1,
    Pack    = 2,
    All     = 0xf,
    Invalid = 0xff,
};

struct TileDimReconfigMode
{
    bool unpack_reconfig_en  : 1;
    bool pack_reconfig_en    : 1;
};


constexpr std::uint32_t NUM_MAX_IN_BUFFERS_PER_CORE = HlkOperand::in7 - HlkOperand::in0 + 1;
constexpr std::uint32_t NUM_MAX_PARAM_BUFFERS_PER_CORE = HlkOperand::param7 - HlkOperand::param0 + 1;
constexpr std::uint32_t NUM_MAX_OUT_BUFFERS_PER_CORE = HlkOperand::out7 - HlkOperand::out0 + 1;
constexpr std::uint32_t NUM_MAX_INTERMED_BUFFERS_PER_CORE = HlkOperand::intermed7 - HlkOperand::intermed0 + 1;
constexpr std::uint32_t NUM_MAX_BUFFERS = HlkOperand::intermed7 - HlkOperand::in0 + 1;
constexpr std::uint32_t DEFAULT_TILE_DIMS[2] = {32, 32};
class tt_core;

// This will be redefined in chlkc_list.h
#ifndef TT_HLK_ALWAYS_INLINE
    #define TT_HLK_ALWAYS_INLINE
#endif

TT_HLK_ALWAYS_INLINE void hlk_acquire_dst(tt_core* core_ptr);
TT_HLK_ALWAYS_INLINE void hlk_release_dst(tt_core* core_ptr);

// Define TT_LOG and it's derivatives so the compile passes.
// If we are running workloads on hardware, TT_LOG will already have been defined. 
#ifndef TT_LOG_DEFINED
    #define TT_LOG_DEFINED
    #define TT_LOG(...) (void)sizeof(__VA_ARGS__)
    #define TT_LOG_NB(...) (void)sizeof(__VA_ARGS__)
    #define TT_PAUSE(...) (void)sizeof(__VA_ARGS__)
    #define TT_RISC_ASSERT(...) (void)sizeof(__VA_ARGS__)
    #define TT_LLK_DUMP(...) (void)sizeof(__VA_ARGS__)
    #define TT_DUMP_LOG(...) (void)sizeof(__VA_ARGS__)
    #define TT_DUMP_ASSERT(...) (void)sizeof(__VA_ARGS__)
#endif

// unpack
TT_HLK_ALWAYS_INLINE void hlk_wait_tiles(tt_core* core_ptr, int stream, int num_tiles);
template<int pop_blocks = 0>
TT_HLK_ALWAYS_INLINE void hlk_pop_tiles(tt_core* core_ptr, int stream, int num_tiles);

// pack
TT_HLK_ALWAYS_INLINE void hlk_wait_for_free_tiles(tt_core* core_ptr, int stream, int num_tiles);
TT_HLK_ALWAYS_INLINE void hlk_push_tiles(tt_core* core_ptr, int stream, int num_tiles);
TT_HLK_ALWAYS_INLINE void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream);
TT_HLK_ALWAYS_INLINE void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream, int tile_index);

TT_HLK_ALWAYS_INLINE void hlk_get_tile(tt_core* core_ptr, int stream, int index, volatile void* p_tile);
TT_HLK_ALWAYS_INLINE void hlk_release_tile(tt_core* core_ptr, int stream);

// Debug dumps
TT_HLK_ALWAYS_INLINE void hlk_debug_dump(tt_core* core_ptr, unsigned char *data, int size);
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_seek(tt_core* core_ptr, int offset);

TT_HLK_ALWAYS_INLINE void hlk_debug_thread_halt(tt_core* core_ptr);
TT_HLK_ALWAYS_INLINE void hlk_debug_thread_unhalt(tt_core* core_ptr);

// For the following debug function, the procedure is as following.
//  - Before running a test, set ENABLE_TT_LLK_DUMP=1 env variable
//  - Before calling a specific debug dump function, call hlk_debug_thread_halt()
//  - After calling the desired debug dump function, call hlk_debug_thread_unhalt()

// Dumps the contents of the dest register, specified with row_addr
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_dest_acc_row(tt_core* core_ptr, int row_addr);

// Dumps a row from srcA register. Usually, the only dumpable values are datums from the last face of the tile (except
// on WH_B0 matmul where unpacking a whole tile to src reg is implemented). Those datums are specified with row_addr.
// In case of full tile dumping, row_addr indices go from 0 to 63. 0-15 represents face0, 16-31 face1, 32-47 face2, 48-63 face3.
// In case of dumping last face, row_addr indices go from 0 to 15 and represent face3.
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_srca_row(tt_core* core_ptr, int row_addr);

// Dumps a row from srcB register. Usually, the only dumpable values are datums from the last face of the tile (except
// on WH_B0 matmul where unpacking a whole tile to src reg is implemented). Those datums are specified with row_addr.
// In case of full tile dumping, row_addr indices go from 0 to 63. 0-15 represents face0, 16-31 face1, 32-47 face2, 48-63 face3.
// In case of dumping last face, row_addr indices go from 0 to 15 and represent face3.
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_srcb_row(tt_core* core_ptr, int row_addr);

// Dumps unpack hw config registers..
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_hw_config_regs(tt_core* core_ptr);

// Dumps unpack, math and pack thread config registers.
TT_HLK_ALWAYS_INLINE void hlk_debug_dump_thread_config_regs(tt_core* core_ptr);

TT_HLK_ALWAYS_INLINE void hlk_flush_tiles(tt_core* core_ptr, int stream, int num_tiles);

// math
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst(
    tt_core* core_ptr, int stream, int index, int dstindex, int transpose_of_faces);

// Wormhole_b0 specific API
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly(
    tt_core* core_ptr, int stream, int index, int dstindex, int transpose_of_faces);

TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile(tt_core* core_ptr, int stream, int index, int dstindex, int dim, int phase);
template <int lstream_kernel_broadcast=0, int rstream_kernel_broadcast=0>
TT_HLK_ALWAYS_INLINE void hlk_mm_tile(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim);
template <int lstream_kernel_broadcast=0, int rstream_kernel_broadcast=0>
TT_HLK_ALWAYS_INLINE void hlk_mm_block(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim);

// binary sfpu
TT_HLK_ALWAYS_INLINE void hlk_sfpu_quant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_requant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_dequant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_add_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_local_sort(tt_core* core_ptr, int stream, int dst_index, int dir, int end_phase, int start_phase, int end_step, int start_step);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_merge(tt_core* core_ptr, int stream, int dst_index, int m, int k);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_rebuild(tt_core* core_ptr, int stream, int dst_index, int dir, int m, int k, int logk, bool skip_second_tile);

// sfpu_specific_param_0 is unused except in these cases:
// 1. dropout - represents probability
// 2. power - represents exponent
// 3. lrelu - represents slope
// 4. relu_min/relu_max - represents min/max
// sfpu_specific_param_1 is unused except in these cases:
// 1. dropout - represents scale
template <SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_unary_sfpu_op(
    tt_core* core_ptr, int stream, int dst_index, int dim, unsigned int sfpu_specific_param_0 = 0, unsigned int sfpu_specific_param_1 = 0, unsigned int param_2 = 0, unsigned int param_3 = 0, unsigned int param_4 = 0, unsigned int param_5 = 0);

// acc_to_dest parameter represents accumulation in dest register. It is always turned on in case of multiply operation, and the parameter has no effect there.
// Addition and subtraction turn it on/off depending on the passed in value.
template<BinaryOp op_type, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_init(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest);
template<BinaryOp op_type, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_init_short(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest);
template<BinaryOp op_type, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dst_index, int transpose);

template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest_init(tt_core* core_ptr);
template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest_init_short(tt_core* core_ptr);
template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest(tt_core* core, int rstream, int rindex, int dst_index, int clear_fp_32_dst_acc = (int)true);

TT_HLK_ALWAYS_INLINE void hlk_tilize(tt_core* core_ptr, int lstream, int lindex, int dstindex, int ct_dim);
template<ReduceFunc reduce_func, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile(tt_core* core_ptr, int lstream, int lindex, int dstindex, float coefficient);

// HW configs
// In each high level kernel, before tile processing, you need to call one of HW configs, which corresponds on the first hlk_op that is being called (e.g. hlk_binary_op_init()).
// If the first op is reduce or matmul, you need to call one of those 3 hw configs.
// If it is not one of those, then call hlk_hw_config_single_operand() for unary ops, and hlk_hw_config_two_operands() for binary ops.
// After HW config is called, it is usually followed by the actual op init call. (e.g. hlk_binary_op_init())

TT_HLK_ALWAYS_INLINE void hlk_hw_config_single_operand(tt_core* core_ptr, int stream, int transpose);
TT_HLK_ALWAYS_INLINE void hlk_hw_config_two_operands(tt_core* core_ptr, int stream_a, int stream_b, int transpose);
// Wormhole_b0 specific API
TT_HLK_ALWAYS_INLINE void hlk_hw_config_int8_fpu_math(tt_core* core_ptr);

template<ReduceFunc reduce_func, Dim reduce_dim>
TT_HLK_ALWAYS_INLINE void hlk_hw_config_reduce(tt_core* core_ptr, int stream, float coefficient);
TT_HLK_ALWAYS_INLINE void hlk_hw_config_matmul(tt_core* core_ptr, int stream_a, int stream_b, int transpose);

// Hw config for workarounds
TT_HLK_ALWAYS_INLINE void hlk_dbg_feature_disable(tt_core* core_ptr);

// Op inits
// init -> dynamic unpacker+packer+math init
// init_short -> dynamic unpacker+math init (no llk output for packer)

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_init(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose);
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_init_short(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose);
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly_init(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose);
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly_init_short(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose);

TT_HLK_ALWAYS_INLINE void hlk_mm_tile_init(tt_core* core_ptr, int lstream, int rstream, int transpose);
TT_HLK_ALWAYS_INLINE void hlk_mm_tile_init_short(tt_core* core_ptr, int lstream, int rstream, int transpose);

TT_HLK_ALWAYS_INLINE void hlk_mm_block_init(tt_core* core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim);
TT_HLK_ALWAYS_INLINE void hlk_mm_block_init_short(tt_core* core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim);

template<ReduceFunc reduce_func, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile_init(tt_core* core_ptr, int within_face_16x16_transpose);
template<ReduceFunc reduce_func, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile_init_short(tt_core* core_ptr, int within_face_16x16_transpose);

TT_HLK_ALWAYS_INLINE void hlk_transpose_xy_tile_init(tt_core* core_ptr, int transpose_of_faces, int within_face_16x16_transpose);
TT_HLK_ALWAYS_INLINE void hlk_transpose_xy_tile_init_short(tt_core* core_ptr, int transpose_of_faces, int within_face_16x16_transpose);

TT_HLK_ALWAYS_INLINE void hlk_tilize_init(tt_core* core_ptr, int stream, int ct_dim);
TT_HLK_ALWAYS_INLINE void hlk_tilize_init_short(tt_core* core_ptr, int stream, int ct_dim);

TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile_init(tt_core* core_ptr);
TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile_init_short(tt_core* core_ptr);

// binary sfpu
// Wormhole_b0 specific APIs
TT_HLK_ALWAYS_INLINE void hlk_sfpu_quant_int32_init(tt_core* core_ptr, int zero_point);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_requant_int32_init(tt_core* core_ptr, int zero_point);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_dequant_int32_init(tt_core* core_ptr, int zero_point);
TT_HLK_ALWAYS_INLINE void hlk_sfpu_add_int32_init(tt_core* core_ptr);
TT_HLK_ALWAYS_INLINE void hlk_topk_init(tt_core* core_ptr, int stream);

// sfpu_specific_parameter is unused except in these cases:
// 1. dropout - represents seed
template <SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_unary_sfpu_init(
    tt_core* core_ptr, int stream, unsigned int sfpu_specific_parameter = 0);

// Reconfigure data formats
TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_df(tt_core *core_ptr, int old_stream, int new_stream);
TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_df(tt_core *core_ptr, int new_stream);

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df(tt_core *core_ptr, int old_lstream, int new_lstream, int old_rstream, int new_rstream);
TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr, int old_srca, int new_srca);
TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr, int old_srcb, int new_srcb);

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df(tt_core *core_ptr, int new_lstream, int new_rstream);
TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr, int new_srca);
TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr, int new_srcb);

// Configure relu
TT_HLK_ALWAYS_INLINE void hlk_relu_config(tt_core *core_ptr, int config);

// Reonfigure (enable/disable) l1 accumulation
TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_l1_acc(tt_core *core_ptr, int config);

// Configure and clear masks for reduce
template<Dim reduce_dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_mask_config(tt_core* core_ptr);
TT_HLK_ALWAYS_INLINE void hlk_reduce_mask_clear(tt_core* core_ptr);

// Perf measurement functions - they do nothing in standard LLK builds, and are active only if PERF_DUMP or KERNEL_DELAY is enabled.
TT_HLK_ALWAYS_INLINE void hlk_pre_input_processing(tt_core* core_ptr, const int input_index);
TT_HLK_ALWAYS_INLINE void hlk_post_input_processing(tt_core* core_ptr);

// The function which ends up being called on hardware on each trisc (unpacker, math, packer).
// The user needs to define 2 functions:
// - hlk_setup_kernel()             - a function where HW config and inits should be called before any input is processed.
// - hlk_process_single_input()     - a function where a single input is processed.
template<typename hlk_args>
void hlk_process_all_inputs(tt_core* core_ptr, const hlk_args *args, const int input_count) {
    hlk_setup_kernel(core_ptr, args);
    for (int i = 0; i < input_count; i++) {
        hlk_pre_input_processing(core_ptr, i);
        hlk_process_single_input(core_ptr, args);
        hlk_post_input_processing(core_ptr);
    }
}