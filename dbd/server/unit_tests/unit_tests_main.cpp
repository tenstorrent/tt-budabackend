// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "utils/gtest_initializer.hpp"

// This is a custom listener that prints failed test parts to stderr so they show up in the console output.
class CustomTestListener : public ::testing::EmptyTestEventListener {
   protected:
    virtual void OnTestEnd(const ::testing::TestInfo& test_info) override {
        if (test_info.result()->Failed()) {
            // Here you can iterate over failed test parts if needed
            for (int i = 0; i < test_info.result()->total_part_count(); ++i) {
                const auto& part = test_info.result()->GetTestPartResult(i);
                if (part.failed()) {
                    std::cerr << "---- Failure in " << test_info.test_case_name() << "." << test_info.name()
                              << std::endl
                              << " " << part.file_name() << ":" << part.line_number() << std::endl
                              << part.summary() << std::endl;
                }
            }
        }
    }
};

int main(int argc, char** argv) {
    setenv("LOGGER_LEVEL", "Disabled", false /* overwrite */);

    initialize_gtest(argc, argv);

    // Get the default listener and unregister it
    ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
    delete listeners.Release(listeners.default_result_printer());

    // Add our custom listener
    listeners.Append(new CustomTestListener());

    return RUN_ALL_TESTS();
}