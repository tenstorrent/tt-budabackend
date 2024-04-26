// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/cache_lib.hpp"
#include "common/size_lib.hpp"
#include "runtime_io.hpp"

namespace tt::eager::io {

const std::string CACHE_TAG = "eager_backend";

// IO process initialize (cache init, device cluster init)
void initialize(const tt::ARCH &arch, const std::set<chip_id_t> &chip_id_ids = {});

// IO process finish (cache clear, memory free, dumping/reporting)
void finish();

// Queue/Ram header init
void init_queue(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, const int &entries);

// Queue/Ram push
void push_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, const tt_py_desc &data_tensor, const IO_TYPE io_type, const int ram_ptr=0);

// Queue/Ram read without pop
void get_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, tt_py_desc &data_tensor, const IO_TYPE io_type, const int ram_ptr=0, const bool untilized_output=true);

// Queue pop (does not apply to Ram)
void pop_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor);

} // namespace tt::io
