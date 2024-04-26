// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// llk specific headers
#include "llk_soc_descriptor.h"
#include "llk_tensor.h"
#include "llk_xy_pair.h"

class c_versim_core;
namespace nuapi {
namespace device {
template <typename, typename>
class Simulator;
}
}  // namespace nuapi
namespace versim {
struct VersimSimulatorState;
using VersimSimulator = nuapi::device::Simulator<c_versim_core *, VersimSimulatorState>;
}  // namespace versim

namespace llk {

enum class ThreadType { Unpack = 0, Math = 1, Pack = 2, Ncrisc = 3, Firmware = 4 };
string to_string(ThreadType thread);
string get_overlay_version(const ARCH arch_name);

//! Device API in a wrapper class
class Device {
   public:
    //! Initializes descriptor for device
    void init(std::string root, std::string device_descriptor_filename);
    //! Starts device
    /*!
      \param device Reference to device object that is to be started
      \param root root folder (git root)
      \param dump_cores vector of cores to dump in simulation
    */
    void start(
        std::string root,
        std::string output_root,
        std::vector<std::string> plusargs,
        std::vector<std::string> dump_cores,
        YAML::Node &testdef_yaml,
        std::string overlay_graph = "");
    //! Sets up test information (ckernels images etc)
    void setup_kernels(
        std::string root,
        std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> &kernels_used,
        std::string test_name,
        std::string test_dir,
        bool hlkc_test = false,
        bool perf_dump = false);
    //! Builds overlay blob hex files
    void generate_overlay_blob(std::string root, std::string output_root, std::map<std::string, std::string> blob_args);
    //! Runs simulation
    bool run();
    //! Stops and teardowns device
    bool stop();

    //! write vector to specific core -- address is byte address
    /*!
        \param mem_vector is the vector to write.
        \param target is xy coordinate that is the target of the read or write
        \param address is register byte address
    */
    void write_register(uint32_t &value, llk::xy_pair target, std::int32_t address);

    //! read vector from specific core -- address is byte address
    /*!
        \param mem_vector is the vector to read
        \param target is xy coordinate that is the target of the read or write
        \param address is register byte address
    */
    void read_register(uint32_t &value, llk::xy_pair target, std::int32_t address);

    //! write vector to specific core -- address is byte address
    /*!
        \param mem_vector is the vector to write.
        \param target is xy coordinate that is the target of the read or write
        \param address is byte address
    */
    void write_vector(std::vector<std::uint32_t> &mem_vector, llk::xy_pair target, std::int32_t address);

    //! read vector from specific core -- address is byte address
    /*!
        \param mem_vector is the vector to read
        \param target is xy coordinate that is the target of the read or write
        \param address is byte address
        \param size_in_bytes is number of bytes to read
    */
    void read_vector(
        std::vector<std::uint32_t> &mem_vector, llk::xy_pair target, std::int32_t address, std::int32_t size_in_bytes);

    //! Prepares a tensor for streaming by writing to memories (hex files) stored in a specified IO location
    /*!
        \param tensor is the tensor to write.
        \param num_blocks is how many blocks in x or y dims the tensor needs to be split.  Default of 1,1 means no
       splitting
        \param output_directory_path is the output folder path that the memories are written to. Any conflicts will
       cause overwrite
    */
    void prepare_tensor_for_streaming(
        llk::Tensor &tensor, std::string output_directory_path, llk::xy_pair num_blocks = llk::xy_pair(1, 1));

    //! write tensor to specific core -- address is byte address
    /*!
        \param tensor is the tensor to write.
        \param target is xy coordinate that is the target of the read or write
        \param address is byte address
        \param num_blocks is how many blocks in x or y dims the tensor needs to be split.  Default of 1,1 means no
       splitting
    */
    void write_tensor(
        llk::Tensor &tensor,
        llk::xy_pair target,
        std::int32_t address,
        llk::xy_pair num_blocks = llk::xy_pair(1, 1),
        bool untilize = false);

    //! Read a tensor that was streamed to a specified IO location
    /*!
        \param tensor is the tensor to write.
        \param output_directory_path is the output folder path that the memories are written to. Any conflicts will
       cause overwrite
    */
    void read_tensor_for_streaming(llk::Tensor &tensor, std::string directory_path);

    //! read tensor from specific core
    /*!
        \param tensor is the destination tensor to fill.  Dimension and format must be initialized correctly!
        \param target is xy coordinate that is the target of the read or write
        \param address is byte address
        \param dims is tensor dimensions
    */
    void read_tensor(llk::Tensor &tensor, llk::xy_pair target, std::int32_t address, bool untilize);

    //! Wait for completion status/signal from device
    bool wait_completion(
        int timeout = 200000,
        std::vector<ThreadType> threads_to_check = {
            ThreadType::Unpack, ThreadType::Math, ThreadType::Pack, ThreadType::Ncrisc, ThreadType::Firmware});

    //! Simple test of communication to device/target.  true if it passes.
    bool test_write_read(llk::xy_pair target);

    llk::SocDescriptor soc_descriptor;

   private:
    versim::VersimSimulator *versim;
};

}  // namespace llk
