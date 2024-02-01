// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "base.hpp"

using std::invalid_argument;

namespace tt
{

    tt_core_buf_id::tt_core_buf_id(uint32_t id)
    {
        log_assert (id < 64, "buf id out of range");
        this->b = id;
    }

    tt_bufq_ptr::tt_bufq_ptr(uint32_t islots)
    {
        wr_ptr = 0;
        rd_ptr = 0;
        slots = islots;
    }
    uint32_t tt_bufq_ptr::incr_rd()
    {
        uint32_t tmp = rd_ptr;
        rd_ptr++;
        if(rd_ptr == (2*slots))
        {
            rd_ptr = 0;
        }

        // Wrap the returned pointer into actual buffer range
        if(tmp >= slots) tmp = tmp - slots;
        return tmp;
    }
    uint32_t tt_bufq_ptr::incr_wr()
    {
        uint32_t tmp = wr_ptr;
        wr_ptr++;
        if(wr_ptr == (2*slots))
        {
            wr_ptr = 0;
        }

        // Wrap the returned pointer into actual buffer range
        if(tmp >= slots) tmp = tmp - slots;
        return tmp;
    }
    bool tt_bufq_ptr::empty()
    {
        bool empty = (rd_ptr == wr_ptr);
        return empty;
    }
    bool tt_bufq_ptr::full()
    {
        bool full = false;
        uint32_t reduced_by_slots;
        if(rd_ptr != wr_ptr)
        {
            if(wr_ptr > rd_ptr)
            {
                reduced_by_slots = wr_ptr - slots;
                full = (reduced_by_slots == rd_ptr);
            }
            else
            {
                reduced_by_slots = rd_ptr - slots;
                full = (reduced_by_slots == wr_ptr);
            }
        }
        return full;
    }
    uint32_t tt_bufq_ptr::get_wr_ptr()
    {
        uint32_t tmp = wr_ptr;
        // Wrap the returned pointer into actual buffer range
        if(tmp >= slots) tmp = tmp - slots;
        return tmp;
    }
    uint32_t tt_bufq_ptr::get_rd_ptr()
    {
        uint32_t tmp = rd_ptr;
        // Wrap the returned pointer into actual buffer range
        if(tmp >= slots) tmp = tmp - slots;
        return tmp;
    }

    UniqueId::UniqueId() {
        static uint32_t next_unique_id = 0;
        unique_id = next_unique_id++;
        log_assert(next_unique_id != 0 ,  "we overflowed");
    }

    bool UniqueId::operator== (const UniqueId &rhs) const
    {
        return this->get() == rhs.get();
    }

    bool tt_logical_core_coords::operator==(const tt_logical_core_coords &rhs) const {
        return (this->relative == rhs.relative) && (this->absolute == rhs.absolute);
    }

    string epoch_type_to_string(EpochType epoch_type) {
        switch (epoch_type) {
            case EpochType::Forward: return "EpochType::Forward";
            case EpochType::Backward: return "EpochType::Backward";
            case EpochType::Optimizer: return "EpochType::Optimizer";
            case EpochType::Control: return "EpochType::Control";
            case EpochType::Default: return "EpochType::Default";
            default: log_fatal("unrecognized enum type for EpochType");
        }
    }

    string epoch_opcode_to_string(EpochInstOpCode opcode) {
        switch (opcode) {
            case EpochInstOpCode::Execute: return "EpochInstOpCode::Execute";
            case EpochInstOpCode::Loop: return "EpochInstOpCode::Loop";
            case EpochInstOpCode::LoopEnd: return "EpochInstOpCode::LoopEnd";
            case EpochInstOpCode::EndProgram: return "EpochInstOpCode::EndProgram";
            case EpochInstOpCode::Invalid: return "EpochInstOpCode::Invalid";
            default: log_fatal("unrecognized enum type for EpochInstOpCode");
        }
    }

#if 0
ostream& operator<<(ostream &os, const MathFidelity &fidelity) {
    switch (fidelity) {
        case MathFidelity::LoFi: os << "LoFi"; break;
        case MathFidelity::HiFi2: os << "HiFi2"; break;
        case MathFidelity::HiFi3: os << "HiFi3"; break;
        case MathFidelity::HiFi4: os << "HiFi4"; break;
        case MathFidelity::HiFi2_A: os << "HiFi2_A"; break;
        case MathFidelity::HiFi2_B: os << "HiFi2_B"; break;
        case MathFidelity::Invalid: os << "Invalid"; break;
        default: throw invalid_argument("Unknown format");
    }
    return os;
}
#endif

ostream& operator<<(ostream& os, const EpochType &epoch_type) {
    os << epoch_type_to_string(epoch_type);
    return os;
}

ostream &operator<<(ostream &os, const tt::EpochInstOpCode &opcode) {
    os << epoch_opcode_to_string(opcode);
    return os;
}

}  // end namespace tt
