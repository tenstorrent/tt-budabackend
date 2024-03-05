// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <dbdserver/umd_server.h>
#include <device/tt_cluster_descriptor_types.h>

#include <map>
#include <memory>
#include <set>

class tt_SiliconDevice;
class tt_ClusterDescriptor;

class plain_server : private tt::dbd::umd_server {
   private:
    std::string connection_address;  // The zmq connection string. Keeping it around for logging purposes
    std::unique_ptr<tt_SiliconDevice> device;
    std::string device_configuration_path;
    std::string cluster_descriptor_path;
    std::vector<uint8_t> device_ids;
    std::map<uint8_t, std::string> device_soc_descriptors;

    bool create_device();
    bool is_chip_mmio_capable(uint8_t chip_id);
    void create_device_soc_descriptors();

    static std::unique_ptr<tt_SiliconDevice> create_grayskull_device(
        const std::string &device_configuration_path, const std::string &cluster_descriptor_path, const std::set<chip_id_t> &target_devices);
    static std::unique_ptr<tt_SiliconDevice> create_wormhole_device(
        const std::string &device_configuration_path, const std::string &cluster_descriptor_path, const std::set<chip_id_t> &target_devices);
    static std::unique_ptr<tt_SiliconDevice> create_blackhole_device(
        const std::string &device_configuration_path, const std::string &cluster_descriptor_path, const std::set<chip_id_t> &target_devices);

   protected:
    std::optional<std::string> pci_read_tile(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) override;
    std::optional<std::string> get_runtime_data() override;
    std::optional<std::string> get_cluster_description() override;
    std::optional<std::vector<uint8_t>> get_device_ids() override;
    std::optional<std::string> get_device_soc_description(uint8_t chip_id) override;

   public:
    virtual ~plain_server();

    bool start(int port);
};
