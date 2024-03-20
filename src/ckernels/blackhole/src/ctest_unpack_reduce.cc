
#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_reduce_params.h"
#include "llk_unpack_api.h"
#include "llk_io_unpack.h"

#ifndef REDUCE_TYPE
#define REDUCE_TYPE AVG
#endif
#ifndef REDUCE_DIM
#define REDUCE_DIM REDUCE_ROW
#endif
#ifndef FP32_DEST_ACC_EN
#define FP32_DEST_ACC_EN false
#endif
namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_reduce_params = static_cast<const llk_unpack_reduce_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int num_block_rows = hlk_params->num_block_rows;
    const int num_block_columns = hlk_params->num_block_columns;
    const int num_tile_rows_per_block = hlk_params->num_tile_rows_per_block;
    const int num_tile_columns_per_block = hlk_params->num_tile_columns_per_block;

    llk_unpack_reduce_hw_configure<PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM, FP32_DEST_ACC_EN>(unpack_reduce_params, const_mult);
    llk_unpack_reduce_init<PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM>();

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int br = 0; br < num_block_rows; ++br) {
            for (int bc = 0; bc < num_block_columns; ++bc) {
                llk_wait_tiles(0, num_tile_rows_per_block * num_tile_columns_per_block);

                for (int r = 0; r < num_tile_rows_per_block; ++r) {
                    for (int c = 0; c < num_tile_columns_per_block; ++c) {
                        llk_unpack_reduce<PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM>(
                            0, r * num_tile_columns_per_block + c);
                    }
                }

                llk_pop_tiles(0, num_tile_rows_per_block * num_tile_columns_per_block);
            }
        }
    }

    return 0;
}

}  // namespace NAMESPACE
