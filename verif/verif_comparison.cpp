// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_comparison.hpp"

#include <iomanip>

#include "common/tensor_lib.hpp"
#include "utils/logger.hpp"
#include "verif_test_config.hpp"
#include "yaml-cpp/yaml.h"

using std::setprecision;
using std::setw;
using std::signbit;
namespace verif::comparison {
ComparisonVerbosity get_comparison_verbosity(string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
    if (input == "concise") {
        return ComparisonVerbosity::Concise;
    } else if (input == "allfails") {
        return ComparisonVerbosity::AllFails;
    } else if (input == "verbose") {
        return ComparisonVerbosity::Verbose;
    } else {
        throw std::runtime_error("Incorrect comparison verbosity selected");
    }
}

ComparisonMethod get_comparison_method(string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
    if(input == "tilizedtensor") {
        return ComparisonMethod::TilizedTensor;
    }
    else if(input == "flattensor") {
        return ComparisonMethod::FlatTensor;
    }
    else {
        throw std::runtime_error("Incorrect comparison method selected");
    }
}

ComparisonType get_comparison_type(string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
    if (input == "exact") {
        return ComparisonType::Exact;
    } else if (input == "allclose") {
        return ComparisonType::AllClose;
    } else if (input == "allclosehw") {
        return ComparisonType::AllCloseHw;
    } else {
        throw std::runtime_error("Incorrect comparison type selected");
    }
}

bool are_tensor_dims_equal(const tt_tensor &lhs, const tt_tensor &rhs) { return lhs.same_shape(rhs); }

double inf_to_num(double in) {
    if (isinf(in)) {
        if (signbit(in))
            return -MAXFLOAT;
        else
            return MAXFLOAT;
    } else {
        return in;
    }
}

bool inf_compare(float a, float b) {
    log_assert(isinf(a) || isinf(b), "At least 1 value must be infinity");
    log_assert(!isnan(a) && !isnan(b), "Cannot be nan");

    if (a == b) {
        return true;
    }

    if (signbit(a) != signbit(b)) {
        return false;
    }

    // Store the potential non-inf value in b
    if (!isinf(a) && isinf(b)) {
        swap(a, b);
    }

    int exp;
    frexp(b, &exp);

    // It's not big enough to be inf
    if (exp <= 126) {
        return false;
    }

    return true;
}

std::tuple<bool, double> allclose(
    const tt_tile &lhs,
    const tt_tile &rhs,
    const double rtol,
    const double atol,
    double pct_matched,
    bool print_diffs,
    const std::pair<int, int> &check_tile_rows_range,
    const std::pair<int, int> &check_tile_cols_range) {
    bool close = true;
    int total_mismatches = 0;
    stringstream out_ss;
    for (int i = get<0>(check_tile_rows_range); i < get<1>(check_tile_rows_range); ++i) {
        for (int j = get<0>(check_tile_cols_range); j < get<1>(check_tile_cols_range); ++j) {
            // Implements the C++ equivalent of the numpy allclose function:
            // https://numpy.org/doc/stable/reference/generated/numpy.isclose.html
            if (isnan(lhs.t[i][j]) or isnan(rhs.t[i][j])) {
                bool nan_same = isnan(lhs.t[i][j]) and isnan(rhs.t[i][j]);
                close &= nan_same;
                total_mismatches += nan_same ? 0 : 1;
            } else if (isinf(lhs.t[i][j]) or isinf(rhs.t[i][j])) {
                bool inf_same = inf_compare(lhs.t[i][j], rhs.t[i][j]);
                close &= inf_same;
                total_mismatches += inf_same ? 0 : 1;
            } else {
                bool nums_close =
                    fabs(lhs.t[i][j] - rhs.t[i][j]) <= (atol + rtol * std::max(fabs(lhs.t[i][j]), fabs(rhs.t[i][j])));
                double rel_error;
                if (rhs.t[i][j] == 0.0) {
                    rel_error = (fabs(lhs.t[i][j] - rhs.t[i][j])) / numeric_limits<float>::min();
                } else {
                    rel_error = (fabs(lhs.t[i][j] - rhs.t[i][j])) / fabs(rhs.t[i][j]);
                }
                total_mismatches += (!nums_close) ? 1 : 0;
                if (print_diffs) {
                    if (nums_close) {
                        out_ss << "(" << i << ", " << j << ") lhs = " << lhs.t[i][j] << ", rhs = " << rhs.t[i][j]
                               << ", pct error =  " << rel_error;
                    } else {
                        out_ss << "-- Not close --> (" << i << ", " << j << ") lhs = " << lhs.t[i][j]
                               << ", rhs = " << rhs.t[i][j] << ", pct error =  " << rel_error;
                    }
                }
                close &= nums_close;
            }
        }
    }
    double total_nums = (double)(get<1>(check_tile_rows_range) - get<0>(check_tile_rows_range) + 1) *
                        (get<1>(check_tile_cols_range) - get<0>(check_tile_cols_range) + 1);
    double total_matched_pct = (total_nums - (double)total_mismatches) / total_nums;
    out_ss << "allclose { ";
    out_ss << "total matched percentage = " << fixed << setprecision(5) << total_matched_pct;
    out_ss << ", total number of mismatches = " << total_mismatches;
    out_ss << "}";
    log_debug(tt::LogVerif, "{}", out_ss.str());
    if (total_matched_pct >= pct_matched) {
        close = true;
    } else {
        close = false;
        stringstream error_ss;
        error_ss << fixed << setprecision(5);
        error_ss << "\033[1;31m<EXIT-SIG> Failed due to match % " << total_matched_pct
                 << " being lower than pass threshhold of " << pct_matched << "\033[0m";
        log_error("{}", out_ss.str());
    }
    return std::tie(close, total_matched_pct);
}

bool exact_compare_on_vectors(vector<float>::iterator lhs, const vector<float>::iterator rhs, int num_datums_to_check){
    bool match = true;
    for(int idx = 0; idx < num_datums_to_check; idx++){
        match &= *(lhs + idx) == *(rhs + idx);
    }
    return match;
}

bool allclose_on_vectors(vector<float>::iterator lhs, vector<float>::iterator rhs, double rtol, double atol, double pct_matched, int num_datums_to_check, double& total_pct_across_threads, std::mutex& pct_mutex){
    bool close = true;
    int total_mismatches = 0;
    for(int idx = 0; idx < num_datums_to_check; idx++){
        if(isnan(*(lhs + idx)) or isnan(*(rhs + idx))){
            total_mismatches += (isnan(*(lhs + idx)) and isnan(*(rhs + idx))) ? 0 : 1;
        }
        else if(isinf(*(lhs + idx)) or isinf(*(rhs + idx))){
            total_mismatches += inf_compare(*(lhs + idx), *(rhs + idx)) ? 0 : 1;
        }
        else{
            bool nums_close = fabs(*(lhs + idx) - *(rhs + idx)) <= (atol + rtol * std::max(fabs(*(lhs + idx)), fabs(*(rhs + idx))));
            total_mismatches += nums_close ? 0 : 1;
        }
    }
    double total_matched_pct = (num_datums_to_check - double(total_mismatches))/ num_datums_to_check;

    std::lock_guard<std::mutex> lock(pct_mutex); // guard shared resource
    total_pct_across_threads = total_pct_across_threads + total_matched_pct;
    bool pass = total_matched_pct >= pct_matched;
    if(!pass) log_error("AllClose Check Failed on Vector Slice with pct = {}", total_matched_pct);
    return pass;
}

bool pcc_compare_on_vectors(vector<float>::iterator lhs, vector<float>::iterator rhs, double rtol, double atol, double pass_pcc, int num_datums_to_check, double& total_pcc){
    bool pass = false;
    if (fabs(pass_pcc) > 1.0) {
        log_fatal("pcc_compare: pcc_compare must be -1 <= x <= 1");
    }

    if ((rtol < 0.0) || (atol < 0.0)) {
        log_fatal("pcc_compare: rtol and atol must be non-negative!");
    }

    double sum_t1 = 0;
    double sum_t2 = 0;
    double square_sum_t1 = 0;
    double square_sum_t2 = 0;
    double sum_t1_t2 = 0;

    bool is_same_t1 = true;
    bool is_same_t2 = true;
    
    const double first_val_t1 = inf_to_num(static_cast<double>(*lhs));
    const double first_val_t2 = inf_to_num(static_cast<double>(*rhs));

    for(int idx = 0; idx < num_datums_to_check; idx++){
        double num1 = inf_to_num((double)*(lhs + idx));
        double num2 = inf_to_num((double)*(rhs + idx));

        is_same_t1 = is_same_t1 && (num1 == first_val_t1);
        is_same_t2 = is_same_t2 && (num2 == first_val_t2);

        sum_t1 += num1;                  // Sum of elements of tile1
        square_sum_t1 += (num1 * num1);  // Sum of square of tile1 elements.
        sum_t2 += num2;                  // Sum of elements of tile2
        square_sum_t2 += (num2 * num2);  // Sum of square of tile2 elements.
        sum_t1_t2 += (num1 * num2);
    }

    double mean_t1 = sum_t1 / num_datums_to_check;
    double mean_t2 = sum_t2 / num_datums_to_check;
    double var_t1 = (square_sum_t1 / num_datums_to_check) - (mean_t1 * mean_t1);
    double var_t2 = (square_sum_t2 / num_datums_to_check) - (mean_t2 * mean_t2);
    double mean_delta = fabs(mean_t1 - mean_t2);
    // use allclose condition to prevent false pass
    bool pass_threshold = (mean_delta <= (atol + rtol * mean_t2));
    bool tiles_identical = pass_threshold && is_same_t1 && is_same_t2;

    double ratio_t1 = (mean_t1 * mean_t1) / (square_sum_t1 / num_datums_to_check);
    double ratio_t2 = (mean_t2 * mean_t2) / (square_sum_t2 / num_datums_to_check);
    bool rnd_err_t1 = (var_t1 < 0 && ratio_t1 >= 1.00 && ratio_t1 <= 1.0000001);
    bool rnd_err_t2 = (var_t2 < 0 && ratio_t2 >= 1.00 && ratio_t2 <= 1.0000001);
    if (rnd_err_t1)
        var_t1 = 0.0f;
    if (rnd_err_t2)
        var_t2 = 0.0f;

    double stdev_t1 = sqrt(var_t1);                                           // Standard deviation for tile1
    double stdev_t2 = sqrt(var_t2);                                           // Standard deviation for tile2
    double cov_t1_t2 = (sum_t1_t2 / num_datums_to_check) - (mean_t1 * mean_t2);  // Covariance of tile1 and tile2

    double ratio_cov = (mean_t1 * mean_t2) / (sum_t1_t2 / num_datums_to_check);
    bool rnd_err_cov = (cov_t1_t2 < 0 && ratio_cov >= 1.00 && ratio_cov <= 1.0000001);
    if (rnd_err_cov)
        cov_t1_t2 = 0.0f;  // correct neg rounding error
    bool tiles_very_close = rnd_err_t1 && rnd_err_t2 && rnd_err_cov;

    if (stdev_t1 == 0.0)
        stdev_t1 =
            0.00001;  // Adding a small offset if standard deviation is zero, to avoid div by 0 in PCC formula below
    if (stdev_t2 == 0.0)
        stdev_t2 = 0.00001;

    double pcc = (cov_t1_t2) / (stdev_t1 * stdev_t2);
    total_pcc += pcc;
    
    if (cov_t1_t2 == 0.0)
        log_debug(tt::LogVerif, "pcc_compare: Covariance for two tiles is 0. Data in two tiles might be independent");
    
    bool sign_equal = signbit(pcc) == signbit(pass_pcc);
    
    if (fabs(pcc) >= fabs(pass_pcc) && sign_equal) {
        pass = fabs(pcc) <=  1.0000001;
    } else if (tiles_identical or tiles_very_close) {
        pass = true;
        log_debug(
            tt::LogVerif,
            "Encountered a special condition for which we skip the Pearson correlation coefficient check:");
        log_debug(
            tt::LogVerif,
            "   within each tile, all values in one tile are all the same except for a few very close datums");
        log_debug(
            tt::LogVerif,
            "   resulting in a coefficient of effectively 0, thus Pearson's correlation is not defined and we assume "
            "tile is fine");

        pcc = 1.0;
    } else if (pass_pcc == 0.0) {
        pass = true;
        log_warning(tt::LogVerif, "pcc_compare skipped due to pass_pcc being set to 0");
    } else {
        pass = false;
    }
    if(!pass) log_error("PCC Check Failed on Vector Slice with pcc = {}", pcc);
    return pass;
}

/*
    Compare two tiles using Pearson correlation coefficient (PCC). PCC it is a measure of the linear correlation between
   two variables X and Y. It has a value between +1 and −1: 1 indicates a strong positive relationship. -1 indicates a
   strong negative relationship. 0 indicates no relationship at all.
*/
std::tuple<bool, double> pcc_compare(
    const tt_tile &lhs,
    const tt_tile &rhs,
    const double rtol,
    const double atol,
    const double pass_pcc,
    const std::pair<int, int> &check_tile_rows_range,
    const std::pair<int, int> &check_tile_cols_range) {
    bool pass = false;
    bool debug = false;
    if (fabs(pass_pcc) > 1.0) {
        log_fatal("pcc_compare: pcc_compare must be -1 <= x <= 1");
    }

    if ((rtol < 0.0) || (atol < 0.0)) {
        log_fatal("pcc_compare: rtol and atol must be non-negative!");
    }

    double sum_t1 = 0;
    double sum_t2 = 0;
    double square_sum_t1 = 0;
    double square_sum_t2 = 0;
    double sum_t1_t2 = 0;
    double total_data_count = (double)(get<1>(check_tile_rows_range) - get<0>(check_tile_rows_range) + 1) *
                              (get<1>(check_tile_cols_range) - get<0>(check_tile_cols_range) + 1);

    bool is_same_t1 = true;
    bool is_same_t2 = true;
    const double first_val_t1 = inf_to_num(static_cast<double>(lhs.t[0][0]));
    const double first_val_t2 = inf_to_num(static_cast<double>(rhs.t[0][0]));

    for (int i = get<0>(check_tile_rows_range); i < get<1>(check_tile_rows_range); ++i) {
        for (int j = get<0>(check_tile_cols_range); j < get<1>(check_tile_cols_range); ++j) {
            double num1 = (double)lhs.t[i][j];
            double num2 = (double)rhs.t[i][j];

            if (isinf(num1))
                num1 = inf_to_num(num1);
            if (isinf(num2))
                num2 = inf_to_num(num2);

            is_same_t1 = is_same_t1 && (num1 == first_val_t1);
            is_same_t2 = is_same_t2 && (num2 == first_val_t2);

            sum_t1 += num1;                  // Sum of elements of tile1
            square_sum_t1 += (num1 * num1);  // Sum of square of tile1 elements.
            sum_t2 += num2;                  // Sum of elements of tile2
            square_sum_t2 += (num2 * num2);  // Sum of square of tile2 elements.
            sum_t1_t2 += (num1 * num2);
        }
    }

    double mean_t1 = sum_t1 / total_data_count;
    double mean_t2 = sum_t2 / total_data_count;
    double var_t1 = (square_sum_t1 / total_data_count) - (mean_t1 * mean_t1);
    double var_t2 = (square_sum_t2 / total_data_count) - (mean_t2 * mean_t2);
    double mean_delta = fabs(mean_t1 - mean_t2);
    // use allclose condition to prevent false pass
    bool pass_threshold = (mean_delta <= (atol + rtol * mean_t2));
    bool tiles_identical = pass_threshold && is_same_t1 && is_same_t2;

    // Handle rounding error cases when near identical numbers are seen
    double ratio_t1 = (mean_t1 * mean_t1) / (square_sum_t1 / total_data_count);
    double ratio_t2 = (mean_t2 * mean_t2) / (square_sum_t2 / total_data_count);
    bool rnd_err_t1 = (var_t1 < 0 && ratio_t1 >= 1.00 && ratio_t1 <= 1.0000001);
    bool rnd_err_t2 = (var_t2 < 0 && ratio_t2 >= 1.00 && ratio_t2 <= 1.0000001);
    if (rnd_err_t1)
        var_t1 = 0.0f;
    if (rnd_err_t2)
        var_t2 = 0.0f;

    double stdev_t1 = sqrt(var_t1);                                           // Standard deviation for tile1
    double stdev_t2 = sqrt(var_t2);                                           // Standard deviation for tile2
    double cov_t1_t2 = (sum_t1_t2 / total_data_count) - (mean_t1 * mean_t2);  // Covariance of tile1 and tile2

    double ratio_cov = (mean_t1 * mean_t2) / (sum_t1_t2 / total_data_count);
    bool rnd_err_cov = (cov_t1_t2 < 0 && ratio_cov >= 1.00 && ratio_cov <= 1.0000001);
    if (rnd_err_cov)
        cov_t1_t2 = 0.0f;  // correct neg rounding error
    bool tiles_very_close = rnd_err_t1 && rnd_err_t2 && rnd_err_cov;

    if (stdev_t1 == 0.0)
        stdev_t1 =
            0.00001;  // Adding a small offset if standard deviation is zero, to avoid div by 0 in PCC formula below
    if (stdev_t2 == 0.0)
        stdev_t2 = 0.00001;

    double pcc = (cov_t1_t2) / (stdev_t1 * stdev_t2);

    if (cov_t1_t2 == 0.0)
        log_debug(tt::LogVerif, "pcc_compare: Covariance for two tiles is 0. Data in two tiles might be independent");

    stringstream out_ss;
    out_ss << "pcc_compare { ";
    out_ss << "Pearson's correlation coefficient = " << fixed << setprecision(5) << pcc;

    bool sign_equal = signbit(pcc) == signbit(pass_pcc);
    if (fabs(pcc) >= fabs(pass_pcc) && sign_equal) {
        if (fabs(pcc) <= 1.0000001) {
            pass = true;
        } else {
            log_error("\033[1;31m<EXIT-SIG> PCC {} greater than 1 is unexpected \033[0m", pcc);
            pass = false;
        }
    } else if (tiles_identical or tiles_very_close) {
        pass = true;
        log_debug(
            tt::LogVerif,
            "Encountered a special condition for which we skip the Pearson correlation coefficient check:");
        log_debug(
            tt::LogVerif,
            "   within each tile, all values in one tile are all the same except for a few very close datums");
        log_debug(
            tt::LogVerif,
            "   resulting in a coefficient of effectively 0, thus Pearson's correlation is not defined and we assume "
            "tile is fine");

        pcc = 1.0;
    } else if (pass_pcc == 0.0) {
        pass = true;
        log_warning(tt::LogVerif, "pcc_compare skipped due to pass_pcc being set to 0");
    } else {
        pass = false;
        stringstream error_ss;
        error_ss << fixed << setprecision(5);
        error_ss << "\033[1;31m<EXIT-SIG> Failed due to PCC " << pcc << " being lower than pass threshhold of "
                 << pass_pcc << "\033[0m\n"
                 << endl;
        log_error("{}", error_ss.str());
    }

    if (debug or !pass) {
        out_ss << fixed << setw(10) << setprecision(3);
        out_ss << ", T1_sum = " << sum_t1 << ", T1_mean =" << mean_t1 << ", T1_sq_sum =" << square_sum_t1
               << ", T1_var = " << var_t1 << ", T1_stddev = " << stdev_t1;
        out_ss << ", T2_sum = " << sum_t2 << ", T2_mean = " << mean_t2 << ", T2_sq_sum =" << square_sum_t2
               << ", T2_var = " << var_t2 << ", T2_stddev = " << stdev_t2;
        out_ss << ", T1-T2 Covariance: " << cov_t1_t2;
    }
    out_ss << "}";
    log_debug(tt::LogVerif, "{}", out_ss.str());
    return std::tie(pass, pcc);
}
bool compare_flat_tensor_data(vector<float>& lhs, vector<float>& rhs, const VerifComparisonConfig &comp){
    int num_threads = 8;
    std::vector<std::thread> compare_threads;
    std::vector<exception_ptr> exceptions_all_threads(num_threads, nullptr);

    bool pass = true;
    double total_pcc = 0;
    double total_pct = 0;
    std::mutex pass_mutex, pcc_mutex, pct_mutex; //guard shared resources across threads
    
    log_assert(lhs.size() == rhs.size(), "Flat Tensor Dims Mismatch -- lhs:{} rhs:{}", lhs.size(), rhs.size());
    if(lhs.size() != rhs.size()) return false;

    if(comp.type == ComparisonType::Exact){
        log_info(tt::LogVerif, "Performing Exact Check on Flattened Tensors");
        for(int th = 0; th < num_threads; th++){
            compare_threads.push_back(std::thread([th, &lhs, &rhs, num_threads, &pass, &comp, &exceptions_all_threads, &pass_mutex] {
                try{
                    bool result = exact_compare_on_vectors(lhs.begin() + th*lhs.size()/num_threads, rhs.begin() + th*rhs.size()/num_threads, lhs.size()/num_threads);
                    std::lock_guard<std::mutex> lock(pass_mutex);
                    pass &= result;
                } catch(std::exception &e){
                    exceptions_all_threads.at(th) = std::current_exception();
                }
            }));
        }
        for (auto th = 0; th < num_threads; th++) {
            compare_threads.at(th).join();
        }
        for (auto th = 0; th < num_threads; th++) {
            if (exceptions_all_threads.at(th)) {
                std::rethrow_exception(exceptions_all_threads.at(th));
                return false;
            }
        }
    }
    else if(comp.type == ComparisonType::AllCloseHw){
        log_info(tt::LogVerif, "Performing All Close Check and PCC Check on Flattened Tensors");
        int num_tiles = 0;
        for(int i = 0; i < lhs.size(); i+= 32*32){
            pass &= allclose_on_vectors(lhs.begin() + i, rhs.begin() + i, comp.rtol, comp.atol, comp.check_pct, 32*32, total_pct, pct_mutex)
                            && pcc_compare_on_vectors(lhs.begin() + i, rhs.begin() + i, comp.rtol, comp.atol, comp.check_pcc, 32*32, total_pcc);
            num_tiles++;
        }

        total_pcc = total_pcc / num_tiles;
        total_pct = total_pct / num_tiles;
        log_info(tt::LogVerif, "Average pcc {}", total_pcc);
        log_info(tt::LogVerif, "Average allclose pct {}", total_pct);
    }

    else if(comp.type == ComparisonType::AllClose) {
        log_info(tt::LogVerif, "Performing All Close Check on Flattened Tensors");
        for(int th = 0; th < num_threads; th++){
            compare_threads.push_back(std::thread([th, &lhs, &rhs, num_threads, &pass, &comp, &total_pct, &exceptions_all_threads, &pass_mutex, &pct_mutex] {
                try{
                    bool result = allclose_on_vectors(lhs.begin() + th*lhs.size()/num_threads, rhs.begin() + th*rhs.size()/num_threads, comp.rtol, comp.atol, comp.check_pct, lhs.size()/num_threads, total_pct, pct_mutex);
                    std::lock_guard<std::mutex> lock(pass_mutex);
                    pass &= result;
                } catch(std::exception &e){
                    exceptions_all_threads.at(th) = std::current_exception();
                }
            }));
        }
        for (auto th = 0; th < num_threads; th++) {
            compare_threads.at(th).join();
        }
        for (auto th = 0; th < num_threads; th++) {
            if (exceptions_all_threads.at(th)) {
                std::rethrow_exception(exceptions_all_threads.at(th));
                return false;
            }
        }
        total_pct = total_pct / num_threads;
        log_info(tt::LogVerif, "Average allclose pct {}", total_pct);
    }
    else{
        log_fatal("ComparisonType not supported");
    }
    return pass;
}

bool compare_tensor_data(const tt_tensor &lhs, const tt_tensor &rhs, const VerifComparisonConfig &comp) {
    bool match = true;
    int n = 1;
    double avg_pct = 0.0;
    double avg_pcc = 0.0;
    for (int wi = 0; wi < lhs.getw(); ++wi) {
        for (int zi = 0; zi < lhs.getz(); ++zi) {
            for (int ri = 0; ri < lhs.getrt(); ++ri) {
                for (int ci = 0; ci < lhs.getct(); ++ci) {
                    bool tile_result = true;
                    double delta_pct = 0.0f;
                    double delta_pcc = 0.0f;
                    std::tuple<bool, double> allclose_result;
                    std::tuple<bool, double> pcc_result;
                    switch (comp.type) {
                        case ComparisonType::Exact:
                            tile_result = (lhs.tile_tensor[wi][zi][ri][ci] == rhs.tile_tensor[wi][zi][ri][ci]);
                            break;
                        case ComparisonType::AllCloseHw:
                            allclose_result = allclose(
                                lhs.tile_tensor[wi][zi][ri][ci],
                                rhs.tile_tensor[wi][zi][ri][ci],
                                comp.rtol,
                                comp.atol,
                                comp.check_pct,
                                comp.verbosity == ComparisonVerbosity::Verbose, 
                                comp.check_tile_rows_range, 
                                comp.check_tile_cols_range);
                            pcc_result = pcc_compare(
                                lhs.tile_tensor[wi][zi][ri][ci],
                                rhs.tile_tensor[wi][zi][ri][ci],
                                comp.rtol,
                                comp.atol,
                                comp.check_pcc, 
                                comp.check_tile_rows_range, 
                                comp.check_tile_cols_range);
                            tile_result = get<0>(allclose_result) and get<0>(pcc_result);
                            delta_pct = get<1>(allclose_result) - avg_pct;
                            avg_pct += delta_pct / n;
                            delta_pcc = get<1>(pcc_result) - avg_pcc;
                            avg_pcc += delta_pcc / n;
                            n++;
                            break;
                        case ComparisonType::AllClose:
                            allclose_result = allclose(
                                lhs.tile_tensor[wi][zi][ri][ci],
                                rhs.tile_tensor[wi][zi][ri][ci],
                                comp.rtol,
                                comp.atol,
                                comp.check_pct,
                                false,
                                comp.check_tile_rows_range,
                                comp.check_tile_cols_range);
                            tile_result = get<0>(allclose_result);
                            delta_pct = get<1>(allclose_result) - avg_pct;
                            avg_pct += delta_pct / n;
                            n++;
                            break;
                        default: log_fatal("ComparisonType not supported");
                    }
                    match &= tile_result;
                    if (!match) {
                        log_error("First Mismatched Tile for Tensor");
                        log_error(
                            "Tensor Shape [ci,ri,zi,wi] = [{},{},{},{}]",
                            lhs.getct(),
                            lhs.getrt(),
                            lhs.getz(),
                            lhs.getw());
                        log_error("Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_error(
                            "Diff\n{}",
                            lhs.tile_tensor[wi][zi][ri][ci].get_diff_string(
                                rhs.tile_tensor[wi][zi][ri][ci], comp.lhs_header, comp.rhs_header));
                        if (comp.type == ComparisonType::AllCloseHw) {
                            log_error("Average pcc = {}", avg_pcc);
                        }
                        log_error("Average allclose pct = {}", avg_pct);
                        if (comp.verbosity == ComparisonVerbosity::Concise) {
                            return match;
                        }
                    } else if (comp.verbosity == ComparisonVerbosity::Verbose) {
                        log_info(
                            tt::LogVerif,
                            "Tensor Shape [ci,ri,zi,wi] = [{},{},{},{}]",
                            lhs.getct(),
                            lhs.getrt(),
                            lhs.getz(),
                            lhs.getw());
                        log_info(tt::LogVerif, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_info(
                            tt::LogVerif,
                            "Diff\n{}",
                            lhs.tile_tensor[wi][zi][ri][ci].get_diff_string(
                                rhs.tile_tensor[wi][zi][ri][ci], comp.lhs_header, comp.rhs_header));
                    }
                }
            }
        }
    }
    if (comp.type == ComparisonType::AllCloseHw) {
        log_info(tt::LogVerif, "Average pcc = {}", avg_pcc);
    }
    log_info(tt::LogVerif, "Average allclose pct = {}", avg_pct);
    return match;
}

bool compare_tensors(vector<float>& lhs, vector<float>& rhs, const VerifComparisonConfig &comp, tt_dram_io_desc q_desc){
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    bool pass = true;
    if(!(comp.method == ComparisonMethod::TilizedTensor || comp.verbosity == ComparisonVerbosity::Verbose)){
        if(compare_flat_tensor_data(lhs, rhs, comp)){
            pass = true;
            log_info(tt::LogVerif, "Flat Tensors Passed Comparison Check for output={}", q_info.name);
            return true;
        }
        else{
            pass = false;
            log_error("Failed test on flat tensors for output={}", q_info.name);
        }
    }
    if(comp.method == ComparisonMethod::TilizedTensor || comp.verbosity == ComparisonVerbosity::Verbose || !pass){
        tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
        md.shape.w = q_info.input_count;
        log_info(tt::LogVerif, "Performing checks on tilized tensors");
        tt_tensor lhs_tensor(md), rhs_tensor(md);

        // construct and tilize tt tensors from flat vectors
        lhs_tensor.set_is_tilized(false);
        rhs_tensor.set_is_tilized(false);
        lhs_tensor.fill_with_data(lhs);
        rhs_tensor.fill_with_data(rhs);
        lhs_tensor.tilize_inplace(true, false, q_desc.layout == IO_LAYOUT::Flat);
        rhs_tensor.tilize_inplace(true, false, q_desc.layout == IO_LAYOUT::Flat);
        std::vector<tt_tensor> lhs_tensors = tensor_lib::split_merge_ops::wsplit(lhs_tensor, false);
        std::vector<tt_tensor> rhs_tensors = tensor_lib::split_merge_ops::wsplit(rhs_tensor, false);

        // compare each batch
        for(int b = 0; b < lhs_tensors.size(); b++){
            log_info(tt::LogTest, "Checking Entry idx={} for output={}", b, q_info.name);
            if(not compare_tensors(lhs_tensors.at(b), rhs_tensors.at(b), comp)){
                log_error("Entry idx={} for output={} Mismatched", b, q_info.name);
                pass = false;
            }
        }
    }
    return pass;
}

bool compare_tensors_exact(const tt_tensor &lhs, const tt_tensor &rhs) {
    bool match = true;
    for (int wi = 0; wi < lhs.getw(); ++wi) {
        for (int zi = 0; zi < lhs.getz(); ++zi) {
            for (int ri = 0; ri < lhs.getrt(); ++ri) {
                for (int ci = 0; ci < lhs.getct(); ++ci) {
                    match &= (lhs.tile_tensor[wi][zi][ri][ci] == rhs.tile_tensor[wi][zi][ri][ci]);
                }
            }
        }
    }
    return match;
}

//! Function to compare tensors and verify.
bool compare_tensors(const tt_tensor &lhs, const tt_tensor &rhs, const VerifComparisonConfig &comp) {
    bool match = true;
    bool check_dims_only = false;
    tt_op *op = lhs.get_producer_op_ptr()   ? lhs.get_producer_op_ptr()
                : rhs.get_producer_op_ptr() ? rhs.get_producer_op_ptr()
                                            : nullptr;
    if (op) {
        log_debug(tt::LogVerif, "Producer Op = {}, Name = {}", op->type, op->name);
        check_dims_only = (comp.check_dims_only_types.find(op->type) != comp.check_dims_only_types.end()) or
                          (comp.check_dims_only_names.find(op->name) != comp.check_dims_only_names.end());
    }
    // Base Case where dims mismatch
    match &= are_tensor_dims_equal(lhs, rhs);
    if (not match) {
        log_error("Dims Mismatch -- lhs:{} rhs:{}", lhs.get_shape(), rhs.get_shape());
        return match;
    } else if (check_dims_only) {
        return match;
    }
    if (comp.verbosity == ComparisonVerbosity::Concise) {
        log_info(tt::LogVerif, "Concise Verbosity Comparison");
    } else if (comp.verbosity == ComparisonVerbosity::AllFails) {
        log_info(tt::LogVerif, "AllFails Verbosity Comparison");
    } else if (comp.verbosity == ComparisonVerbosity::Verbose) {
        log_info(tt::LogVerif, "Verbose Verbosity Comparison");
    }
    match &= compare_tensor_data(lhs, rhs, comp);
    return match;
}

VerifComparisonConfig read_from_yaml_node(const YAML::Node &node) {
    if (not node.IsMap()) {
        log_fatal("VerifComparisonConfig expected yaml node have to be a map");
    }
    VerifComparisonConfig config;
    for (auto it : node) {
        std::string key = it.first.as<std::string>();
        if (key == "verbosity") {
            config.verbosity = get_comparison_verbosity(it.second.as<std::string>());
        
        } else if (key == "atol") {
            config.atol = it.second.as<double>();
        } else if (key == "rtol") {
            config.rtol = it.second.as<double>();
        } else if (key == "check_pct") {
            config.check_pct = it.second.as<double>();
        } else if (key == "check_pcc") {
            config.check_pcc = it.second.as<double>();
        } else if (key == "type") {
            config.type = get_comparison_type(it.second.as<std::string>());
        } else if(key == "method") {
            config.method = get_comparison_method(it.second.as<std::string>());
        } else if (key == "check_tile_rows_range") {
            const vector<int> row_range = it.second.as<std::vector<int>>();
            log_assert(
                row_range.size() == 2 && row_range[0] >= 1 && row_range[0] <= row_range[1] &&
                    row_range[1] <= tt::constants::TILE_HEIGHT,
                "comparison filtered_tile_rows_range has to be in format [r1, r2] (1-based inclusive range)");
            config.check_tile_rows_range = {row_range[0] - 1, row_range[1] - 1};
        } else if (key == "check_tile_cols_range") {
            const vector<int> col_range = it.second.as<std::vector<int>>();
            log_assert(
                col_range.size() == 2 && col_range[0] >= 1 && col_range[0] <= col_range[1] &&
                    col_range[1] <= tt::constants::TILE_WIDTH,
                "comparison filtered_tile_cols_range has to be in format [c1, c2] (1-based inclusive range)");
            config.check_tile_cols_range = {col_range[0] - 1, col_range[1] - 1};
        } else if (key == "lhs_header") {
            config.lhs_header = it.second.as<std::string>();
        } else if (key == "rhs_header") {
            config.rhs_header = it.second.as<std::string>();
        } else if (key == "overrides") {
        } else {
            log_fatal("Unsupported key in config parse: {}", key);
        }
    }
    return config;
}

VerifComparisonConfig read_from_yaml_file(const std::string &filepath) {
    return verif::test_config::read_config_from_yaml_file<VerifComparisonConfig>(
        filepath, "comparison-config", verif::comparison::read_from_yaml_node);
}

std::unordered_map<std::string, VerifComparisonConfig> read_configs_map_from_yaml_file(const std::string &filepath) {
    return verif::test_config::read_configs_map_from_yaml_file<VerifComparisonConfig>(
        filepath, "comparison-config", verif::comparison::read_from_yaml_node);
}

std::unordered_map<std::string, VerifComparisonConfig> read_configs_map_with_defaults(
    const std::string &filepath, const std::string &default_filepath) {
    return verif::test_config::read_configs_map_with_defaults<VerifComparisonConfig>(
        filepath, default_filepath, "comparison-config", verif::comparison::read_from_yaml_node);
}

//! Returns a default config if key doesn't exist
VerifComparisonConfig get_config(
    const std::string key, const std::unordered_map<std::string, VerifComparisonConfig> &configs) {
    return verif::test_config::get_config_from_map<VerifComparisonConfig>(key, configs);
}
}  // namespace verif::comparison