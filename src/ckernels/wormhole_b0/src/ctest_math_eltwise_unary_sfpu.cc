#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_math_eltwise_unary_params.h"
#include "llk_math_api.h"

#include <type_traits>

#ifndef SFPU_TYPE
#define SFPU_TYPE exponential
#endif

#ifndef APPROX
#define APPROX true
#endif

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_math_pack_sync_init<SyncFull>();
    llk_math_eltwise_unary_datacopy_init<DataCopyType::A2D>();
    if constexpr (SfpuType::SFPU_TYPE == SfpuType::exponential) {
        llk_math_eltwise_unary_sfpu_exponential_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sqrt) {
        llk_math_eltwise_unary_sfpu_sqrt_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu) {
        llk_math_eltwise_unary_sfpu_gelu_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu_derivative) {
        llk_math_eltwise_unary_sfpu_gelu_derivative_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::reciprocal) {
        llk_math_eltwise_unary_sfpu_reciprocal_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::log) {
        llk_math_eltwise_unary_sfpu_log_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::tanh) {
        llk_math_eltwise_unary_sfpu_tanh_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sigmoid) {
        llk_math_eltwise_unary_sfpu_sigmoid_init<APPROX>(0);
    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::dropout) {
        llk_math_eltwise_unary_sfpu_dropout_init(0, sfpu_params[2]);
    } else {
        static_assert(SfpuType::SFPU_TYPE < SfpuType::unused, "SFPU_TYPE not supported");
    }

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_math_wait_for_dest_available<SyncFull>();

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_eltwise_unary_datacopy<DataCopyType::A2D, BroadcastType::NONE, SyncFull>(
                        r * dst_tile_cols + c);
                }
            }

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    if constexpr (SfpuType::SFPU_TYPE == SfpuType::exponential) {
                        llk_math_eltwise_unary_sfpu_exponential<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sqrt) {
                        llk_math_eltwise_unary_sfpu_sqrt<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu) {
                        llk_math_eltwise_unary_sfpu_gelu<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::gelu_derivative) {
                        llk_math_eltwise_unary_sfpu_gelu_derivative<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::reciprocal) {
                        llk_math_eltwise_unary_sfpu_reciprocal<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::log) {
                        llk_math_eltwise_unary_sfpu_log<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::tanh) {
                        llk_math_eltwise_unary_sfpu_tanh<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::sigmoid) {
                        llk_math_eltwise_unary_sfpu_sigmoid<APPROX, SyncFull>(0, r * dst_tile_cols + c);
                    } else if constexpr (SfpuType::SFPU_TYPE == SfpuType::dropout) {
                        llk_math_eltwise_unary_sfpu_dropout<SyncFull>(
                            0, r * dst_tile_cols + c, (int)VectorMode::RC, sfpu_params[0], sfpu_params[1]);
                    } else {
                        static_assert(SfpuType::SFPU_TYPE < SfpuType::unused, "SFPU_TYPE not supported");
                    }
                }
            }
            llk_math_dest_section_done<SyncFull>();
        }
    }

    return 0;
}
}  // namespace NAMESPACE
