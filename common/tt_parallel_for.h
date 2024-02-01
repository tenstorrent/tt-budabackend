// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <thread>
#include <algorithm>
#include <exception>
#include <numeric>

namespace tt {
    // Utility function to parallelize a for loop using std::thread.
    template <typename Iterator, typename Function>
    void parallel_for(Iterator begin, Iterator end, Function func, int num_threads) {
        int total_work = std::distance(begin, end);
        int work_per_thread = (total_work + num_threads - 1) / num_threads;

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        std::vector<std::exception_ptr> exceptions_all_threads(num_threads, nullptr);

        Iterator chunk_start = begin;
        for (int t = 0; t < num_threads && t < total_work; ++t) {
            Iterator chunk_end = std::next(chunk_start, std::min(work_per_thread, std::max(0, total_work - t * work_per_thread)));
            threads.emplace_back([=, &exceptions_all_threads, &func]() {
                try {
                    std::for_each(chunk_start, chunk_end, func);
                } catch (std::exception& e) {
                    exceptions_all_threads[t] = std::current_exception();
                }
            });
            chunk_start = chunk_end;
        }

        for (std::thread& thread : threads) {
            thread.join();
        }

        for (const auto& exception : exceptions_all_threads) {
            if (exception) {
                std::rethrow_exception(exception);
                return;
            }
        }
    }

    template <typename Function>
    void parallel_for(int start_index, int end_index, Function func, int num_threads) {
        std::vector<int> indices(end_index - start_index);
        std::iota(indices.begin(), indices.end(), start_index);
        parallel_for(indices.begin(), indices.end(), func, num_threads);
    }
} // namespace tt