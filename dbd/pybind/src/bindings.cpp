// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "bindings.h"

static std::unique_ptr<tt::dbd::debuda_implementation> debuda_implementation;

class scoped_null_stdout {
   private:
    std::streambuf *original_stdout;
    std::ofstream null_stream;

   public:
    scoped_null_stdout() {
        original_stdout = std::cout.rdbuf();
        null_stream.open("/dev/null");
        std::cout.rdbuf(null_stream.rdbuf());
    }

    ~scoped_null_stdout() { std::cout.rdbuf(original_stdout); }
};

void set_debuda_implementation(std::unique_ptr<tt::dbd::debuda_implementation> imp) {
    debuda_implementation = std::move(imp);
}

bool open_device(const std::string &binary_directory, const std::string &runtime_yaml_path) {
    try {
        // Since tt_SiliconDevice is printing some output and we don't want to see it in python, we disable std::cout
        scoped_null_stdout null_stdout;

        debuda_implementation = tt::dbd::umd_with_open_implementation::open(binary_directory, runtime_yaml_path);
        if (!debuda_implementation) {
            return false;
        }
    } catch (std::runtime_error &error) {
        std::cerr << "Cannot open device: " << error.what() << std::endl;
        return false;
    }
    return true;
}

std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) {
    if (debuda_implementation) {
        return debuda_implementation->pci_read4(chip_id, noc_x, noc_y, address);
    }
    return {};
}

std::optional<uint32_t> pci_write4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) {
    if (debuda_implementation) {
        return debuda_implementation->pci_write4(chip_id, noc_x, noc_y, address, data);
    }
    return {};
}

std::optional<pybind11::object> pci_read(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                         uint32_t size) {
    if (debuda_implementation) {
        auto data = debuda_implementation->pci_read(chip_id, noc_x, noc_y, address, size);

        if (data) {
            // This is a hacky way to create a bytes object for Python so that we avoid multiple
            // copying. Pyobject is created manually and then passed to pybind11::reinterpret_steal
            // to prevent memory leak.
            // See https://github.com/pybind/pybind11/issues/1236
            PyBytesObject *bytesObject = nullptr;

            bytesObject = (PyBytesObject *)PyObject_Malloc(offsetof(PyBytesObject, ob_sval) + size + 1);

            PyObject_INIT_VAR(bytesObject, &PyBytes_Type, size);
            bytesObject->ob_shash = -1;
            bytesObject->ob_sval[size] = '\0';

            for (size_t i = 0; i < size; i++) {
                bytesObject->ob_sval[i] = data.value()[i];
            }

            return pybind11::reinterpret_steal<pybind11::object>((PyObject *)bytesObject);
        }
    }
    return {};
}

std::optional<uint32_t> pci_write(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                  pybind11::buffer data, uint32_t size) {
    if (debuda_implementation) {
        pybind11::buffer_info info = data.request();
        uint8_t *data_ptr = static_cast<uint8_t *>(info.ptr);

        return debuda_implementation->pci_write(chip_id, noc_x, noc_y, address, data_ptr, size);
    }
    return {};
}

std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) {
    if (debuda_implementation) {
        return debuda_implementation->pci_read4_raw(chip_id, address);
    }
    return {};
}

std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) {
    if (debuda_implementation) {
        return debuda_implementation->pci_write4_raw(chip_id, address, data);
    }
    return {};
}

std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) {
    if (debuda_implementation) {
        return debuda_implementation->dma_buffer_read4(chip_id, address, channel);
    }
    return {};
}

std::optional<std::string> pci_read_tile(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size,
                                         uint8_t data_format) {
    if (debuda_implementation) {
        return debuda_implementation->pci_read_tile(chip_id, noc_x, noc_y, address, size, data_format);
    }
    return {};
}

std::optional<std::string> get_runtime_data() {
    if (debuda_implementation) {
        return debuda_implementation->get_runtime_data();
    }
    return {};
}

std::optional<std::string> get_cluster_description() {
    if (debuda_implementation) {
        return debuda_implementation->get_cluster_description();
    }
    return {};
}

std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) {
    if (debuda_implementation) {
        return debuda_implementation->get_harvester_coordinate_translation(chip_id);
    }
    return {};
}

std::optional<std::vector<uint8_t>> get_device_ids() {
    if (debuda_implementation) {
        return debuda_implementation->get_device_ids();
    }
    return {};
}

std::optional<std::string> get_device_arch(uint8_t chip_id) {
    if (debuda_implementation) {
        return debuda_implementation->get_device_arch(chip_id);
    }
    return {};
}

std::optional<std::string> get_device_soc_description(uint8_t chip_id) {
    if (debuda_implementation) {
        return debuda_implementation->get_device_soc_description(chip_id);
    }
    return {};
}

PYBIND11_MODULE(tt_dbd_pybind, m) {
    m.def("open_device", &open_device, "Opens tt device. Prints error message if failed.",
          pybind11::arg("binary_directory"), pybind11::arg("runtime_yaml_path"));
    m.def("pci_read4", &pci_read4, "Reads 4 bytes from PCI address", pybind11::arg("chip_id"), pybind11::arg("noc_x"),
          pybind11::arg("noc_y"), pybind11::arg("address"));
    m.def("pci_write4", &pci_write4, "Writes 4 bytes to PCI address", pybind11::arg("chip_id"), pybind11::arg("noc_x"),
          pybind11::arg("noc_y"), pybind11::arg("address"), pybind11::arg("data"));
    m.def("pci_read", &pci_read, "Reads data from PCI address", pybind11::arg("chip_id"), pybind11::arg("noc_x"),
          pybind11::arg("noc_y"), pybind11::arg("address"), pybind11::arg("size"));
    m.def("pci_write", &pci_write, "Writes data to PCI address", pybind11::arg("chip_id"), pybind11::arg("noc_x"),
          pybind11::arg("noc_y"), pybind11::arg("address"), pybind11::arg("data"), pybind11::arg("size"));
    m.def("pci_read4_raw", &pci_read4_raw, "Reads 4 bytes from PCI address", pybind11::arg("chip_id"),
          pybind11::arg("address"));
    m.def("pci_write4_raw", &pci_write4_raw, "Writes 4 bytes to PCI address", pybind11::arg("chip_id"),
          pybind11::arg("address"), pybind11::arg("data"));
    m.def("dma_buffer_read4", &dma_buffer_read4, "Reads 4 bytes from DMA buffer", pybind11::arg("chip_id"),
          pybind11::arg("address"), pybind11::arg("channel"));
    m.def("pci_read_tile", &pci_read_tile, "Reads tile from PCI address", pybind11::arg("chip_id"),
          pybind11::arg("noc_x"), pybind11::arg("noc_y"), pybind11::arg("address"), pybind11::arg("size"),
          pybind11::arg("data_format"));
    m.def("get_runtime_data", &get_runtime_data, "Returns runtime data");
    m.def("get_cluster_description", &get_cluster_description, "Returns cluster description");
    m.def("get_harvester_coordinate_translation", &get_harvester_coordinate_translation,
          "Returns harvester coordinate translation", pybind11::arg("chip_id"));
    m.def("get_device_ids", &get_device_ids, "Returns device ids");
    m.def("get_device_arch", &get_device_arch, "Returns device architecture", pybind11::arg("chip_id"));
    m.def("get_device_soc_description", &get_device_soc_description, "Returns device SoC description",
          pybind11::arg("chip_id"));
}
