// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "bindings.h"

class bindings_implementation : public tt::dbd::debuda_implementation {
   private:
    std::map<std::tuple<uint8_t, uint8_t, uint8_t, uint64_t>, uint32_t> read_write_4;
    std::map<std::tuple<uint8_t, uint8_t, uint8_t, uint64_t, uint32_t>, std::vector<uint8_t>> read_write;
    std::map<std::tuple<uint8_t, uint64_t>, uint32_t> read_write_4_raw;

   public:
    std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) override {
        auto it = read_write_4.find(std::make_tuple(chip_id, noc_x, noc_y, address));
        if (it != read_write_4.end()) {
            return it->second;
        }
        return {};
    }

    std::optional<uint32_t> pci_write4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                       uint32_t data) override {
        read_write_4[std::make_tuple(chip_id, noc_x, noc_y, address)] = data;
        return data;
    }

    std::optional<std::vector<uint8_t>> pci_read(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                                 uint32_t size) override {
        auto it = read_write.find(std::make_tuple(chip_id, noc_x, noc_y, address, size));
        if (it != read_write.end()) {
            return it->second;
        }
        return {};
    }

    std::optional<uint32_t> pci_write(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                      const uint8_t *data, uint32_t size) override {
        std::vector<uint8_t> data_vector(size);
        for (size_t i = 0; i < size; i++) {
            data_vector[i] = data[i];
        }
        read_write[std::make_tuple(chip_id, noc_x, noc_y, address, size)] = data_vector;
        return size;
    }

    std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) override {
        auto it = read_write_4_raw.find(std::make_tuple(chip_id, address));
        if (it != read_write_4_raw.end()) {
            return it->second;
        }
        return {};
    }

    std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) override {
        read_write_4_raw[std::make_tuple(chip_id, address)] = data;
        return data;
    }

    std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) override {
        auto it = read_write_4_raw.find(std::make_tuple(chip_id, address));
        if (it != read_write_4_raw.end()) {
            return it->second + channel;
        }
        return {};
    }

    std::optional<std::string> pci_read_tile(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                             uint32_t size, uint8_t data_format) override {
        return "pci_read_tile(" + std::to_string(chip_id) + ", " + std::to_string(noc_x) + ", " +
               std::to_string(noc_y) + ", " + std::to_string(address) + ", " + std::to_string(size) + ", " +
               std::to_string(data_format) + ")";
    }
    std::optional<std::string> get_runtime_data() override { return "get_runtime_data()"; }
    std::optional<std::string> get_cluster_description() override { return "get_cluster_description()"; }
    std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) override {
        return "get_harvester_coordinate_translation(" + std::to_string(chip_id) + ")";
    }
    std::optional<std::vector<uint8_t>> get_device_ids() override { return std::vector<uint8_t>{0, 1}; }
    std::optional<std::string> get_device_arch(uint8_t chip_id) override {
        return "get_device_arch(" + std::to_string(chip_id) + ")";
    }
    std::optional<std::string> get_device_soc_description(uint8_t chip_id) override {
        return "get_device_soc_description(" + std::to_string(chip_id) + ")";
    }
};

void set_debuda_test_implementation() {
    set_debuda_implementation(std::move(std::make_unique<bindings_implementation>()));
}

PYBIND11_MODULE(tt_dbd_pybind_unit_tests, n) {
    n.def("set_debuda_test_implementation", &set_debuda_test_implementation);
}