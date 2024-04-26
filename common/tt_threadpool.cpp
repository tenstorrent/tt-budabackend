// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_threadpool.hpp"

void TT_ThreadPool::loop() {
    //bind each sub thread to a single slot (L3 cache)
    {
        std::unique_lock<std::mutex> lock(core_mutex);
        if(ndesc_ != nullptr) {
            // Bind thread if multi-threading is actually performed and if the network
            // descriptor is passed in for the CPU Allocator
            tt::cpuset::tt_cpuset_allocator::bind_thread_to_cpuset(ndesc_, chip_id_, true);
        }
    }

    while(true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(job_mutex);
            job_cv.wait(lock, [this] { return stop_flag || !jobs.empty();});
            if(stop_flag) return;
            free_mutex.lock();
            num_free--;
            free_mutex.unlock();
            job = jobs.front();
            jobs.pop();
        }
        job();
        std::unique_lock<std::mutex> lock(free_mutex);
        num_free++;
    }
}

void TT_ThreadPool::start(std::uint32_t num_threads, tt_cluster_description* ndesc, chip_id_t chip_id) {
    ndesc_ = ndesc;
    chip_id_ = chip_id;
    num_threads_ = num_threads;
    num_free = num_threads;
    stop_flag = false;
    if(num_threads > 1) {
        for(uint32_t i = 0; i < num_threads; i++){
            threads.push_back(std::thread(&TT_ThreadPool::loop, this));
        }
    }
}

void TT_ThreadPool::queue_job(std::function<void()> push_func) {
    {
        std::unique_lock<std::mutex> lock(job_mutex);
        jobs.push(push_func);
    }
    job_cv.notify_one();
}

void TT_ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(job_mutex);
        stop_flag = true;
    }
    job_cv.notify_all();

    for(std::thread& active_thread : threads) {
        active_thread.join();
    }
    threads.clear();
}

bool TT_ThreadPool::wait() {
    bool in_use = false;
    {
        std::unique_lock<std::mutex> lock(job_mutex);
        in_use = !jobs.empty();
    }
    return in_use;
}