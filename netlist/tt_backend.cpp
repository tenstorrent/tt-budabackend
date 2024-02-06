// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_backend.hpp"

//! EXPLICITLY TAG THE EXACT PATH TO INCL FILE
//! We don't want to include the whole folder of golden/model2/loader in netlist
//! Must explicitly tag includes for any inherited devices
#include "golden/tt_golden.hpp"
#include "runtime/runtime.hpp"
#include "common/cache_lib.hpp"

using namespace tt;
using namespace tt::golden;

tt_backend::~tt_backend() { tt_object_cache<tt_backend_config>::clear(netlist_path); };

tt::DEVICE_STATUS_CODE tt_backend::initialize(tt::tt_compile_result *result) {
    if (config.type != tt::DEVICE::Silicon) {
        return this->initialize();
    }
    log_fatal("initialize() must be overridden for this device");
    return tt::DEVICE_STATUS_CODE::RuntimeError;
}

tt::DEVICE_STATUS_CODE tt_backend::memory_barrier(tt::MemBarType barrier_type, int chip, const std::unordered_set<tt_xy_pair>& cores) {
    if (config.type == tt::DEVICE::Silicon) {
        log_fatal("memory_barrier() must be overridden for this device");
    }
    return tt::DEVICE_STATUS_CODE::RuntimeError;
}

tt::DEVICE_STATUS_CODE tt_backend::compile_netlist(const std::string &netlist_path) {
    log_warning(tt::LogNetlist, "compile_netlist() not implemented for this device");
    return tt::DEVICE_STATUS_CODE::RuntimeError;
}

tt::DEVICE_STATUS_CODE tt_backend::compile_and_run_netlist(const std::string &netlist_path, const std::map<std::string, std::string> &parameters) {
    log_warning(tt::LogNetlist, "compile_and_run_netlist() not implemented for this device");
    return tt::DEVICE_STATUS_CODE::RuntimeError;
}

std::shared_ptr<tt_backend> tt_backend::create(const std::shared_ptr<tt::tt_device_image> tti, const tt::tt_backend_config &config) {
    tt::tt_backend_config model_config(config);
    model_config.tti = tti;
    model_config.arch = tt::get_arch_from_string(tti->arch());
    model_config.type = config.type == DEVICE::Invalid ? tt::get_device_from_string(tti->backend()) : config.type;
    model_config.mode = DEVICE_MODE::RunOnly;
    // We currently assume that the netlist will be placed in <out_dir>/backend_build_binaries
    // The raw netlist path in the device.json file curently contains some extra prefixes that make it invalid
    // Extract netlist name from path and infer its location based on this assumption 
    model_config.output_dir = tti->get_model_path("backend_build_binaries");
    return create(tti -> get_netlist_path(), model_config);
}

std::shared_ptr<tt_backend> tt_backend::create(const std::string &netlist_path, const tt::tt_backend_config &config) {
    std::shared_ptr<tt_backend> backend;
    switch (config.type) {
        case tt::DEVICE::Golden:
            backend = std::make_shared<tt_golden>(netlist_path, get_golden_config(config));
            backend->set_config(config);
            break;
        case tt::DEVICE::Versim:
        case tt::DEVICE::Silicon: 
            backend = std::make_shared<tt_runtime>(get_runtime_config(config, netlist_path)); 
            break;
        default: 
            log_fatal("Not Supported Yet"); 
            break;
    }
    backend->netlist_path = netlist_path;
    return backend;
};

std::shared_ptr<tt_backend> tt_backend::create_eager_backend(const tt::tt_backend_config &config, const std::set<int> &target_devices) {
    std::shared_ptr<tt_backend> backend;
    switch (config.type) {
        case tt::DEVICE::Versim:
        case tt::DEVICE::Silicon: backend = std::make_shared<tt_runtime>(get_runtime_config(config, ""), target_devices); break;
        default: log_fatal("Not Supported Yet"); break;
    }
    return backend;
}
