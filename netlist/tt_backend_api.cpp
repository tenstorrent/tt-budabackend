// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_backend_api.hpp"

#include <algorithm>

#include "common/buda_soc_descriptor.h"
#include "common/cache_lib.hpp"
#include "common/error_types.hpp"
#include "common/mem_lib.hpp"
#include "compile_trisc/compile_trisc.hpp"
#include "golden/golden_io.hpp"
#include "netlist_utils.hpp"
#include "runtime/runtime_io.hpp"
#include "common/size_lib.hpp"
#include "utils/scoped_timer.hpp"
#include "device/cpuset_lib.hpp"
#include "op_model/op_model.hpp"

extern perf::tt_backend_perf backend_profiler;
namespace tt::backend {

tt::DEVICE_STATUS_CODE push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_TilizedTensorDesc &tilized_tensor_desc,
    const int timeout_in_seconds,
    const int ptr) {
    try {
        if (q_desc.io_type == tt::IO_TYPE::RandomAccess) {
            log_assert(ptr >= 0, "push_input in tt_backend_api.cpp Random access must specify a valid ptr");
        }
        switch (q_desc.backend_type) {
            case tt::DEVICE::Versim:
            case tt::DEVICE::Silicon:
                tt::io::push_input_to_device(q_desc, tilized_tensor_desc, timeout_in_seconds, ptr);
                break;
            case tt::DEVICE::Golden:
                tt::golden::io::push_input(q_desc, tilized_tensor_desc, timeout_in_seconds, ptr);
                break;
            default: 
                log_fatal("Not Supported Yet"); 
                break;
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (tt::error_types::timeout_error &e) {
        log_debug(tt::LogBackend, "{}", e.what());
        return tt::DEVICE_STATUS_CODE::TimeoutError;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_PytorchTensorDesc &py_tensor_desc_in,
    const bool push_one,
    const int timeout_in_seconds,
    const int ptr) {
    int timeout_in_seconds_override = parse_env("TT_BACKEND_PUSH_TIMEOUT", timeout_in_seconds);
    auto push_start = std::chrono::high_resolution_clock::now();
    try {
        if (q_desc.io_type == tt::IO_TYPE::RandomAccess) {
            log_assert(ptr >= 0, "push_input in tt_backend_api.cpp Random access must specify a valid ptr");
        }
        tt::tt_PytorchTensorDesc py_tensor_desc = tt::io::expand_pytorch_tensor_dims(py_tensor_desc_in);
        switch (q_desc.backend_type) {
            case tt::DEVICE::Golden:
                tt::golden::io::push_input(q_desc, py_tensor_desc, push_one, timeout_in_seconds_override, ptr);
                break;
            case tt::DEVICE::Versim:
            case tt::DEVICE::Silicon:
                tt::io::push_input_to_device(q_desc, py_tensor_desc, push_one, timeout_in_seconds_override, ptr);
                break;
            default: 
                log_fatal("Not Supported Yet"); 
                break;
        }
        if (backend_profiler.is_enabled()) {
            auto push_end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(push_end - push_start).count();
            const bool include_tile_header_size = q_desc.layout != IO_LAYOUT::Flat;
            log_assert(q_desc.tile_height > 0 && q_desc.tile_width > 0, "Tile dimensions must be positive");
            int entry_size = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, include_tile_header_size, q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width);
            int io_size = (entry_size * py_tensor_desc.shape[0] + QUEUE_HEADER_SIZE_BYTES) * q_desc.bufq_grid_dim_r * q_desc.bufq_grid_dim_c;
            [[maybe_unused]] float push_bandwidth = ((float)io_size / (1024*1024*1024)) / ((float)duration / 1e6);
            [[maybe_unused]] float push_rate = (float)(py_tensor_desc.shape[0]) / ((float)duration / 1e6);
            log_profile("Pushed {} ({} KB) with duration = {} us, rate = {} inputs/s, {} GB/s ", q_desc.queue_name, io_size / 1024, duration, push_rate, push_bandwidth);
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (tt::error_types::timeout_error &e) {
        log_debug(tt::LogBackend, "{}", e.what());
        return tt::DEVICE_STATUS_CODE::TimeoutError;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE pop_output(
    const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds) {
    int timeout_in_seconds_override = parse_env("TT_BACKEND_POP_TIMEOUT", timeout_in_seconds);
    try {
        switch (q_desc.backend_type) {
            case tt::DEVICE::Golden: 
                tt::golden::io::pop_output(q_desc, pop_one, timeout_in_seconds_override); 
                break;
            case tt::DEVICE::Versim:
            case tt::DEVICE::Silicon: 
                tt::io::pop_output_from_device(q_desc, pop_one, timeout_in_seconds_override); 
                break;
            default: 
                log_fatal("Not Supported Yet"); 
                break;
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (tt::error_types::timeout_error &e) {
        log_debug(tt::LogBackend, "{}", e.what());
        return tt::DEVICE_STATUS_CODE::TimeoutError;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE get_output(
    const tt::tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool get_one,
    const int timeout_in_seconds,
    const int ptr) {
    int timeout_in_seconds_override = parse_env("TT_BACKEND_GET_TIMEOUT", timeout_in_seconds);
    auto get_start = std::chrono::high_resolution_clock::now();
    try {
        if (q_desc.io_type == tt::IO_TYPE::RandomAccess) {
            log_assert(ptr >= 0, "Random access must specify a valid ptr");
        }
        switch (q_desc.backend_type) {
            case tt::DEVICE::Golden:
                tt::golden::io::get_output(q_desc, py_tensor_desc, get_one, timeout_in_seconds_override, ptr);
                break;
            case tt::DEVICE::Versim:
            case tt::DEVICE::Silicon:
                tt::io::get_output_from_device(q_desc, py_tensor_desc, get_one, timeout_in_seconds_override, ptr);
                break;
            default: 
                log_fatal("Not Supported Yet"); 
                break;
        }
        if (backend_profiler.is_enabled()) {
            auto get_end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(get_end - get_start).count();
            const bool include_tile_header_size = q_desc.layout != IO_LAYOUT::Flat;
            log_assert(q_desc.tile_height > 0 && q_desc.tile_width > 0, "Tile dimensions must be positive");
            int entry_size = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, include_tile_header_size, q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width);
            int io_size = (entry_size * py_tensor_desc.shape[0] + QUEUE_HEADER_SIZE_BYTES) * q_desc.bufq_grid_dim_r * q_desc.bufq_grid_dim_c;
            [[maybe_unused]] float get_bandwidth = ((float)io_size / (1024*1024*1024)) / ((float)duration / 1e6);;
            [[maybe_unused]] float get_rate = (float)(py_tensor_desc.shape[0]) / ((float)duration / 1e6);;
            log_profile("Read {} ({} KB) with duration: {} us, rate = {} outputs/s , {} GB/s ", q_desc.queue_name, io_size/1024, duration, get_rate, get_bandwidth);
        }
        py_tensor_desc = tt::io::reduce_pytorch_tensor_dims(py_tensor_desc);
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (tt::error_types::timeout_error &e) {
        log_debug(tt::LogBackend, "{}", e.what());
        return tt::DEVICE_STATUS_CODE::TimeoutError;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE translate_addresses(tt::tt_dram_io_desc &q_desc) {
    try {
        switch (q_desc.backend_type) {
            case tt::DEVICE::Versim:
            case tt::DEVICE::Silicon: tt::io::translate_addresses(q_desc); break;
            default: break;
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

template<typename T>
tt::DEVICE_STATUS_CODE free_tensor(T &tensor_desc) {
    try {
        void *ptr = const_cast<void *>(tensor_desc.ptr);
        log_assert(tensor_desc.ptr != nullptr, "tensor_desc is null");
        log_assert(tensor_desc.owner == tt::OWNERSHIP::Backend, "desc_owner isn't Backend");
        
        if (tt::mem::host_shared_memory.find(ptr) != tt::mem::host_shared_memory.end()) {
            log_assert(tt::mem::host_shared_memory.at(ptr).use_count() == 1, "use_count != 1");  // last reference
            tt::mem::host_shared_memory.erase(ptr);
        } else {
            log_debug(
                tt::LogBackend,
                "Tensor {} absent from backend dynamically allocated memory, skipping free_tensor",
                reinterpret_cast<intptr_t>(ptr));
        }
        tensor_desc.ptr = nullptr;
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE initialize_child_process(const std::string &output_dir) {
    try {
        tt::io::info.output_dir = output_dir;
        tt::io::info.proc_id = getpid();
        setup_backend_profiler(output_dir);
        log_info(tt::LogBackend, "initialize_child_process called on pid {}", tt::io::info.proc_id);

        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE finish_child_process() {
    try {
        log_assert(
            tt::io::info.proc_id == getpid(),
            "Current child process {} was not initialized!", std::to_string(getpid()));
        log_info(tt::LogBackend, "finish_child_process called on pid {}", tt::io::info.proc_id);

        tt::io::free_object_cache();
        tt::cpuset::tt_cpuset_allocator::clear_state_and_cpuset_pins();
        tt::golden::io::free_object_cache();
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

// TODO look into desc caching
// - can device_yaml be reliably used as cache key, what if the device_yaml is updated?
// - when do we clear the cache? process based or user initiated?

std::tuple<std::uint32_t, std::uint32_t> translate_coord_logical_to_routing(
    const std::string &device_yaml, const std::tuple<std::uint32_t, std::uint32_t> &coord) {
    auto desc = load_soc_descriptor_from_yaml(device_yaml);
    tt_xy_pair xy = {std::get<0>(coord), std::get<1>(coord)};
    xy = desc->get_routing_core(xy);
    return std::make_tuple(xy.x, xy.y);
}

std::tuple<std::uint32_t, std::uint32_t> translate_coord_routing_to_logical(
    const std::string &device_yaml, const std::tuple<std::uint32_t, std::uint32_t> &coord) {
    auto desc = load_soc_descriptor_from_yaml(device_yaml);
    tt_xy_pair xy = {std::get<0>(coord), std::get<1>(coord)};
    log_assert(desc->is_worker_core(xy), "Logical coordinate only exists for Tensix worker cores!");
    xy = desc->get_worker_core(xy);
    return std::make_tuple(xy.x, xy.y);
}

int get_tile_size_in_bytes(
    const DataFormat data_formati, const bool is_untilized, const int tile_height, const int tile_width) {
    return tt::size::get_tile_size_in_bytes(data_formati, !is_untilized, tile_height, tile_width);
}

int get_entry_size_in_bytes(
    const DataFormat data_formati,
    const bool is_untilized,
    const int ublock_ct,
    const int ublock_rt,
    const int mblock_m,
    const int mblock_n,
    const int t,
    const int tile_height,
    const int tile_width) {
    int entry_size = tt::size::get_entry_size_in_bytes(
        data_formati, !is_untilized, ublock_ct, ublock_rt, mblock_m, mblock_n, t, tile_height, tile_width);
    return entry_size;
}

int get_io_size_in_bytes(
    const DataFormat data_formati,
    const bool is_untilized,
    const int ublock_ct,
    const int ublock_rt,
    const int mblock_m,
    const int mblock_n,
    const int t,
    const int entries,
    const int tile_height,
    const int tile_width) {
    int entry_size =
        tt::size::get_entry_size_in_bytes(data_formati, !is_untilized, ublock_ct, ublock_rt, mblock_m, mblock_n, t, tile_height, tile_width);
    return entry_size * entries + QUEUE_HEADER_SIZE_BYTES;
}

uint32_t get_next_aligned_address(const uint32_t address) {
    constexpr uint32_t alignment = QUEUE_ALIGN_SIZE_BYTES;
    return ((address + (alignment - 1)) & ~(alignment - 1));
}

std::vector<tt::ARCH> detect_available_devices(bool only_detect_mmio) { return tt_cluster::detect_available_devices(TargetDevice::Silicon, only_detect_mmio); }

std::vector<tt::param::DeviceDesc> get_device_descs_for_available_devices(const std::string& out_dir) { return tt::param::get_device_descs(out_dir); }

tt::param::DeviceDesc get_custom_device_desc(
                      tt::ARCH arch,
                      bool mmio, 
                      std::uint32_t harvesting_mask, 
                      std::pair<int, int> grid_dim,
                      const std::string& out_dir) {
                        return tt::param::get_custom_device_desc(arch, mmio, harvesting_mask, grid_dim, out_dir); 
}

std::string get_device_cluster_yaml() {
    log_warning(tt::LogAlways, "get_device_cluster_yaml has been deprecated. Please use get_device_cluster_yaml_v2.");
    return tt::param::get_device_cluster_yaml("");
}

std::string get_device_cluster_yaml_v2(const std::string& out_dir) { return tt::param::get_device_cluster_yaml(out_dir); }

std::string get_backend_param(const std::string &key, const std::string &sdesc, const std::string &ndesc, const std::string &runtime_params_ref_path, const bool save) {
    // get the params store
    auto &store = tt::param::tt_backend_params::get(sdesc, ndesc, runtime_params_ref_path, save);
    // check if wildcard key if used to dump all params
    if (key == "*") {
        int arch_params = store.params.size();
        int sys_params = store.runtime_system_params.size();
        int num_params = arch_params + sys_params;
        std::stringstream ss;
        ss << "{" << std::endl;
        for (auto &it : store.params) {
            ss << "  {" << it.first << ": " << it.second << "}" << std::endl;
        }
        for (auto &it : store.runtime_system_params) {
            ss << "  {" << it.first << ": " << it.second << "}" << std::endl;
        }
        ss << "}" << std::endl;
        log_info(tt::LogBackend, "Dumping {} backend params ... \n{}", num_params, ss.str());
        return std::to_string(num_params);
    }
    return store.get_param(key, runtime_params_ref_path, save);
}

void clear_backend_param_cache(const std::string& out_dir) {
    log_warning(tt::LogAlways, "clear_backend_param_cache has been deprecated. Please use clear_backend_param_cache_v2");
    tt::param::tt_backend_params::reset();
}

void clear_backend_param_cache_v2() {
    tt::param::tt_backend_params::reset();
}

uint32_t get_op_model_execution_cycles(const tt_op_model_desc &op_desc) {
    return tt::OpModel::get_op_cycles(op_desc);
}

uint32_t get_op_model_param(const tt_op_model_desc &op_desc, const std::string &param_name) {
    return tt::OpModel::get_op_param(op_desc, param_name);
}

tt::DEVICE_STATUS_CODE setup_backend_profiler(const std::string &output_dir) {
    try {
        pid_t process_id = getpid();
        backend_profiler.setup_host_perf_profiler(output_dir, "", process_id, false);
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::DEVICE_STATUS_CODE record_backend_event(const std::string &event_label) {
    try {
        backend_profiler.record_loader_event(event_label);
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

tt::tt_TilizedTensorDesc tilize_tensor(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc) {
    return tt::io::tilize_tensor(q_desc, py_tensor_desc);
}
template<typename T>
void binarize_tensor(const T& tensor, const std::string& file_path) {
    tt::io::binarize_tensor(tensor, file_path);
}
template<typename T>
void debinarize_tensor(T& tensor, const std::string& file_path) {
    tt::io::debinarize_tensor(tensor, file_path);
}
}  // namespace tt::backend
template tt::DEVICE_STATUS_CODE tt::backend::free_tensor<tt::tt_PytorchTensorDesc>(tt::tt_PytorchTensorDesc&);
template tt::DEVICE_STATUS_CODE tt::backend::free_tensor<tt::tt_TilizedTensorDesc>(tt::tt_TilizedTensorDesc&);
template void tt::backend::binarize_tensor<tt::tt_TilizedTensorDesc>(const tt::tt_TilizedTensorDesc&, const std::string&);
template void tt::backend::binarize_tensor<tt::tt_PytorchTensorDesc>(const tt::tt_PytorchTensorDesc&, const std::string&);
template void tt::backend::debinarize_tensor<tt::tt_TilizedTensorDesc>(tt::tt_TilizedTensorDesc&, const std::string&);
template void tt::backend::debinarize_tensor<tt::tt_PytorchTensorDesc>(tt::tt_PytorchTensorDesc&, const std::string&);