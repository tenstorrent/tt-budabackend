// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace tt {
// DRAM accessible via HOST-to-DEVICE MMIO
constexpr uint32_t DRAM_HOST_MMIO_SIZE_BYTES    = 256 * 1024 * 1024;
constexpr uint32_t DRAM_HOST_MMIO_CHANNEL       = 0;
constexpr uint32_t DRAM_HOST_MMIO_RANGE_START   = 0x30000000;
constexpr uint32_t WH_DRAM_HOST_MMIO_RANGE_END = 0x40000000; //MMIO range for WH starts at 0x30000000 and ends at 0x40000000. This is where the DRAM bank for GS ends.

// DRAM accessible via DEVICE-to-DEVICE MMIO
constexpr uint32_t DRAM_PEER2PEER_SIZE_BYTES    = 256 * 1024 * 1024;
constexpr uint32_t DRAM_PEER2PEER_CHANNEL       = 0;
constexpr uint32_t DRAM_PEER2PEER_REGION_START  =  0x30000000;
constexpr uint32_t QUEUE_HEADER_WORDS = 8;
}

enum class tt_runtime_state {
    Uninitialized = 0,
    Initialized = 1,
    RunBusy = 2,
    RunComplete = 3,
};

enum class tt_data_location {
    L1 = 0,
    DRAM = 1,
    HOST = 2,
    Invalid = 0xFF,
};

enum class tt_queue_header_field : uint8_t {
    GlobalRdptr = 0b1 << 0,
    GlobalWrptr = 0b1 << 1,
    LocalRdptr =  0b1 << 2,
    ZeroSetting = 0b1 << 3,
    Invalid = 0xFF,
};

// For Varinst-on-device feature, to differentiate between which IO Queue Setting fields are targeted
struct tt_queue_varinst_update_field_mask {

    using Field = tt_queue_header_field;
    std::vector<Field> supported_fields = {Field::GlobalRdptr, Field::GlobalWrptr, Field::LocalRdptr, Field::ZeroSetting};
    uint8_t value = 0;

    void set_field(Field field_type) {
        value |= static_cast<uint8_t>(field_type);
    }
    void set_fields(std::vector<Field> field_types) {
        for (auto &field_type : field_types) {
            value |= static_cast<uint8_t>(field_type);
        }
    }
    bool has_field(Field field_type) const { return ((value & static_cast<uint8_t>(field_type)) == static_cast<uint8_t>(field_type));}
    bool has_only_one_field_set() const { return value && (!(value & (value - 1)));}
    bool not_empty() const { return value != 0;}

    Field get_single_field_from_mask(std::string var_name) const {

        // To start, must ensure variable is only bound to single queue-setting field. Later, they may be merged into single cmd.
        log_assert(has_only_one_field_set() == true,
            "Var {} must be mapped to a single header/settings field for queue(s) for varinst-on-device currently.", var_name);

        for (auto &field_type : supported_fields) {
            if (has_field(field_type)) return field_type;
        }

        log_assert(false, "Var {} wasn't mapped to any supported queue header/settings fields. mask: 0x{:x}", var_name, value);
        return Field::Invalid;
    }
};

enum class tt_queue_settings_sync_type {
    SyncOnProducers = 0,
    SyncOnConsumers = 1,
    SyncOnBoth = 2,
    SyncOnNone = 0xF,
};

const std::unordered_map<tt_queue_header_field, tt_queue_settings_sync_type> tt_queue_varinst_sync_type_map = {
    {tt_queue_header_field::GlobalRdptr, tt_queue_settings_sync_type::SyncOnConsumers},
    {tt_queue_header_field::GlobalWrptr, tt_queue_settings_sync_type::SyncOnProducers},
    {tt_queue_header_field::LocalRdptr,  tt_queue_settings_sync_type::SyncOnConsumers},
    {tt_queue_header_field::ZeroSetting, tt_queue_settings_sync_type::SyncOnBoth},

};

struct tt_epoch_config {
  uint32_t overlay_size_bytes;
  uint32_t dram_header_polling_freq;
};

// Queue Header
// ------------
// The queue header is a 32-byte structure that store the queue state and meta data
// This must be kept in sync with the FW implementation in `src/firmware/common/epoch.h`
struct tt_queue_header {
    std::uint16_t global_rd_ptr         ;  // Word 0
    std::uint16_t global_rdptr_autoinc  ;
    std::uint16_t global_wr_ptr         ;  // Word 1
    std::uint16_t global_wrptr_autoinc  ;
    std::uint16_t local_rd_ptr          ;  // Word 2
    std::uint16_t epoch_id_tag          ;
    std::uint16_t stride                ;  // Word 3
    std::uint16_t zero                  : 1 ;
    std::uint16_t decouple_queue        : 1 ;
    std::uint16_t __reserved_word_3     : 14;
    std::uint16_t local_rdptr_autoinc   ;  // Word 4
    std::uint16_t __reserved_half_4     ;
    std::uint16_t queue_update_stride   ;  // Word 5
    std::uint16_t __reserved_half_5     ;
    std::uint32_t __reserved_word_6     ;  // Word 6
    std::uint32_t __reserved_word_7     ;  // Word 7
};

struct tt_queue_header_mask {
    uint8_t value = 0;

    static constexpr uint8_t GLOBAL_RD_PTR_MASK  = 0b1   << 0;
    static constexpr uint8_t GLOBAL_WR_PTR_MASK  = 0b1   << 1;
    static constexpr uint8_t LOCAL_RD_PTR_MASK   = 0b1   << 2;
    static constexpr uint8_t LOCAL_SETTINGS_MASK = 0b111 << 2;
    static constexpr uint8_t RD_PTR_MASK = GLOBAL_RD_PTR_MASK | LOCAL_RD_PTR_MASK;
    static constexpr uint8_t FULL_MASK = 0xFF;
    static constexpr uint8_t NULL_MASK = 0x00;

    void set(uint8_t mask)    { value = mask; }
    void set_global_rd_ptr()  { value |= GLOBAL_RD_PTR_MASK; }
    void set_global_wr_ptr()  { value |= GLOBAL_WR_PTR_MASK; }
    void set_local_rd_ptr()   { value |= LOCAL_RD_PTR_MASK; }
    void set_local_settings() { value |= LOCAL_SETTINGS_MASK; }
    void set_full()           { value |= FULL_MASK; }

    void clear_global_rd_ptr()  { value &= ~GLOBAL_RD_PTR_MASK; }
    void clear_global_wr_ptr()  { value &= ~GLOBAL_WR_PTR_MASK; }
    void clear_local_rd_ptr()   { value &= ~LOCAL_RD_PTR_MASK; }
    void clear_local_settings() { value &= ~LOCAL_SETTINGS_MASK; }

    uint8_t get()             const { return value; }
    bool has_global_rd_ptr()  const { return (value & GLOBAL_RD_PTR_MASK) == GLOBAL_RD_PTR_MASK; }
    bool has_global_wr_ptr()  const { return (value & GLOBAL_WR_PTR_MASK) == GLOBAL_WR_PTR_MASK; }
    bool has_local_rd_ptr()   const { return (value & LOCAL_RD_PTR_MASK)  == LOCAL_RD_PTR_MASK; }
    bool has_local_settings() const { return (value & LOCAL_SETTINGS_MASK) == LOCAL_SETTINGS_MASK; }
    bool has_full()           const { return has_global_rd_ptr() && has_global_wr_ptr() && has_local_rd_ptr() && has_local_settings(); }

    tt_queue_settings_sync_type get_sync_type() const {
        bool sync_producers = has_global_wr_ptr() || has_local_settings();
        bool sync_consumers = has_global_rd_ptr() || has_local_rd_ptr() || has_local_settings();
        if (sync_producers && sync_consumers) {
            return tt_queue_settings_sync_type::SyncOnBoth;
        } else if (sync_producers) {
            return tt_queue_settings_sync_type::SyncOnProducers;
        } else if (sync_consumers) {
            return tt_queue_settings_sync_type::SyncOnConsumers;
        } else {
            return tt_queue_settings_sync_type::SyncOnNone;
        }
    }

    // tt_queue_header_mask() {};
};

union tt_queue_header_wrap {

    tt_queue_header_wrap() {};
    tt_queue_header_wrap(vector<uint32_t> vec) {
        for (int i = 0; i<QUEUE_HEADER_WORDS; ++i) {
            val[i] = vec.at(i);
        }
    };
    tt_queue_header_wrap(tt_queue_header hdr) {
        header = hdr;
    };

    uint32_t val[QUEUE_HEADER_WORDS] = {0};
    tt_queue_header header;

    std::vector<uint32_t> get_vec() const {
        constexpr int n = sizeof(tt_queue_header) / sizeof(uint32_t);
        std::vector<uint32_t> vec(val, val + n);
        return vec;
    };

    // Get a 16b word from header given an offset idx in 16b words.
    uint16_t get_16b_word(uint8_t idx_16b) const {
        int idx_32b = idx_16b / 2;
        return ((idx_16b % 2) ? (val[idx_32b] >> 16) : val[idx_32b]) & 0xFFFF;
    };

    std::pair<std::vector<uint32_t>, int> get_rdptr_vec() const {
        constexpr int offset = 0; // global rdptr offset
        constexpr int offset_bytes = offset*sizeof(uint32_t);
        return {{val[offset]}, offset_bytes};
    };

    std::pair<std::vector<uint32_t>, int> get_wrptr_vec() const {
        constexpr int offset = 1; // global wrptr offset
        constexpr int offset_bytes = offset*sizeof(uint32_t);
        return {{val[offset]}, offset_bytes};
    };

    std::pair<std::vector<uint32_t>, int> get_lrdptr_vec() const {
        constexpr int offset = 2; // local rdptr offset
        constexpr int offset_bytes = offset*sizeof(uint32_t);
        return {{val[offset]}, offset_bytes};
    };

    std::pair<std::vector<uint32_t>, int> get_settings_vec() const {
        constexpr int offset = 2; // local settings offset
        constexpr int offset_bytes = offset*sizeof(uint32_t);
        return {{val[offset], val[offset+1], val[offset+2]}, offset_bytes};
    };

    void set_vec(std::pair<std::vector<uint32_t>, int> vec_with_offset) {
        auto vec = vec_with_offset.first;
        auto num_words = vec.size();
        auto offset_words = vec_with_offset.second / sizeof(uint32_t);
        log_assert(offset_words + num_words <= QUEUE_HEADER_WORDS, "Out of bounds");
        std::copy(vec.begin(), vec.end(), val + offset_words);
    };

    bool matches(const tt_queue_header_wrap& rhs,  const tt_queue_header_mask &header_mask) {

        bool matching = true;

        if (header_mask.has_full()) {
            matching &= (get_vec() == rhs.get_vec());
        } else {
            if (header_mask.has_global_rd_ptr()) {
                matching &= (get_rdptr_vec() == rhs.get_rdptr_vec());
            }
            if (header_mask.has_global_wr_ptr()) {
                matching &= (get_wrptr_vec() == rhs.get_wrptr_vec());
            }
            if (header_mask.has_local_settings()) {
                matching &= (get_settings_vec() == rhs.get_settings_vec());
            } else if (header_mask.has_local_rd_ptr()) {
                matching &= (get_lrdptr_vec() == rhs.get_lrdptr_vec());
            }
        }

        return matching;

    }

};

inline bool operator == (const tt_queue_header& lhs, const tt_queue_header& rhs) {
    const uint32_t* lhs_val = (const uint32_t*)(&lhs);
    const uint32_t* rhs_val = (const uint32_t*)(&rhs);
    for (int i=0; i<QUEUE_HEADER_WORDS; i++) {
        if (*lhs_val != *rhs_val) {
            return false;
        }
        lhs_val++;
        rhs_val++;
    }
    return true;
}

inline bool operator != (const tt_queue_header& lhs, const tt_queue_header& rhs) {
    return !(lhs == rhs);
}

inline ostream& operator<<(ostream& os, const tt_queue_header& t) {
    os << "tt_queue_header{ ";
    os << ".global_rd_ptr = " << t.global_rd_ptr << ", ";
    os << ".global_wr_ptr = " << t.global_wr_ptr << ", ";
    os << ".local_rd_ptr = " << t.local_rd_ptr << ", ";
    os << ".epoch_id_tag = " << t.epoch_id_tag << ", ";
    os << ".stride = " << t.stride << ", ";
    os << ".zero = " << t.zero << ", ";
    os << ".decouple_queue = " << t.decouple_queue << ", ";
    os << ".global_rdptr_autoinc = " << (int)t.global_rdptr_autoinc << ", ";
    os << ".global_wrptr_autoinc = " << (int)t.global_wrptr_autoinc << ", ";
    os << ".local_rdptr_autoinc = " << (int)t.local_rdptr_autoinc << ", ";
    os << "}";
    return os;
}

inline void print_queue_header(ostream& os, vector<uint32_t> header_vec) {
    log_assert(header_vec.size() == QUEUE_HEADER_WORDS, "Incorrect queue header size");
    // log_assert(header_vec.size() == 8, "Queue header must have 32 Bytes");
    uint32_t header_arr[QUEUE_HEADER_WORDS];
    std::copy(header_vec.begin(), header_vec.end(), header_arr);
    tt_queue_header header = *(reinterpret_cast<tt_queue_header*>(header_arr));
    os << header;
}