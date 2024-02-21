// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <vector>

#include "common/tti_lib.hpp"
#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
/**
 * @file
 * This file contains data types used in Runtime and IO APIs
 */

namespace tt {

static constexpr std::uint32_t PY_TENSOR_DIMS = 4;

/**
 * @brief Tile dimension enum used to pass variable tile sizes across the SW+HW stack.
 * Only specific dimensions are valid. Please check the enum definition for all valid dimensions.
 */
enum class TileDim : std::uint8_t {
    Dim32x32 = 0,
    Dim16x32 = 1,
    Dim32x16 = 2,
    Dim8x32  = 3,
    Dim4x32  = 4,
    Dim2x32  = 5,
    Dim1x32  = 6,
    Default  = Dim32x32,
    Invalid  = 0xff
};
/**
 * @brief Get the tile dimension as a string.
 */
std::string get_string(TileDim tile_dim);
/**
 * Convert an {x, y} vector to a TileDim.
 */
TileDim get_tile_dim_from_array(const std::vector<int>& tile_dim_arr);
/**
 * @brief Convert an XxY string to a TileDim.
 */
TileDim get_tile_dim_from_string(const std::string& tile_dim_str);
/**
 * @brief Transpose Tile Dimensions.
 */
TileDim transpose_tile_dim(TileDim input_tile_dim);
/**
 * @brief Get an {x, y} vector from the tile dim.
 */
std::vector<int> tile_dim_to_array(TileDim tile_dim);

/**
 * @brief Tensix data format enum, used across the entire SW+HW stack.
 * Bfp formats are block floating point formats specific to TT hardware
 * Raw formats are non-tilized data consumed by risc-v cores, not TT hardware
 * Others are standard formats supported by software frameworks
 */
enum class DataFormat : uint8_t {
    Float32 = 0,
    Float16 = 1,
    Bfp8 = 2,
    Bfp4 = 3,
    Bfp2 = 11,
    Float16_b = 5,
    Bfp8_b = 6,
    Bfp4_b = 7,
    Bfp2_b = 15,
    Lf8 = 10,
    Fp8_e4m3 = 0x1A,
    UInt16 = 9,
    Int8 = 14,
    UInt8 = 30,
    Tf32 = 4,
    Int32 = 8,
    RawUInt8 = 0xf0,
    RawUInt16 = 0xf1,
    RawUInt32 = 0xf2,
    Invalid = 0xff
};
DataFormat get_format_from_string(const std::string& format_str);

/**
 * @brief Evaluates to true if the data format is a custom TT silicon format.
 */
inline bool is_tt_data_format(DataFormat format) {
    if (format == DataFormat::Bfp2 || format == DataFormat::Bfp2_b || format == DataFormat::Bfp4 ||
        format == DataFormat::Bfp4_b || format == DataFormat::Bfp8 || format == DataFormat::Bfp8_b ||
        format == DataFormat::Lf8) {
        return true;
    }
    return false;
}

/**
 * @brief Evaluates to true if the data format is an integer type.
 */
inline bool is_integer_format(DataFormat format) {
    return (
        (format == DataFormat::Int32) || (format == DataFormat::Int8) ||
        (format == DataFormat::UInt16) || (format == DataFormat::UInt8) ||
        (format == DataFormat::RawUInt32) || (format == DataFormat::RawUInt16) || (format == DataFormat::RawUInt8));
}

/// \cond
inline std::ostream& operator<<(std::ostream& os, const DataFormat& format) {
    switch (format) {
        case DataFormat::Bfp2: os << "Bfp2"; break;
        case DataFormat::Bfp2_b: os << "Bfp2_b"; break;
        case DataFormat::Bfp4: os << "Bfp4"; break;
        case DataFormat::Bfp4_b: os << "Bfp4_b"; break;
        case DataFormat::Bfp8: os << "Bfp8"; break;
        case DataFormat::Bfp8_b: os << "Bfp8_b"; break;
        case DataFormat::Float16: os << "Float16"; break;
        case DataFormat::Float16_b: os << "Float16_b"; break;
        case DataFormat::Float32: os << "Float32"; break;
        case DataFormat::Tf32: os << "Tf32"; break;
        case DataFormat::Int8: os << "Int8"; break;
        case DataFormat::UInt8: os << "UInt8"; break;
        case DataFormat::Lf8: os << "Lf8"; break;
        case DataFormat::Fp8_e4m3: os << "Fp8_e4m3"; break;
        case DataFormat::UInt16: os << "UInt16"; break;
        case DataFormat::Int32: os << "Int32"; break;
        case DataFormat::RawUInt8: os << "RawUInt8"; break;
        case DataFormat::RawUInt16: os << "RawUInt16"; break;
        case DataFormat::RawUInt32: os << "RawUInt32"; break;
        case DataFormat::Invalid: os << "Invalid"; break;
        default: throw std::invalid_argument("Unknown format");
    }
    return os;
}
/// \endcond

/**
 * @brief Tensix math fidelity enum, used across the entire SW+HW stack.
 */
enum class MathFidelity : uint8_t
{
    LoFi    = 0,
    HiFi2   = 2,
    HiFi3   = 3,
    HiFi4   = 4,
    HiFi2_A = 2,
    HiFi2_B = 10,
    Invalid = 0xff,
};

/// \cond
inline std::ostream& operator<<(std::ostream& os, const MathFidelity &fidelity)
{
    switch (fidelity) {
        case MathFidelity::LoFi: os << "LoFi"; break;
        case MathFidelity::HiFi2: os << "HiFi2"; break;
        case MathFidelity::HiFi3: os << "HiFi3"; break;
        case MathFidelity::HiFi4: os << "HiFi4"; break;
        case MathFidelity::Invalid: os << "Invalid"; break;
        default: throw std::invalid_argument("Unknown format");
    }
    return os;
}
/// \endcond

/**
 * @brief SFPU vector mode.
 */
enum class SfpuVectorMode : std::uint8_t {
    RC = 0,
    R = 1,
    C = 2,
    Invalid = 0xff,
};
SfpuVectorMode get_sfpu_vector_mode_from_string(const std::string &vector_mode_string);

/**
 * @brief DEVICE Enums
 */
enum class DEVICE {
    Model = 0,
    Versim = 1,
    Silicon = 2,
    Golden = 3,
    StaticAnalyzer = 4,
    Emulation = 5,
    Invalid = 0xFF,
};
std::string get_string(DEVICE device);
DEVICE get_device_from_string(const std::string& device_string);

std::string get_string(ARCH arch);
std::string get_string_lowercase(ARCH arch);
ARCH get_arch_from_string(const std::string& arch_str);

/**
 * @brief DEVICE Compile/Run Enums
 */
enum class DEVICE_MODE {
    CompileAndRun = 0,
    CompileOnly = 1,
    RunOnly = 2,
};
std::string get_string(DEVICE_MODE device_mode);
DEVICE_MODE get_device_mode_from_string(const std::string& device_mode_str);

/**
 * @brief DEVICE Status Code Enums
 */
enum class DEVICE_STATUS_CODE {
    Success = 0,       //!< Successful API exeuction
    RuntimeError = 1,  //!< Any Issues or errors due to asserts or failures
    TimeoutError = 2   //!< Timeout on API execution
};

/**
 * @brief Ownership Enums
 */
enum class OWNERSHIP { Frontend = 0, Backend = 1 };

/**
 * @brief Compile failure types
 */
enum class COMPILE_FAILURE {
    BriscCompile = 0,
    EriscCompile = 1,
    NriscCompile = 2,
    Net2Pipe = 3,
    PipeGen = 4,
    BlobGen = 5,
    L1Size = 6,
    OverlaySize = 7,
    Invalid = 0xFFFF,
};
std::string get_string(COMPILE_FAILURE compile_failure);

/**
 * @brief Compile failure result info
 */
struct tt_compile_result {
    bool success = true;
    // Failure info
    COMPILE_FAILURE failure_type = COMPILE_FAILURE::Invalid;
    std::string failure_message = "";
    std::string failure_target = "";
    // Reports space used by overlay blob across cores (str(x, y)) across temporal epochs
    std::unordered_map<std::uint32_t, std::unordered_map<std::string, std::uint32_t>> blob_usage_per_epoch_per_core = {};
    // Compilation info
    uint32_t device_id = 0;
    uint32_t temporal_epoch_id = 0;
    uint32_t logical_core_x = 0;
    uint32_t logical_core_y = 0;
    uint32_t maximum_size_bytes = 0;
    uint32_t allocated_size_bytes = 0;
    uint32_t extra_size_bytes = 0;
    std::string graph_name;
};
std::string get_string(const tt_compile_result& result);

/**
 * @brief Backend Configuration
 */
struct tt_backend_config {
    //! Backend device type (Model, Versim, Silicon, Golden, etc)
    DEVICE type = DEVICE::Invalid;

    //! Backend device architecture (Grayskull, Womrhole, etc)
    ARCH arch = ARCH::Invalid;

    //! Runtime execution mode, for precompiled models RunOnly is used
    DEVICE_MODE mode = DEVICE_MODE::CompileAndRun;

    //! Controls if graphical representation of the netlist graph should be dumped during execution.
    bool dump_graphs = false;

    //! Runtime opt level - lower for more checking (min: 0), higher for best performance (max: 4)
    int optimization_level = 0;

    //! Output directory for all generated files
    std::string output_dir = "tt_build/test_out";

    //! (Optional) Path to user-override soc grid descriptor
    std::string soc_descriptor_path = "";

    //! (Optional) Path to user-override network cluster descriptor
    std::string cluster_descriptor_path = "";

    //! Disable format conversion quantization modelling
    bool ignore_data_format_precision = false;

    //! (Optional) Additional performance profiler arguments
    std::string perf_desc_args = "";

    //! (Optional) Additional runtime arguments
    std::string runtime_args = "";

    //! Pointer to pre-compiled model wrapper, required for stand-alone runtime using TTI
    std::shared_ptr<tt::tt_device_image> tti = nullptr;

    //! (Optional) Specify the physical rows harvested per chip.
    /*! If a device from the workload is not populated here and
    the field is non-empty, backend assumes that the device is unharvested.
    The keys to this map correspond to chip indices the harvesting config is being specified for.
    The values are a vector, specifying the physical rows harvested per chip. */
    std::unordered_map<int, std::vector<uint32_t>> harvested_rows = {};

};

/**
 * @brief IO Type Enums
 */
enum class IO_TYPE {
    Queue,
    RandomAccess,
    Invalid,
};

std::string get_string(const IO_TYPE &io_type);

/**
 * @brief IO Layout Enums
 */
enum class IO_LAYOUT {
    Tilized,
    Flat,
    Invalid,
};

/**
 * @brief Programmable stride descriptor used to shuffle input activations for convolution.
 * Populated by pybuda
 */
struct Stride {
    std::vector<std::pair<int, int>> xy_offsets = {};
    int stride = -1;
};
/**
 * @brief minimal representation of queues, used to only necessary pass information between processes
 */
struct tt_dram_io_desc {
    std::string netlist_path = "";
    std::string queue_name = "";
    std::uint32_t bufq_grid_dim_r = 0;
    std::uint32_t bufq_grid_dim_c = 0;
    std::uint32_t bufq_num_slots = 0;
    std::uint32_t ublock_rt = 0;
    std::uint32_t ublock_ct = 0;
    std::uint32_t mblock_m = 0;
    std::uint32_t mblock_n = 0;
    std::uint32_t tile_height = 0;
    std::uint32_t tile_width = 0;
    std::uint32_t t = 0;
    std::uint32_t input_count = 0;
    std::uint32_t hstack_factor = 1;
    std::uint32_t vstack_factor = 1;
    bool stack_row_major = true;
    DataFormat bufq_target_format = DataFormat::Invalid;
    std::vector<std::pair<uint32_t, uint16_t>> bufq_start_addr_channel = {};
    std::vector<void*> bufq_mapping = {};
    std::uint32_t bufq_entry_size = 0;
    IO_TYPE io_type = IO_TYPE::Invalid;
    DEVICE backend_type = DEVICE::Invalid;
    Stride s_descriptor;
    IO_LAYOUT layout = IO_LAYOUT::Tilized;
};

/**
 * @brief Descriptor for Tilized Tensors containing data post tilization
 */
struct tt_TilizedTensorDesc {
    const void* ptr; //!< Points to data in this format: {tilized_data_buf_0,.....,tilized_data_buf_n}
    std::uint32_t num_buffers; //!< The number of queue buffers the tensor was tilized across
    std::uint32_t buf_size_bytes; //!< Can be used to view packed data for individual buffers
    DataFormat format; //!< Post tilized format
    OWNERSHIP owner; //!< Ownership of the tensor data, used to determine memory management responsibilities
    tt_TilizedTensorDesc() :
        ptr(nullptr),
        num_buffers(0),
        buf_size_bytes(0),
        format(DataFormat::Invalid),
        owner(OWNERSHIP::Frontend) {}
    tt_TilizedTensorDesc(
        const void* ptr,
        std::uint32_t num_buffers,
        std::uint32_t buf_size_bytes,
        DataFormat format) :
        ptr(ptr),
        num_buffers(num_buffers),
        buf_size_bytes(buf_size_bytes),
        format(format),
        owner(OWNERSHIP::Frontend) {}
};

/**
 * @brief Descriptor for Pytorch Tensors used by tilizer functions
 */
struct tt_PytorchTensorDesc {
    const void* ptr;
    const void* handle;                                        //!< Handle to original python object, if any
    std::function<void(const void*, bool)> allocate_callback;  //!< Function to call when this object is destroyed
    std::uint32_t itemsize;                                    //!< Size of each tensor element in bytes
    DataFormat format;                                         //!< Data format of the tensor elements
    std::array<std::uint32_t, PY_TENSOR_DIMS> shape;           //!< Outer-most dimension first, shape of the up to PY_TENSOR_DIMS
    std::array<std::uint32_t, PY_TENSOR_DIMS> strides;         //!< Outer-most dimension first, stride in bytes of each dimension
    unsigned int dim;                                          //!< Number of dimensions in the tensor up to PY_TENSOR_DIMS
    OWNERSHIP owner;                                           //!< Ownership of the tensor data, used to determine memory management responsibilities
    tt_PytorchTensorDesc() :
        ptr(nullptr),
        handle(nullptr),
        allocate_callback(nullptr),
        itemsize(0),
        format(DataFormat::Invalid),
        shape({}),
        strides({}),
        dim(PY_TENSOR_DIMS),
        owner(OWNERSHIP::Frontend) {}
    tt_PytorchTensorDesc(
        const void* ptr,
        std::uint32_t itemsize,
        DataFormat format,
        std::array<std::uint32_t, PY_TENSOR_DIMS> shape,
        std::array<std::uint32_t, PY_TENSOR_DIMS> strides,
        unsigned int dim,
        const void* handle = nullptr,
        std::function<void(const void*, bool)> allocate_callback = nullptr) :
        ptr(ptr),
        handle(handle),
        allocate_callback(allocate_callback),
        itemsize(itemsize),
        format(format),
        shape(shape),
        strides(strides),
        dim(dim),
        owner(OWNERSHIP::Frontend) {}

    tt_PytorchTensorDesc(const tt_PytorchTensorDesc& other) {
        // Copy constructor must increment python reference count
        if (other.allocate_callback != nullptr) {
            bool allocate = true;
            other.allocate_callback(other.handle, allocate);
        }

        ptr = other.ptr;
        handle = other.handle;
        allocate_callback = other.allocate_callback;
        itemsize = other.itemsize;
        format = other.format;
        shape = other.shape;
        strides = other.strides;
        dim = other.dim;
        owner = other.owner;
    }

    ~tt_PytorchTensorDesc() {
        if (allocate_callback != nullptr) {
            bool allocate = false;
            allocate_callback(handle, allocate);
        }
    }
};

/**
 * @brief Adapter function to convert TTI tensor metadata into a tensor descriptor
 */
bool tensor_meta_to_tensor_desc(const tt_device_image::tensor_meta &meta, std::vector<float> &tensor_data, tt_PytorchTensorDesc& pytorch_tensor, tt_TilizedTensorDesc& tilized_tensor, DataFormat buf_target_format);

/**
 * @brief Descriptor used in Buda OP perf modeling
 */
struct tt_op_model_desc {
    std::string type = "";
    std::string arch = "";
    // precision and math fidelity
    DataFormat data_format = DataFormat::Invalid;
    MathFidelity math_fidelity = MathFidelity::Invalid;
    // block shape
    std::uint32_t t = 0;
    std::uint32_t mblock_m = 0;
    std::uint32_t mblock_n = 0;
    std::uint32_t ublock_rt = 0;
    std::uint32_t ublock_ct = 0;

    // aggregation z
    std::uint32_t reduce_z = 0;

    // matmul specific
    std::uint32_t mblock_k = 0;
    std::uint32_t ublock_kt = 0;
    // sparse matmul specific
    std::uint32_t sparse_indices = 0;
    std::int32_t sparse_nz_ublocks = -1;
    std::int32_t sparse_nz_strips = -1;
    // op specific attributes and parameters
    // sfpu ops
    bool approx_mode = false;
    SfpuVectorMode vector_mode = SfpuVectorMode::RC;
    // matmul
    bool l1_accumulate = false;
    std::string op_attr = "";

    // version of the op params requested;
    // default is the first one (v1)
    std::uint32_t version = 1;
};

/**
 * @brief Device performance in a single epoch
 */
struct EpochPerfInfo {
    std::string program_name = "";    //!< Name of the program that graph has been executed from
    std::string graph_name = "";      //!< Name of the graph
    int program_id = -1;              //!< The number of programs that have been executed before the current one
    int graph_id = -1;                //!< The number of graphs that have been executed before the current one inside the current program
    int global_epoch_id = -1;         //!< The global epoch id across all programs
    std::string perf_output_directory = "";  //!< Output directory that contains the performance info for this graph
    int aiclk = -1;                   //!< aiclk of device that graph is executed on
    int device_id = -1;               //!< id for device that graph is executed on
    int input_count = -1;             //!< microbatch count of the inputs fed to the graph

    int num_epochs_current_program = -1;
    std::pair<int, int> first_and_last_inputs_recorded = {-1,-1};

    float num_cycles_per_input = 0;   //!< Average number of cycles it takes to process one input
                                      //!< Starting the measurement from after first input has been processed
                                      //!< (Last_pack_last_input_cycle - Last_pack_first_input_cycle) / (num_inputs - 1)
                                      //!< Will only get calculated for graphs with num_inputs greater than 1
                                      //!< Average number of inputs the hardware processes per second
                                      //!< (num_inputs - 1) / (Last_pack_last_input_seconds - Last_pack_first_input_seconds)
                                      //!< Will only get calculated for graphs with num_inputs greater than 1

    float num_inputs_per_second = 0;  //!< Throughput information in steady state
                                      //!< Only populated when --measure-steady-state is enabled
                                      //!< In this mode, following throughput will be calculated between input_count/4 and 3*input_count/4

    std::pair<int, int> steady_state_first_and_last_inputs = {-1, -1};
    float num_cycles_per_input_steady_state = 0;
    float num_inputs_per_second_steady_state = 0;

    uint64_t last_recorded_input_execution_cycle = 0;   //!< Last_pack - first_unpack of the last input that both of these values are recorded
    uint64_t largest_wait_for_epoch_binary_cycles = 0;  //!< Largest number of cycles each core has been stalled waiting for epoch binaries

    uint64_t first_unpack_first_input = 0;
    uint64_t last_pack_first_input = 0;
    uint64_t last_pack_last_input = 0;
};
/**
 * @brief Runtime Memory Barrier Types
 */
enum class MemBarType {
    host_cluster,                     //!< Wrapper around WFI API. Ensure that all work sent to the cluster is drained
                                      //!< Guarantees that all memory operations in the entire cluster are globally visible.

    device_device,                    //!< Non Blocking Chip Level Memory Barrier during program execution: Ensure that all
                                      //!< memory operations in programs preceeding this barrier have reached global
                                      //!< visibility in device memory on specified chip.

    host_device_dram,                 //!< Ensure that Host initiated writes to Device DRAM are globally visible
                                      //!< to host. Backend use only. Not exposed to Pybuda.

    host_device_l1,                   //!< Ensure that Host initiated writes to Device L1 are globally visible
                                      //!< to host. Backend use only. Not exposed to Pybuda.
};
}  // namespace tt

/// \cond
template <>
struct std::hash<tt::DataFormat> {
    std::size_t operator()(tt::DataFormat const& obj) const noexcept { return static_cast<std::size_t>(obj); }
};
/// \endcond