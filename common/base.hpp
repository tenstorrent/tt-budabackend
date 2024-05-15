// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
/** \file base.hpp
 * The basic enums and data structures used by the rest of BUDA code base.
 */
#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <array>
#include <vector>
#include <map>

#include <boost/functional/hash.hpp> 
#include "common/model/assert.hpp"
#include "hlks/inc/hlk_api.h"
#include "netlist/tt_backend_api_types.hpp" // These are the types exported to frontend team...
#include "l1_address_map.h"
#include "dram_address_map.h"
#include "eth_l1_address_map.h"
#include "common/model/constants.hpp"
#include "common/model/base_types.hpp"

using std::array;
using std::ostream;
using std::uint8_t;
using std::uint32_t;
using std::uint64_t;
using std::vector;
using std::string;
using std::size_t;
using std::map;

namespace tt
{

/*
┌───────────────────────────────────────────────────────┐
│______                                                 │
│| ___ \                                                │
│| |_/ / __ _ ___  ___    ___ _ __  _   _ _ __ ___  ___ │
│| ___ \/ _` / __|/ _ \  / _ \ '_ \| | | | '_ ` _ \/ __|│
│| |_/ / (_| \__ \  __/ |  __/ | | | |_| | | | | | \__ \│
│\____/ \__,_|___/\___|  \___|_| |_|\__,_|_| |_| |_|___/│
└───────────────────────────────────────────────────────┘
*/


/**
 * @brief Currently used only by the Random Buda graph Generator (RBG).
*/
enum DimMask : uint8_t
{
    DimMask_NONE = 0,
    DimMask_R = 1 << 0,
    DimMask_C = 1 << 1,
    DimMask_Z = 1 << 2,
    DimMask_W = 1 << 3,
};

/**
 * @brief Specifies the target devices on which the BUDA graph can be run. 
*/
enum class TargetDevice : uint8_t
{
    Model = 0,
    Versim = 1,
    Silicon = 2,
    Golden = 3,
    StaticAnalyzer = 4,
    Emulation = 5,
    Invalid = 0xFF,
};

constexpr uint32_t MAX_AVAILABLE_CHIPS = 16;
constexpr uint32_t ALL_EPOCHS = 0xFFFFFFFF;


#if 0
/**
 * @brief Tensix engine math fidelity on Grayskull Silicon device (specified per OP).
 * \todo WH support 
*/
enum class MathFidelity : uint8_t
{
    LoFi          = 0,
    HiFi2         = 2,
    HiFi3         = 3,
    HiFi4         = 4,
    HiFi2_A       = 2,
    HiFi2_B       = 10,
    Invalid       = 0xff,
};
#endif

/**
 * @brief Epoch instruction Opcodes. 
 * \todo Doxygen doesn't generate each enum's entry decription (see below).
*/
enum class EpochInstOpCode : uint8_t
{
    Execute      = 1, /*!< 0x1 Execute - execute epoch and move to next epoch_pc */
    Loop         = 2, /*!< 0x2 Loop - loop epochs starting from the next epoch until LoopEnd */
    LoopEnd      = 3, /*!< 0x3 LoopEnd - instruction indicating the end of epochs to be looped */
    EndProgram   = 4, /*!< 0x4 FW ends the epoch proram */
    Invalid      = 0xff,
};

/*
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│______                      _       _            _                   _                       │
│| ___ \                    | |     | |          | |                 | |                      │
│| |_/ / __ _ ___  ___    __| | __ _| |_ __ _ ___| |_ _ __ _   _  ___| |_ _   _ _ __ ___  ___ │
│| ___ \/ _` / __|/ _ \  / _` |/ _` | __/ _` / __| __| '__| | | |/ __| __| | | | '__/ _ \/ __|│
│| |_/ / (_. \__ \  __/ | (_| | (_| | || (_| \__ \ |_| |  | |_| | (__| |_| |_| | | |  __/\__ \│
│\____/ \__,_|___/\___|  \__,_|\__,_|\__\__,_|___/\__|_|   \__,_|\___|\__|\__,_|_|  \___||___/│
└─────────────────────────────────────────────────────────────────────────────────────────────┘
*/

/** 
 * @brief Each epoch is categorized as one of the types below. 
 * The behavior of certain OPs and their treatment of input operands is predicated on the epoch type. 
*/
enum class EpochType : uint8_t {
    Forward = 0,
    Backward = 1, // includes recompute
    Optimizer = 2,
    Control = 3,
    Default = 4,
};
string epoch_type_to_string(EpochType epoch_type);
string epoch_opcode_to_string(EpochInstOpCode opcode);
/** 
 * @brief An epoch instruction, a list of these composes an epoch program. 
*/
struct tt_epoch_inst
{
    EpochInstOpCode op_code;
    EpochType epoch_type;
    uint32_t payload = 0;
    uint32_t num_inputs = 1;
    uint32_t chip_id = 0;
};

/** 
 * @brief Helper struct to support loops in the epoch program.
*/
struct tt_graph_spec
{
    int start_epoch;
    int end_epoch;
    int loop_cnt;
};


/** 
 * @brief Denotes a contiguous slice (i.e., a subgrid) of a grid of cores.
*/
struct tt_grid_slice
{
    tt_grid_shape start = {0, 0}; // inclusive
    tt_grid_shape end = {0, 0};   // exclusive

    inline bool empty() const {
        return end == tt_grid_shape{0, 0};
    }
};

/** 
 * @brief An integer identifier (0 to 63) for a buffer that resides on a Tensix core.
*/
struct tt_core_buf_id
{
    uint32_t b;

    explicit tt_core_buf_id(uint32_t id);
};


/** 
 * @brief \todo add doxygen
*/
class tt_bufq_ptr
{
    public:
    uint32_t wr_ptr;
    uint32_t rd_ptr;
    uint32_t slots;

    tt_bufq_ptr(uint32_t islots);
    uint32_t incr_rd();
    uint32_t incr_wr();
    uint32_t get_wr_ptr();
    uint32_t get_rd_ptr();
    bool empty();
    bool full();
};

/** 
 * @brief A unique identifier for objects (buffers, pipes, dram io) that get emitted to pipegen.yaml.
*/
class UniqueId {
   public:
    UniqueId();
    inline uint32_t get() const { return unique_id; }
    bool operator== (const UniqueId &rhs) const;
    void override(uint32_t id) { unique_id = id; }

   private:
    uint32_t unique_id;
};

inline std::ostream & operator<<(std::ostream & os, const UniqueId & id)
{
    os << "uniqueId{" << id.get() << "}";
    return os;
}

/**
 * @brief Used to specify an output tensor index for multi-tensor-output OPs. 
 * \todo {should this be tt_output_tensor_index}
*/
enum class tt_output_index
{
    ZERO = 0, ONE = 1,
    TWO = 2, THREE = 3,
    FOUR = 4, FIVE = 5,
    SIX = 6, SEVEN = 7,
    EIGHT = 8,
    ALL = -1,
};

constexpr static int DEFAULT_PARENT_GRAPH_ID = 0;

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1,T2> &p) const
    {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ h2;
    }
};

//ostream& operator<<(ostream& os, const tt::MathFidelity &fidelity);
ostream& operator<<(ostream& os, const tt::tt_core_buf_id &bufid);
ostream &operator<<(ostream &os, const tt::EpochType &epoch_type);
ostream &operator<<(ostream &os, const tt::EpochInstOpCode &opcode);

} // end namespace tt

template<>
struct std::hash<tt::UniqueId>
{
    std::size_t operator()(tt::UniqueId const& id) const noexcept
    {
        return std::hash<std::uint32_t>{}(id.get());
    }
};

template<>
struct std::hash<tt::tt_core_coord>
{
    std::size_t operator()(tt::tt_core_coord const& id) const noexcept
    {
        auto h1 = std::hash<std::uint32_t>{}(id.row);
        auto h2 = std::hash<std::uint32_t>{}(id.col);
        return h1 ^ h2;
    }
};


// TODO(agrebenisan): Had to include it here instead of in model.hpp since was getting errors that this type wasn't declared. Consider 
// moving any runtime structs to a runtime config header file
struct tt_epoch_runtime_config 
{
    int32_t save_checkpoint = 0;
    int32_t zero_grad = 0;
};

