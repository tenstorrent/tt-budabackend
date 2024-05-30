// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/functional/hash.hpp>
#include <cstdint>
#include <limits>

#include "device/tt_xy_pair.h"

namespace pipegen2
{
/*
    You can put here definitions for types used in multiple model graphs.
*/

using NodeId = std::uint64_t;

using PhaseId = unsigned long;

using ChipId = std::size_t;

using StreamId = unsigned int;

// Prologue buffer types.
enum class PrefetchType
{
    // PRE_TM are prologue buffers that are copied to L1 before any TM is apllied to them.
    // The TM will be applied when they are sent to the unpacker.
    PRE_TM = 0,

    // POST_TM are prologue buffers where the TM is applied first and then saved in L1.
    // This is done because it can improve the bandwidth utilization when sending this to the unpacker.
    POST_TM
};

enum class NOC_ROUTE : unsigned int
{
    NOC0 = 0,
    NOC1 = 1,
    INVALID = std::numeric_limits<unsigned int>::max()
};

struct tt_cxys_pair : public tt_cxy_pair
{
    tt_cxys_pair() : tt_cxy_pair{}, stream_id{} {}

    tt_cxys_pair(StreamId stream_id, const tt_cxy_pair &pair) :
        tt_cxy_pair(pair.chip, pair.x, pair.y), stream_id(stream_id)
    {
    }

    tt_cxys_pair(StreamId stream_id, std::size_t ichip, std::size_t x, std::size_t y) :
        tt_cxy_pair(ichip, x, y), stream_id(stream_id)
    {
    }

    StreamId stream_id;

    std::string str() const
    {
        return "(chip=" + std::to_string(chip) + ",x=" + std::to_string(x) + ",y=" + std::to_string(y) +
               ",stream=" + std::to_string(stream_id) + ")";
    }

    tt_cxy_pair to_cxy_pair() const { return tt_cxy_pair{chip, x, y}; }
};

constexpr inline bool operator==(const tt_cxys_pair &a, const tt_cxys_pair &b)
{
    return a.x == b.x && a.y == b.y && a.chip == b.chip && a.stream_id == b.stream_id;
}

constexpr inline bool operator!=(const tt_cxys_pair &a, const tt_cxys_pair &b) { return !(a == b); }

constexpr inline bool operator<(const tt_cxys_pair &left, const tt_cxys_pair &right)
{
    return (
        left.chip < right.chip || (left.chip == right.chip && left.x < right.x) ||
        (left.chip == right.chip && left.x == right.x && left.y < right.y) ||
        (left.chip == right.chip && left.x == right.x && left.y == right.y && left.stream_id < right.stream_id));
}

}  // namespace pipegen2

namespace std
{
template <>
struct hash<pipegen2::tt_cxys_pair>
{
    std::size_t operator()(pipegen2::tt_cxys_pair const &o) const
    {
        // Not using std::hash, since it is usually implemented as an identity function.
        std::size_t seed = 0;
        boost::hash_combine(seed, o.x);
        boost::hash_combine(seed, o.y);
        boost::hash_combine(seed, o.chip);
        boost::hash_combine(seed, o.stream_id);
        return seed;
    }
};
}  // namespace std