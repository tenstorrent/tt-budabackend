// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_verify.h"

#include <glog/logging.h>

#include <unordered_map>

namespace CA = CommandAssembler;

bool llk::verify_match_ratio(
    llk::Tensor expected,
    llk::Tensor calculated,
    float minimum_match_ratio,
    float match_ratio_error_threshold,
    float dropout_percentage,
    float dropout_percentage_error_threshold,
    bool skip_match_ratio_check,
    bool skip_dropout_check) {
    bool passed = true;
    float fp_comparison_error = match_ratio_error_threshold * 100;  //  0 < x < 100
    float match_ratio_threshold = minimum_match_ratio * 100;        //  0 < x < 100
    float dropout_perc = dropout_percentage;
    float dropout_tol = dropout_percentage_error_threshold * 100;
    bool report = true;
    bool dropout_passed = true;

    if (expected.dims != calculated.dims) {
        passed = false;
        LOG(ERROR) << __FUNCTION__ << "::Expected dims:" << expected.dims.str()
                   << " does not match Calculated dims:" << calculated.dims.str();
    } else {
        // iteration through each tile
        std::unordered_map<std::string, bool> comparison_results;
        float cumulative_drop_ratio = 0.0f;
        float average_drop_ratio = 0.0f;
        for (auto w = 0; w < expected.tile_tensor.size(); w++) {
            for (auto z = 0; z < expected.tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < expected.tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < expected.tile_tensor[w][z][ct].size(); rt++) {
                        bool match_ratio_failed = false;
                        try {
                            if (not skip_dropout_check) {
                                auto results = calculated.tile_tensor[w][z][ct][rt].compare_dropout_result(
                                    expected.tile_tensor[w][z][ct][rt],
                                    fp_comparison_error,
                                    match_ratio_threshold,
                                    false,
                                    false,
                                    RELU_NONE,
                                    0.0f,
                                    dropout_perc,
                                    100);
                                cumulative_drop_ratio += get<1>(results);
                            }
                        } catch (...) {
                        }

                        try {
                            if (not skip_match_ratio_check) {
                                auto results = calculated.tile_tensor[w][z][ct][rt].compare_result(
                                    expected.tile_tensor[w][z][ct][rt],
                                    fp_comparison_error,
                                    match_ratio_threshold,
                                    report);
                                match_ratio_failed |= get<0>(results);
                                if (get<0>(results)) {
                                    std::cout << "Match Ratio Failed for (w=" << w << ",z=" << z << ",ct=" << ct
                                              << ",rt=" << rt << ")" << std::endl;
                                }
                            }
                        } catch (...) {
                            std::cout << "Match Ratio Failed for (w=" << w << ",z=" << z << ",ct=" << ct << ",rt=" << rt
                                      << ")" << std::endl;
                            match_ratio_failed = true;
                        }

                        passed &= (not match_ratio_failed);
                    }
                }
            }
        }
        average_drop_ratio = cumulative_drop_ratio / expected.num_tiles();
        dropout_passed = skip_dropout_check or ((average_drop_ratio <= ((dropout_perc * 100.0f) + dropout_tol)) and
                                                (average_drop_ratio >= ((dropout_perc * 100.0f) - dropout_tol)));
        if (not skip_dropout_check) {
            std::printf(
                "Dropout Ratio Observed: %0f -- Dropout Ratio Expected: %0f -- Tolerance: %0f\n",
                average_drop_ratio,
                dropout_perc * 100.0f,
                dropout_tol);
            if (not dropout_passed) {
                std::cout << "DROPOUT FAILED FOR TENSOR, average dropout fails tolerance check" << std::endl;
            }
        }
    }
    return passed and dropout_passed;
}

bool llk::verify_pcc(llk::Tensor expected, llk::Tensor calculated, float minimum_pcc, bool skip_pcc_check) {
    bool passed = true;
    float pcc_threshold = minimum_pcc;  //  -1 < x < 1
    bool report = true;

    if (expected.dims != calculated.dims) {
        passed = false;
        LOG(ERROR) << __FUNCTION__ << "::Expected dims:" << expected.dims.str()
                   << " does not match Calculated dims:" << calculated.dims.str();
    } else {
        // iteration through each tile
        std::unordered_map<std::string, bool> comparison_results;
        for (auto w = 0; w < expected.tile_tensor.size(); w++) {
            for (auto z = 0; z < expected.tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < expected.tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < expected.tile_tensor[w][z][ct].size(); rt++) {
                        bool pcc_passed = true;
                        try {
                            if ((not skip_pcc_check) and
                                (not expected.tile_tensor[w][z][ct][rt].are_all_values_equal())) {
                                pcc_passed = expected.tile_tensor[w][z][ct][rt].correlation_compare(
                                    calculated.tile_tensor[w][z][ct][rt], pcc_threshold);
                            }
                            if (not pcc_passed) {
                                std::cout << "PCC Failed for (w=" << w << ",z=" << z << ",ct=" << ct << ",rt=" << rt
                                          << ")" << std::endl;
                            }
                        } catch (...) {
                            std::cout << "PCC Failed for (w=" << w << ",z=" << z << ",ct=" << ct << ",rt=" << rt << ")"
                                      << std::endl;
                            pcc_passed = false;
                        }

                        passed &= pcc_passed;
                    }
                }
            }
        }
    }
    return passed;
}