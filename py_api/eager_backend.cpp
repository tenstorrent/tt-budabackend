// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <unordered_map>

#include "py_api/eager_backend.hpp"
#include "runtime/runtime_eager_io.hpp"

#include "netlist/tt_backend.hpp"
#include "netlist/tt_backend_api.hpp"

namespace tt::backend_api
{
using tt_backend_config = tt::tt_backend_config;

void python_handle_refchange(const void *handle_ptr, bool allocate)
{
    py::handle handle((PyObject *)handle_ptr);
    if (allocate)
        handle.inc_ref();
    else
        handle.dec_ref();
}

void EagerBackend(py::module &m_backend) {

    py::class_<tt_backend_config>(m_backend, "BackendConfig")
        .def(py::init([](
                  tt::DEVICE backend_type, 
                  tt::ARCH backend_device, 
                  tt::DEVICE_MODE device_mode, 
                  int opt_level,
                  const std::string &output_dir,
                  const std::string &cluster_descriptor_path) {

            auto cfg = tt_backend_config{
                .type = backend_type, 
                .arch = backend_device, 
                .mode = device_mode,
                .output_dir = output_dir,
                .cluster_descriptor_path = cluster_descriptor_path};

            char *env_opt_level = getenv("TT_BACKEND_OPT_LEVEL");
            if (env_opt_level) {
                cfg.optimization_level = atoi(env_opt_level);
            }
            else {
                cfg.optimization_level = opt_level;
            }
            if (backend_type == tt::DEVICE::Golden) {
                cfg.ignore_data_format_precision = true; // run backend at full precision by default (on Golden)
            }
            return cfg;
        }))
        .def("set_golden_ignore_df_precision", [](tt_backend_config &self, bool ignore_data_format_precision) {
            self.ignore_data_format_precision = ignore_data_format_precision;
        })
        .def("set_performance_trace_args", [](tt_backend_config &self, std::string args) {
            self.perf_desc_args = args;
        })
        .def("set_runtime_args", [](tt_backend_config &self, std::string args) {
            self.runtime_args = args;
        });

    m_backend.def("get_runtime_config", [](tt::ARCH arch) {
        tt_backend_config cfg = {tt::DEVICE::Silicon, arch};
        cfg.runtime_args = "--skip-io-init --skip-overlap-check";
        return cfg;
    });

    py::class_<tt::tt_PytorchTensorDesc>(m_backend, "PytorchTensorDesc", py::buffer_protocol())
        .def(py::init([]() {
            return tt_PytorchTensorDesc();
        }))
        .def(py::init([](py::object pytorch_tensor, std::uint32_t itemsize, tt::DataFormat format,
                        std::uint32_t dim,
                        std::array<std::uint32_t, 4> shape,
                        std::array<std::uint32_t, 4> strides) {

            auto ptr = pytorch_tensor.attr("data_ptr")().cast<std::uint64_t>();
            py::handle handle = pytorch_tensor.release();

            return tt_PytorchTensorDesc(
                (void *)ptr, itemsize, format, shape, strides, dim, (void*)handle.ptr(), python_handle_refchange);
        }))
        .def(py::init([](void *buffer, std::uint32_t itemsize, tt::DataFormat format,
                        std::uint32_t dim,
                        std::array<std::uint32_t, 4> shape,
                        std::array<std::uint32_t, 4> strides) {

            return tt_PytorchTensorDesc(buffer, itemsize, format, shape, strides, dim);
        }))
        .def_readwrite("format", &tt_PytorchTensorDesc::format)
        .def_readonly("shape", &tt_PytorchTensorDesc::shape)
        .def("print", [](tt::tt_PytorchTensorDesc &self) {
            std::cout << "Descriptor: ptr=" << (std::uint64_t)self.ptr << 
                    ", itemsize=" << self.itemsize <<
                    ", format =" << (int)self.format <<
                    ", dim =" << self.dim <<
                    ", shape =" << self.shape[0] << "," << self.shape[1] << "," << self.shape[2] << "," << self.shape[3] <<
                    ", strides =" << self.strides[0] << "," << self.strides[1] << "," << self.strides[2] << "," << self.strides[3] << std::endl;
        })
        .def_buffer([](tt::tt_PytorchTensorDesc &desc) -> py::buffer_info {

           // Mostly irrelevant since we'll be turning this into torch tensor with its
           // own format. However, this could cause numpy to interpret the data wrong
           std::string data_format = py::format_descriptor<float>::format();
           return py::buffer_info(
                const_cast<void *>(desc.ptr),
                desc.itemsize,
                data_format,
                4,
                desc.shape,
                desc.strides);
         })
         .def(py::pickle(
           [](const tt::tt_PytorchTensorDesc &t) { // __getstate__
              return py::make_tuple(
                  reinterpret_cast<std::uintptr_t>(t.ptr),
                  t.itemsize,
                  t.format,
                  t.shape,
                  t.strides,
                  t.dim);
           },
           [](py::tuple t) { // __setstate__
              if (t.size() != 6)
                  throw std::runtime_error("Invalid state!");

              tt::tt_PytorchTensorDesc p;
              p.ptr = reinterpret_cast<const void*>(t[0].cast<std::uintptr_t>());
              p.itemsize = t[1].cast<std::uint32_t>();
              p.format = t[2].cast<DataFormat>();
              p.shape = t[3].cast<std::array<std::uint32_t, 4>>();
              p.strides = t[4].cast<std::array<std::uint32_t, 4>>();
              p.dim = t[5].cast<std::uint32_t>();
              return p;
           }
          ));

    py::class_<tt_backend, std::shared_ptr<tt_backend>>(m_backend, "Backend")
        .def(py::init([](const tt::tt_backend_config &config, const std::set<int> &target_devices) {
            std::shared_ptr<tt_backend> backend = tt_backend::create_eager_backend(config, target_devices);
            tt::eager::io::initialize(config.arch, target_devices);
            return backend;
        }))
        .def("compile_netlist", &tt_backend::compile_netlist, py::call_guard<py::gil_scoped_release>())
        .def("compile_and_run_netlist", &tt_backend::compile_and_run_netlist, py::call_guard<py::gil_scoped_release>())
        .def("run_program", &tt_backend::run_program, py::call_guard<py::gil_scoped_release>())
        .def("wait_for_idle", &tt_backend::wait_for_idle, py::call_guard<py::gil_scoped_release>())
        .def("destroy", [](std::shared_ptr<tt_backend> self) {
            tt::eager::io::finish();
            self->finish();
            self.reset();
        });

    m_backend.def("push_tensor", &tt::eager::io::push_tensor, py::call_guard<py::gil_scoped_release>());
    m_backend.def("get_tensor", &tt::eager::io::get_tensor, py::call_guard<py::gil_scoped_release>());
    m_backend.def("pop_tensor", &tt::eager::io::pop_tensor, py::call_guard<py::gil_scoped_release>());
    m_backend.def("init_queue", &tt::eager::io::init_queue);
    m_backend.def("free_tensor", &tt::backend::free_tensor<tt::tt_TilizedTensorDesc>);
    m_backend.def("free_tensor", &tt::backend::free_tensor<tt::tt_PytorchTensorDesc>);
    // This is only needed if Python runtime spanws a child process for IO
    // otherwise these are taken care of by the default backend init and destroy
    m_backend.def("initialize_child_process", &tt::eager::io::initialize);
    m_backend.def("finish_child_process", &tt::eager::io::finish);

}
}  // namespace tt::backend_api

PYBIND11_MODULE(eager_backend, m) {
    py::module_ m_backend = m.def_submodule("backend_api", "API to Buda Backend");
    tt::backend_api::EagerBackend(m_backend);

    py::enum_<tt::DataFormat>(m, "DataFormat")
        .value("Float32", tt::DataFormat::Float32)
        .value("Float16", tt::DataFormat::Float16)
        .value("Bfp8", tt::DataFormat::Bfp8)
        .value("Bfp4", tt::DataFormat::Bfp4)
        .value("Bfp2", tt::DataFormat::Bfp2)
        .value("Float16_b", tt::DataFormat::Float16_b)
        .value("Bfp8_b", tt::DataFormat::Bfp8_b)
        .value("Bfp4_b", tt::DataFormat::Bfp4_b)
        .value("Bfp2_b", tt::DataFormat::Bfp2_b)
        .value("Lf8", tt::DataFormat::Lf8)
        .value("UInt16", tt::DataFormat::UInt16)
        .value("Int8", tt::DataFormat::Int8)
        .value("RawUInt8", tt::DataFormat::RawUInt8)
        .value("RawUInt16", tt::DataFormat::RawUInt16)
        .value("RawUInt32", tt::DataFormat::RawUInt32)
        .value("Invalid", tt::DataFormat::Invalid)
        .export_values();

    py::enum_<tt::DEVICE>(m, "BackendType")
        .value("Golden", tt::DEVICE::Golden)
        .value("Model", tt::DEVICE::Model)
        .value("Silicon", tt::DEVICE::Silicon)
        .value("NoBackend", tt::DEVICE::Invalid)
        .export_values();

    py::enum_<tt::IO_TYPE>(m, "IOType")
        .value("Queue", tt::IO_TYPE::Queue)
        .value("RandomAccess", tt::IO_TYPE::RandomAccess)
        .value("Invalid", tt::IO_TYPE::Invalid);

    py::enum_<QUEUE_LOCATION>(m, "IOLocation")
        .value("Dram", QUEUE_LOCATION::DRAM)
        .value("Host", QUEUE_LOCATION::HOST)
        .value("Invalid", QUEUE_LOCATION::INVALID);

    py::enum_<tt::ARCH>(m, "BackendDevice")
        .value("Grayskull", tt::ARCH::GRAYSKULL)
        .value("Wormhole", tt::ARCH::WORMHOLE)
        .value("Wormhole_B0", tt::ARCH::WORMHOLE_B0)
        .value("Invalid", tt::ARCH::Invalid);

    py::enum_<tt::DEVICE_MODE>(m, "DeviceMode")
        .value("CompileAndRun", tt::DEVICE_MODE::CompileAndRun)
        .value("CompileOnly", tt::DEVICE_MODE::CompileOnly)
        .value("RunOnly", tt::DEVICE_MODE::RunOnly);

    py::enum_<tt::DEVICE_STATUS_CODE>(m, "BackendStatusCode")
        .value("Success", tt::DEVICE_STATUS_CODE::Success)
        .value("RuntimeError", tt::DEVICE_STATUS_CODE::RuntimeError)
        .value("TimeoutError", tt::DEVICE_STATUS_CODE::TimeoutError);
}
