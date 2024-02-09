// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include "loader/tt_cluster.hpp"
#include "device/cpuset_lib.hpp"

class TT_ThreadPool{
    public:
        void start(std::uint32_t num_threads, tt_cluster_description* ndesc = nullptr, chip_id_t chip_id = 0);
        void queue_job(std::function<void()> push_func);
        void stop();
        bool wait();
        uint32_t num_free = 0;
        uint32_t num_threads_ = 0;
        
    private:
        void loop();
        bool stop_flag = false;
        std::mutex job_mutex;
        std::mutex free_mutex;
        std::mutex core_mutex;
        std::condition_variable job_cv;
        tt_cluster_description* ndesc_;
        std::vector<std::thread> threads;
        std::queue<std::function<void()>> jobs;
        chip_id_t chip_id_;
        
};