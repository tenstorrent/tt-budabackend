#include <dbdserver/debuda_implementation.h>
#include <dbdserver/umd_with_open_implementation.h>
#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "device/tt_device.h"

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

bool open_device(const std::string &binary_directory) {
    try {
        // Since tt_SiliconDevice is printing some output and we don't want to see it in python, we disable std::cout
        scoped_null_stdout null_stdout;

        debuda_implementation = tt::dbd::umd_with_open_implementation::open(binary_directory);
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

std::optional<std::vector<uint8_t>> pci_read(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size) {
    if (debuda_implementation) {
        return debuda_implementation->pci_read(chip_id, noc_x, noc_y, address, size);
    }
    return {};
}

std::optional<uint32_t> pci_write(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, const uint8_t *data, uint32_t size) {
    if (debuda_implementation) {
        return debuda_implementation->pci_write(chip_id, noc_x, noc_y, address, data, size);
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

std::optional<std::string> pci_read_tile(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) {
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
    m.def("open_device", &open_device, "Opens tt device. Prints error message if failed.");
    m.def("pci_read4", &pci_read4);
    m.def("pci_write4", &pci_write4);
    m.def("pci_read", &pci_read);
    m.def("pci_write", &pci_write);
    m.def("pci_read4_raw", &pci_read4_raw);
    m.def("pci_write4_raw", &pci_write4_raw);
    m.def("dma_buffer_read4", &dma_buffer_read4);
    m.def("pci_read_tile", &pci_read_tile);
    m.def("get_runtime_data", &get_runtime_data);
    m.def("get_cluster_description", &get_cluster_description);
    m.def("get_harvester_coordinate_translation", &get_harvester_coordinate_translation);
    m.def("get_device_ids", &get_device_ids);
    m.def("get_device_arch", &get_device_arch);
    m.def("get_device_soc_description", &get_device_soc_description);
}
