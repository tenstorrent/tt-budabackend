// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#include "common/tti_lib.hpp"
#include "tt_backend_api_types.hpp"
/**
 * @file
 * This file contains functions related to backend Runtime API
 */

//! Base Backend which defines the spec that all backend devices should follow
class tt_backend {
   public:
    virtual ~tt_backend();

    /**
     * @brief Set Config
     * For advanced users only to set the backend config. If not called, the config provided during create() is used.
     * Depending on the time of call, some configs may not take effect
     * \param config Backend config that will be used to override the existing config
     */
    void set_config(const tt::tt_backend_config &config) { this->config = config; };
    const tt::tt_backend_config &get_config() { return config; };

    /**
     * @brief Initialize
     * This function is called to initialize the backend device. Must be called once and only once before any running of programs
     * Triggers overlay & firmware compile if requested is done in this step, skipped for running precompiled models.
     * Sets device power states and deasserts resets. Initializes any queue/ramd states and optionally loads model weights to device.
     */
    virtual tt::DEVICE_STATUS_CODE initialize() = 0;
    virtual tt::DEVICE_STATUS_CODE initialize(tt::tt_compile_result *result);

    //! Finish - Must be called once and only once after all programs are run.  Clean up
    /**
     * @brief Finish
     * This function is called to tear down the backend device. Must be called once and only once when we're ready to shutdown the device
     * Triggers performance dumps if performance events are enabled during compile and run.
     * Sends END command to device to stop all running firmware. Waits for all programs to finish and starts shutdown sequence.
     */
    virtual tt::DEVICE_STATUS_CODE finish() = 0;

    /**
     * @brief Run Program
     * This function is called to schedule a program to device.
     * It may or may not be blocking depending on the backend optimization levels configured in the backend config.
     * - At OPT_LEVEL 0, it is blocking and returns only after the program is finished, it is the safest and slowest option
     * - At OPT_LEVEL 4, it is non-blocking and returns after commands are scheduled to device epoch command queues, it is the most performant option
     * \param program_name Name of the program to run, each program is a collection of instructions and graphs to run
     * \param parameters Map of parameters to pass to the program, required for programs that take parameters
     * such as loop count
     */
    virtual tt::DEVICE_STATUS_CODE run_program(
        const std::string &program_name, const std::map<std::string, std::string> &parameters) = 0;

    /**
     * @brief Wait-for-idle (WFI)
     * This function is called to block until all launched work are drained via polling device state.
     * Device is idle upon WFI's return and memory operations are guaranteed to have reached global visibility
     */
    virtual tt::DEVICE_STATUS_CODE wait_for_idle() = 0;

    /**
     * @brief Memory Barrier (MEMBAR)
     * This function will issue a MEMBAR of type barrier_type to ensure that loads or stores are globally visible
     * within the scope of the barrier.
     * \param barrier_type Specifies the scope of the memory barrier.
     * \param chip Specifies the chip participating in the barrier sync. Memory Barrier will only be sent to this chip
     * (unless its a host_cluster sync, in which case all chips will be involved)
     * \param cores Set of cores participating in the sync. Memory operations preceeding the barrier are only guaranteed
     * to be globally visible to these cores (unless its a host_cluster sync, in which case all cores will be involved).
     */ 
    virtual tt::DEVICE_STATUS_CODE memory_barrier(tt::MemBarType barrier_type, int chip, const std::unordered_set<tt_xy_pair>& cores = {}) = 0;

    /**
     * @brief Compile Netlist (Eager Mode)
     * This function is called to incrementally compile a netlist. 
     * It should be used by eager mode frontend execution only, NOT graph mode or pre-compiled model runs
     * \param netlist_path Path to the netlist file
     */
    virtual tt::DEVICE_STATUS_CODE compile_netlist(const std::string &netlist_path);

    /**
     * @brief Compile and Run Netlist (Eager Mode)
     * This function is called to incrementally compile a netlist and run all of the programs in the netlist.
     * It should be used by eager mode frontend execution only, NOT graph mode or pre-compiled model runs
     * \param netlist_path Path to the netlist file
     * \param parameters Map of parameters to pass to the programs, required for programs that take parameters
     */
    virtual tt::DEVICE_STATUS_CODE compile_and_run_netlist(const std::string &netlist_path, const std::map<std::string, std::string> &parameters);

    /**
     * @brief Queue Descriptor Query
     * This function is called to query the queue/ram descriptor for a given queue name in the netlist
     * The returned object can be used by IO APIs to read/write to the queue/ram
     * \param queue_name Name of the queue to query
     * @return queue descriptor for the given queue name
     */
    virtual const tt::tt_dram_io_desc get_queue_descriptor(const std::string &queue_name) = 0;

    /**
     * @brief Creation Method (Graph Mode)
     * Creation Function for the backend class shared pointer from netlist
     * \param netlist_path Path to the netlist file
     * \param config Backend config
     */
    static std::shared_ptr<tt_backend> create(const std::string &netlist_path, const tt::tt_backend_config &config);

    /**
     * @brief Creation Method (Graph Mode)
     * Creation Function for the backend class shared pointer from TTI
     * \param tti TTI object
     * \param config Backend config
     */
    static std::shared_ptr<tt_backend> create(const std::shared_ptr<tt::tt_device_image> tti, const tt::tt_backend_config &config);

    /**
     * @brief Creation Method (Eager Mode)
     * Creation Function for a backend class shared pointer without a target workload
     * Eager exeuction will feed the backend with netlists to be incrementally compiled and run
     * \param config Backend config
     * \param target_devices Cluster of devices allocated to this instance of backend
     */
    static std::shared_ptr<tt_backend> create_eager_backend(const tt::tt_backend_config &config, const std::set<int> &target_devices);

   protected:
    //! configuration
    tt::tt_backend_config config;
    std::string netlist_path;
};
