// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <glog/logging.h>
#include <tclap/CmdLine.h>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "tests.h"

namespace {
const std::string kLlkTestVersion = "0.1";
}
using namespace TCLAP;
int main(int argc, char **argv, char **env) {
    TCLAP::CmdLine cmd("llk-test version", '=', kLlkTestVersion);
    auto root_env = std::getenv("ROOT");
    std::vector<std::string> dump_cores;
    std::string device = std::string("grayskull");
    std::string test_descriptor = std::string("default.yaml");
    bool dump_tensors = false;
    bool regression_mode = false;
    bool force_seed = false;
    int jobs = 1;
    std::string coverage_dir = ".";
    int seed = 0;
    int num_configs_to_randomize = 5;
    // Give root default path, similar to buda compile #2293
    std::string gitroot = std::filesystem::current_path().string();
    if (root_env != NULL) {
        gitroot = root_env;
    }
    std::vector<std::string> tests;
    bool perf_dump = false;

    try {
        ValueArg<std::string> gitroot_arg(
            "",          // The one character flag that identifies this argument on the command line.
            "git-root",  // A one word name for the argument. Can be used as a long flag on the command line.
            "Root directory of git repo",  // A description of what the argument is for or does.
            false,                         // Whether the argument is required on the command line.
            "",       // The default value assigned to this argument if it is not present on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(gitroot_arg);
        MultiArg<std::string> tests_arg(
            "t",     // The one character flag that identifies this argument on the command line.
            "test",  // A one word name for the argument. Can be used as a long flag on the command line.
            "test to run, multiple can be given",  // A description of what the argument is for or does.
            false,                                 // Whether the argument is required on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(tests_arg);
        ValueArg<std::string> device_arg(
            "d",                 // The one character flag that identifies this argument on the command line.
            "device",            // A one word name for the argument. Can be used as a long flag on the command line.
            "device to target",  // A description of what the argument is for or does.
            false,               // Whether the argument is required on the command line.
            "",       // The default value assigned to this argument if it is not present on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(device_arg);
        ValueArg<std::string> test_descriptor_arg(
            "",           // The one character flag that identifies this argument on the command line.
            "test-yaml",  // A one word name for the argument. Can be used as a long flag on the command line.
            "filename of the test descriptor",  // A description of what the argument is for or does.
            false,                              // Whether the argument is required on the command line.
            "",       // The default value assigned to this argument if it is not present on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(test_descriptor_arg);
        ValueArg<std::string> coverage_dir_arg(
            "",              // The one character flag that identifies this argument on the command line.
            "coverage-dir",  // A one word name for the argument. Can be used as a long flag on the command line.
            "output folder of coverage file",  // A description of what the argument is for or does.
            false,                             // Whether the argument is required on the command line.
            "",       // The default value assigned to this argument if it is not present on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(coverage_dir_arg);
        MultiArg<std::string> dump_cores_arg(
            "c",           // The one character flag that identifies this argument on the command line.
            "dump-cores",  // A one word name for the argument. Can be used as a long flag on the command line.
            "dump cores specified string formatted as such for noc_node \"x-y\"",  // A description of what the argument
                                                                                   // is for or does.
            false,    // Whether the argument is required on the command line.
            "string"  // A short, human readable description of the type that this object expects. This is used in the
                      // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(dump_cores_arg);
        SwitchArg dump_tensors_arg(
            "",              // The one character flag that identifies this argument on the command line.
            "dump-tensors",  // A one word name for the argument. Can be used as a long flag on the command line.
            "dump tensors into tensor_dumps/ folder -- mainly for debugging",  // A description of what the argument is
                                                                               // for or does.
            false                                                              // Default
        );
        cmd.add(dump_tensors_arg);
        SwitchArg regression_mode_arg(
            "r",                // The one character flag that identifies this argument on the command line.
            "regression-mode",  // A one word name for the argument. Can be used as a long flag on the command line.
            "run regression mode for specified family of test",  // A description of what the argument is
                                                                 // for or does.
            false                                                // Default
        );
        cmd.add(regression_mode_arg);
        ValueArg<std::string> jobs_arg(
            "j",              // The one character flag that identifies this argument on the command line.
            "jobs",           // A one word name for the argument. Can be used as a long flag on the command line.
            "seed for test",  // A description of what the argument is for or does.
            false,            // Whether the argument is required on the command line.
            "1",              // The default value assigned to this argument if it is not present on the command line.
            "int"  // A short, human readable description of the type that this object expects. This is used in the
                   // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(jobs_arg);
        ValueArg<std::string> seed_arg(
            "s",              // The one character flag that identifies this argument on the command line.
            "seed",           // A one word name for the argument. Can be used as a long flag on the command line.
            "seed for test",  // A description of what the argument is for or does.
            false,            // Whether the argument is required on the command line.
            "",               // The default value assigned to this argument if it is not present on the command line.
            "int"  // A short, human readable description of the type that this object expects. This is used in the
                   // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(seed_arg);
        ValueArg<std::string> num_regression_tests_arg(
            "",                      // The one character flag that identifies this argument on the command line.
            "num-regression-tests",  // A one word name for the argument. Can be used as a long flag on the command
                                     // line.
            "Number of test configs to run in regression mode",  // A description of what the argument is for or does.
            false,  // Whether the argument is required on the command line.
            "",     // The default value assigned to this argument if it is not present on the command line.
            "int"   // A short, human readable description of the type that this object expects. This is used in the
                    // generation of the USAGE statement. The goal is to be helpful to the end user of the program.
        );
        cmd.add(num_regression_tests_arg);
        SwitchArg perf_dump_arg(
            "",           // The one character flag that identifies this argument on the command line.
            "perf-dump",  // A one word name for the argument. Can be used as a long flag on the command line.
            "enable performance data dump into pre-allocated buffers in l1",  // A description of what the argument is
                                                                              // for or does.
            false                                                             // Default
        );
        cmd.add(perf_dump_arg);
        // Parse the args.
        cmd.parse(argc, argv);

        // Assign the parsed arguments
        if (gitroot.empty() and gitroot_arg.isSet()) {
            gitroot = gitroot_arg.getValue();
        } else if (gitroot.empty()) {
            LOG(ERROR) << "ROOT env var is not set, and git root arg not passed in";
            return 1;
        }
        if (device_arg.isSet()) {
            device = device_arg.getValue();
        }
        if (coverage_dir_arg.isSet()) {
            coverage_dir = coverage_dir_arg.getValue();
        }
        if (dump_cores_arg.isSet()) {
            dump_cores = dump_cores_arg.getValue();
        }
        if (dump_tensors_arg.isSet()) {
            dump_tensors = dump_tensors_arg.getValue();
        }
        if (regression_mode_arg.isSet()) {
            regression_mode = regression_mode_arg.getValue();
        }
        if (seed_arg.isSet()) {
            force_seed = true;
            seed = stol(seed_arg.getValue());
        }
        if (jobs_arg.isSet()) {
            jobs = stol(jobs_arg.getValue());
        }
        if (num_regression_tests_arg.isSet()) {
            num_configs_to_randomize = stol(num_regression_tests_arg.getValue());
        }
        if (tests_arg.isSet()) {
            tests = tests_arg.getValue();
        }
        if (test_descriptor_arg.isSet()) {
            test_descriptor = test_descriptor_arg.getValue();
        }
        if (perf_dump_arg.isSet()) {
            perf_dump = perf_dump_arg.getValue();
        }
    } catch (ArgException &e) {  // catch any exceptions
        std::cout << "CMD ERROR: " << e.error().c_str() << " for arg " << e.argId().c_str();
        return 1;
    } catch (...) {
        std::cout << "CMD ERROR: Unknown exception while processing command line arguments\n";
        return 1;
    }

    google::InitGoogleLogging(argv[0]);

    tests::TestArgs global_args = {
        .device = device,
        .gitroot = gitroot,
        .dump_tensors = dump_tensors,
        .plusargs = {"+pack_chkr_disable=1", "+t6_os_mdl_enable=1"},
        .dump_cores = dump_cores,
        .test_descriptor = test_descriptor,
        .coverage_dir = coverage_dir,
        .regression_mode = regression_mode,
        .num_configs_to_randomize = num_configs_to_randomize,
        .force_seed = force_seed,
        .seed = seed,
        .jobs = jobs,
        .perf_dump = perf_dump};

    for (auto &test : tests) {
        std::transform(test.begin(), test.end(), test.begin(), ::toupper);
        assert(tests::test_main(test, global_args) && "Test has failures");
    }
}
