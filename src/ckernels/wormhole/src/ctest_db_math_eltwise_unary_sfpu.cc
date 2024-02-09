#include "llk_param_structs.h"
#include "ctest_db_math_eltwise_unary_params.h"
#include "llk_math_eltwise_unary_datacopy.h"
#include "llk_math_eltwise_unary_sfpu.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#define MATH_DATACOPY_OP A2D
#else
#define MATH_DATACOPY_OP B2D
#endif

#ifndef TRANSPOSE_XY
#define TRANSPOSE_XY 0
#endif

#ifndef SFPU_TYPE
#define SFPU_TYPE exponential
#endif

#ifndef APPROX
#define APPROX true
#endif

#ifndef FP32_DEST_ACC_EN
#define FP32_DEST_ACC_EN false
#endif

namespace NAMESPACE {

uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_math_eltwise_unary_datacopy_init<MATH_DATACOPY_OP, BroadcastType::SRC_B_BROADCAST_TYPE>(TRANSPOSE_XY);
    llk_math_pack_sync_init<SyncHalf, FP32_DEST_ACC_EN>();
    if constexpr (SfpuType::SFPU_TYPE == SfpuType::exponential) {
        llk_math_eltwise_unary_sfpu_exponential_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sqrt) {
        llk_math_eltwise_unary_sfpu_sqrt_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu) {
        llk_math_eltwise_unary_sfpu_gelu_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu_derivative) {
        llk_math_eltwise_unary_sfpu_gelu_derivative_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::reciprocal) {
        llk_math_eltwise_unary_sfpu_reciprocal_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::log) {
        llk_math_eltwise_unary_sfpu_log_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::tanh) {
        llk_math_eltwise_unary_sfpu_tanh_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sigmoid) {
        llk_math_eltwise_unary_sfpu_sigmoid_init<APPROX>();
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::dropout) {
        llk_math_eltwise_unary_sfpu_dropout_init(sfpu_params[2]);
    } else {
        static_assert_sfpu_type_dependent<SfpuType::SFPU_TYPE>();
    }

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_math_wait_for_dest_available<SyncHalf>();

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_eltwise_unary_datacopy<MATH_DATACOPY_OP, BroadcastType::SRC_B_BROADCAST_TYPE, SyncHalf, FP32_DEST_ACC_EN>(
                        r * dst_tile_cols + c);
                }
            }

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    if constexpr (SfpuType::SFPU_TYPE == SfpuType::exponential) {
                        llk_math_eltwise_unary_sfpu_exponential<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sqrt) {
                        llk_math_eltwise_unary_sfpu_sqrt<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu) {
                        llk_math_eltwise_unary_sfpu_gelu<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu_derivative) {
                        llk_math_eltwise_unary_sfpu_gelu_derivative<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::reciprocal) {
                        llk_math_eltwise_unary_sfpu_reciprocal<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::log) {
                        llk_math_eltwise_unary_sfpu_log<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::tanh) {
                        llk_math_eltwise_unary_sfpu_tanh<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sigmoid) {
                        llk_math_eltwise_unary_sfpu_sigmoid<APPROX, SyncHalf>(r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::dropout) {
                        llk_math_eltwise_unary_sfpu_dropout<SyncHalf>(
                            r * dst_tile_cols + c, Dim::RC, sfpu_params[0], sfpu_params[1]);
                    } else {
                        static_assert_sfpu_type_dependent<SfpuType::SFPU_TYPE>();
                    }
                }
            }
            llk_math_dest_section_done<SyncHalf, FP32_DEST_ACC_EN>();
        }
    }

    return 0;
}
}  // namespace NAMESPACE
