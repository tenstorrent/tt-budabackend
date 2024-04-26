// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_list.h"

#include <chrono>
#include <ctime>
#include <exception>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "eltwise_binary/test.h"
#include "eltwise_nary/test.h"
#include "eltwise_unary/test.h"
#include "hlkc_test/test.h"
#include "matmul/test.h"
#include "matmul_large/test.h"
#include "matmul_large_add/test.h"
#include "multi_in/test.h"
#include "multi_in_acc/test.h"
#include "multi_out/test.h"
#include "reduce/test.h"
#include "tilize/test.h"
#include "untilize/test.h"
#include "sfpi/test.h"

using namespace tests;

std::unordered_map<std::string, LLK_TEST_TYPE> tests::LLK_TEST_TYPE_NAME{
    {"HLKC_TEST", LLK_TEST_TYPE::HLKC_TEST},
    {"ELTWISE_UNARY", LLK_TEST_TYPE::ELTWISE_UNARY},
    {"ELTWISE_BINARY", LLK_TEST_TYPE::ELTWISE_BINARY},
    {"ELTWISE_NARY", LLK_TEST_TYPE::ELTWISE_NARY},
    {"MATMUL", LLK_TEST_TYPE::MATMUL},
    {"MATMUL_LARGE", LLK_TEST_TYPE::MATMUL_LARGE},
    {"MATMUL_LARGE_ADD", LLK_TEST_TYPE::MATMUL_LARGE_ADD},
    {"REDUCE", LLK_TEST_TYPE::REDUCE},
    {"MULTI_OUT", LLK_TEST_TYPE::MULTI_OUT},
    {"MULTI_IN", LLK_TEST_TYPE::MULTI_IN},
    {"MULTI_IN_ACC", LLK_TEST_TYPE::MULTI_IN_ACC},
    {"TILIZE", LLK_TEST_TYPE::TILIZE},
    {"UNTILIZE", LLK_TEST_TYPE::UNTILIZE},
    {"SFPI", LLK_TEST_TYPE::SFPI},
};

std::vector<std::string> tests::get_all_tests() {
    std::vector<std::string> retval;
    for (auto const &element : LLK_TEST_TYPE_NAME) {
        retval.push_back(element.first);
    }
    return retval;
}

bool tests::test_main(std::string test_name, tests::TestArgs args) {
    auto t1 = std::chrono::high_resolution_clock::now();

    std::string test_descriptor = args.test_descriptor;
    std::string lower_test_name = test_name;
    transform(lower_test_name.begin(), lower_test_name.end(), lower_test_name.begin(), ::tolower);
    std::string family_test_descriptor =
        args.gitroot + "/tb/llk_tb/tests/" + lower_test_name + "/test_descriptors/" + args.test_descriptor;
    if (not std::experimental::filesystem::exists(test_descriptor)) {
        test_descriptor = family_test_descriptor;
    }
    if (not std::experimental::filesystem::exists(test_descriptor)) {
        throw std::runtime_error("Can't find the test_descriptor=" + test_descriptor);
    }
    args.test_descriptor = test_descriptor;
    bool result = true;
    // Initial seeding of test
    if (not args.force_seed) {
        args.seed = chrono::system_clock::now().time_since_epoch().count();
    }
    cout << "Initial Seed: " << args.seed << endl;
    srand(args.seed);  // seed c++ randomization engine

    std::map<string, bool> test_results;
    std::map<string, bool> passed_test_results;
    std::map<string, bool> failed_test_results;

    // Run the specific test family.
    if (LLK_TEST_TYPE_NAME.find(test_name) == LLK_TEST_TYPE_NAME.end()) {
        std::cout << "LLK_TEST_TYPE: " << test_name << " is not supported \n";
        throw;
    } else {
        if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::ELTWISE_UNARY) {
            test_results = eltwise_unary::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::ELTWISE_BINARY) {
            test_results = eltwise_binary::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::ELTWISE_NARY) {
            test_results = eltwise_nary::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MATMUL) {
            test_results = matmul::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MATMUL_LARGE) {
            test_results = matmul_large::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MATMUL_LARGE_ADD) {
            test_results = matmul_large_add::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::HLKC_TEST) {
            test_results = hlkc_test::test_main(args);  // TODO: Make test follow new test flow
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::REDUCE) {
            test_results = reduce::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MULTI_OUT) {
            test_results = multi_out::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MULTI_IN) {
            test_results = multi_in::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::MULTI_IN_ACC) {
            test_results = multi_in_acc::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::TILIZE) {
            test_results = tilize::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::UNTILIZE) {
            test_results = untilize::test_main(args);
        } else if (LLK_TEST_TYPE_NAME[test_name] == LLK_TEST_TYPE::SFPI) {
            test_results = sfpi::test_main(args);
        } else {
            std::cout << "LLK_TEST_TYPE: " << test_name << " test_main is not added to test_list.h \n";
            throw;
        }
    }
    // Final Output Summary
    int num_passed = 0;
    std::vector<std::string> headers;
    std::string coverage_path;
    std::ofstream coveragefile;
    if (args.regression_mode) {
        if (not std::experimental::filesystem::exists(args.coverage_dir)) {
            std::cout << "WARNING: Cannot find coverage dir=" << args.coverage_dir << ", saving in working dir"
                      << std::endl;
            coverage_path = ".";
        } else {
            coverage_path = args.coverage_dir;
        }
        auto t1_time_t = std::chrono::system_clock::to_time_t(t1);
        std::stringstream date_time_ss;
        date_time_ss << std::put_time(std::localtime(&t1_time_t), "%Y_%m_%d_%H_%M");
        coverage_path = coverage_path + "/" + lower_test_name + "_" + date_time_ss.str() + "_coverage.txt";
        std::cout << "Saving coverage to:" << coverage_path << std::endl;
        coveragefile.open(coverage_path);
    }
    for (auto const &it : test_results) {
        result &= it.second;
        num_passed += it.second ? 1 : 0;
        if (it.second) {
            passed_test_results[it.first] = it.second;
        } else {
            failed_test_results[it.first] = it.second;
        }
        if (headers.empty()) {
            headers = tests::test_config_api::get_keys_from_summary_string(it.first);
            std::string headers_string;
            for (auto &header : headers) {
                headers_string = headers_string + header + " ";
            }
            headers_string = headers_string + "results\n";
            if (coveragefile.is_open()) {
                coveragefile << headers_string;
            }
        }
        if (headers.size() > 0) {
            auto values = tests::test_config_api::get_values_from_summary_string(it.first, headers);
            std::string values_string;
            for (auto &value : values) {
                values_string = values_string + value + " ";
            }
            values_string = values_string + (it.second ? "PASSED" : "FAILED") + "\n";
            if (coveragefile.is_open()) {
                coveragefile << values_string;
            }
        }
    }

    for (auto const &it : passed_test_results) {
        cout << "Config[" << it.first << "] = PASSED" << endl;
    }
    for (auto const &it : failed_test_results) {
        cout << "Config[" << it.first << "] = FAILED" << endl;
    }
    float percent_passed = 100.0f * (float)num_passed / (float)test_results.size();
    printf("Pass Rate: %3.3f%% %0d / %0lu Tests --", percent_passed, num_passed, test_results.size());
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
    double ms_double = ms_int.count() / 1000.f;
    std::cout << "Time Elapsed: " << ms_double << " s" << std::endl;
    return result;
}
