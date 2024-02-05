// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "net2hlks.h"

#include <cstdint>
#include <cstdlib>
#include <experimental/filesystem>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <regex>
#include <unordered_set>
#include <utility>

#include "hlks/inc/hlk_api.h"
#include "netlist_fused_op_info_types.hpp"
#include "netlist_op_info_types.hpp"
#include "netlist_utils.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"

#define NET2HLKS_TT_DUMP_LOG(hlks, format, ...) \
    hlks << "TT_DUMP_LOG(\"" << format << "\", " << #__VA_ARGS__ << ");" << std::endl;

#define NET2HLKS_TT_DUMP_ASSERT(hlks, cond, format, ...) \
    hlks << "TT_DUMP_ASSERT(" << #cond << ", \"" << format << "\", " << #__VA_ARGS__ << ");" << std::endl;

#define NET2HLKS_TT_LOG(hlks, format, ...) hlks << "TT_LOG(\"" << format << "\", " << #__VA_ARGS__ << ");" << std::endl;

#define NET2HLKS_TT_RISC_ASSERT(hlks, cond, format, ...) \
    hlks << "TT_RISC_ASSERT(" << #cond << ", \"" << format << "\", " << #__VA_ARGS__ << ");" << std::endl;

static constexpr int debug = 0;

// Destination register
static bool is_dest_reg(const std::string &name) { return name == "dest"; }

static bool is_input_dest_reg(const tt_scheduled_op_info &op) {
    return std::find_if(op.input_names.begin(), op.input_names.end(), is_dest_reg) != op.input_names.end();
}

static bool is_input_dest_reg(const tt_scheduled_op_info &op, int index) {
    return is_dest_reg(op.input_names.at(index));
}

static bool is_output_dest_reg(const tt_scheduled_op_info &op) { return is_dest_reg(op.output); }

static bool is_intermed(const string &name) { return (name.find("intermed") != std::string::npos); }

// ex. for input0 returns 0
static int get_external_input_index(const std::string &input_name) {
    int n = 0;
    sscanf(input_name.c_str(), "input%d", &n);
    return n;
}

// ex. input_names = {"input7", "input2"}, index = 0, returns 7
static int get_external_input_index(const tt_scheduled_op_info &op, int index) {
    return get_external_input_index(op.input_names.at(index));
}

static bool should_pop_input(const tt_scheduled_op_info &op, int index) {
    const std::string &in_name = op.input_names.at(index);
    return op.inputs_to_pop.find(in_name) != op.inputs_to_pop.end();
}

static bool should_pop_last_input(const tt_scheduled_op_info &op, int index) {
    const std::string &in_name = op.input_names.at(index);
    return op.inputs_to_pop_last.find(in_name) != op.inputs_to_pop_last.end();
}

static bool is_matmul(const tt_scheduled_op_info &op) { return netlist_utils::is_valid_matmul_op(op.type); }

static bool is_reduce(const tt_scheduled_op_info &op) { return netlist_utils::is_valid_reduce_op(op.type); }

static std::string get_input_operand_name(int external_input_index) {
    constexpr int MAX_NUM_INPUTS = 8;
    if (external_input_index < MAX_NUM_INPUTS)
        return "in" + to_string(external_input_index);
    else
        return "param" + to_string(external_input_index - MAX_NUM_INPUTS);
}

static std::map<std::string, std::string> get_input_to_operand_map(const tt_scheduled_op_info &op) {
    std::map<std::string, std::string> input_to_operand;
    for (auto in_name : op.input_names) {
        if (!is_intermed(in_name)) {
            int external_input_index = get_external_input_index(in_name);
            input_to_operand[in_name] = get_input_operand_name(external_input_index);
        } else {
            input_to_operand[in_name] = in_name;
        }
    }
    return input_to_operand;
}

static std::unordered_map<std::string, int> get_kernel_broadcast_inputs(const tt_fused_op_info &fused_op) {
    std::unordered_map<std::string, int> result;
    for (int i = 0; i < fused_op.kernel_broadcast.size(); i++) {
        if (fused_op.kernel_broadcast.at(i).first) {
            int bcast_dim = fused_op.kernel_broadcast.at(i).first;
            result["input" + to_string(i)] = bcast_dim;
        }
    }
    return result;
}

// Waiting for kernel broadcast tiles happens before all for loops
static void kernel_broadcast_wait_tiles(ofstream &hlks, const tt_fused_op_info &fused_op) {
    hlks << "wait_for_tiles_kernel_broadcast<MAX_NUM_INPUTS>(core_ptr);" << endl;
}

static void kernel_broadcast_wait_tiles_t(ofstream &hlks, const tt_fused_op_info &fused_op) {
    hlks << "wait_for_tiles_kernel_broadcast_t<MAX_NUM_INPUTS>(core_ptr);" << endl;
}

// Kernel broadcast pop tiles happens after all for loops are done
static void kernel_broadcast_pop_tiles(ofstream &hlks, const tt_fused_op_info &fused_op) {
    hlks << "pop_tiles_kernel_broadcast<MAX_NUM_INPUTS>(core_ptr);" << endl;
}

static void kernel_broadcast_pop_tiles_t(ofstream &hlks, const tt_fused_op_info &fused_op) {
    hlks << "pop_tiles_kernel_broadcast_t<MAX_NUM_INPUTS>(core_ptr);" << endl;
}

// Begin of macro block index check.
// It is added on begin schedule of matmul and reduce.
// Also it can be added before of other ops.
static void begin_macro_block_index_check(ofstream &hlks, const tt_scheduled_op_info &op) {
    hlks << "if ((m_block < " << op.output_dim.mblock_m << ") && " << endl;
    hlks << "(n_block < " << op.output_dim.mblock_n << ")) {" << endl;
}

// End of macro block check
static void end_macro_block_index_check(ofstream &hlks, const tt_scheduled_op_info &op) {
    if (is_matmul(op)) {
        hlks << "} // end for matmul reduce mblock dim check" << endl;
    } else if (is_reduce(op)) {
        hlks << "} // end for reduce mblock dim check" << endl;
    } else {
        hlks << "} // end for op mblock dim check" << endl;
    }
}

static bool is_schedule_with_matmul(const tt_schedule_info &schedule) {
    return is_matmul(schedule.scheduled_ops.back());
}

static bool is_schedule_with_matmul_reduce(const tt_schedule_info &schedule) {
    if (is_schedule_with_matmul(schedule)) {
        const tt_scheduled_op_info &last_op = schedule.scheduled_ops.back();
        return last_op.m_k > 1 || last_op.u_kt > 1;
    }
    return false;
}

static bool is_schedule_with_reduce(const tt_schedule_info &schedule) {
    return is_reduce(schedule.scheduled_ops.back());
}

static bool schedule_has_loop(const tt_schedule_info &schedule) {
    const tt_scheduled_op_info &last_op = schedule.scheduled_ops.back();
    if (is_matmul(last_op) || is_reduce(last_op)) {
        return last_op.m_k > 1;
    }
    return false;
}

static bool schedule_has_matmul_loop(const tt_schedule_info &schedule) {
    return is_schedule_with_matmul(schedule) && schedule_has_loop(schedule);
}

static int schedule_matmul_m_k(const tt_schedule_info &schedule) {
    const tt_scheduled_op_info &last_op = schedule.scheduled_ops.back();
    if (is_matmul(last_op)) {
        return last_op.m_k;
    }
    return 0;
}

static bool schedule_has_matmul_bcast(const tt_schedule_info &schedule) {
    const tt_scheduled_op_info &last_op = schedule.scheduled_ops.back();
    if (is_matmul(last_op)) {
        if (last_op.input_tm_ops.find(0) != last_op.input_tm_ops.end()) {
            for (const auto &tm : last_op.input_tm_ops.at(0)) {
                string bcast_type = tm.first;
                if ((bcast_type == "r_broadcast") || (bcast_type == "c_broadcast")) {
                    return true;
                }
            }
        }
    }

    return false;
}

static bool is_first_op_in_schedule(const tt_schedule_info &schedule, const tt_scheduled_op_info &op) {
    return &schedule.scheduled_ops.front() == &op;
}

static bool is_matmul_with_intermed_input0_and_non_column_input1(const tt_scheduled_op_info &matmul_op) {
    return is_intermed(matmul_op.input_names[0]) && matmul_op.output_dim.mblock_n > 1 && matmul_op.m_k > 1;
}

static void begin_schedule(ofstream &hlks, const tt_schedule_info &schedule) {
    if (is_schedule_with_matmul(schedule) || is_schedule_with_reduce(schedule)) {
        begin_macro_block_index_check(hlks, schedule.scheduled_ops.back());

        if (schedule_has_loop(schedule)) {
            if (is_schedule_with_matmul(schedule)) {
                hlks << "for(int oid = 0; oid < " << schedule.scheduled_ops.back().m_k << "; ++oid) {" << endl;
                if (is_matmul_with_intermed_input0_and_non_column_input1(schedule.scheduled_ops.back())) {
                    hlks << "if (n_block == 0) {" << endl;
                } 

            } else {
                hlks << "for(int block= 0; block< " << schedule.scheduled_ops.back().m_k << "; ++block) {" << endl;
            }
        }
    }
}

static void end_schedule(ofstream &hlks, const tt_schedule_info &schedule, int matmul_or_reduce_id) {
    if (is_schedule_with_matmul(schedule) || is_schedule_with_reduce(schedule)) {
        if (schedule_has_loop(schedule)) {
            if (is_schedule_with_matmul(schedule)) {
                hlks << "} // matmul m_k loop end" << endl;
            } else {
                hlks << "} // reduce block loop end" << endl;
            }
            hlks << "enable_reload[" << matmul_or_reduce_id - 1 << "]=false;" << endl;
        }

        end_macro_block_index_check(hlks, schedule.scheduled_ops.back());
    }
}

static bool is_macro_block_index_check_needed_for_op(
    const tt_fused_op_info &fused_op, const tt_schedule_info &schedule, const tt_scheduled_op_info &op) {
    if (is_schedule_with_matmul(schedule) || is_schedule_with_reduce(schedule)) {
        // if the op is within a schedule ending with matmul/reduce, we only need to check whether its dimensions match
        // the schedule output dimensions since we always add a mblock index check for such schedule
        const auto &schedule_output_dim = schedule.scheduled_ops.back().output_dim;
        return op.output_dim.mblock_m < schedule_output_dim.mblock_m ||
               op.output_dim.mblock_n < schedule_output_dim.mblock_n;
    } else {
        // otherwise, the surrounding loop comes from the fused op output dimensions
        const auto &fused_op_output_dim = fused_op.schedules.back().scheduled_ops.back().output_dim;
        return op.output_dim.mblock_m < fused_op_output_dim.mblock_m ||
               op.output_dim.mblock_n < fused_op_output_dim.mblock_n;
    }
}

// Begin of batch and macro block loops
static void begin_batch_loops(ofstream &hlks) {
    NET2HLKS_TT_DUMP_ASSERT(hlks, args->batch_cnt > 0, "batch_cnt {} must be greater than 0", args->batch_cnt);
    hlks << "for(int batch = 0; batch < args->batch_cnt; ++batch) {" << endl;
    hlks << "bool enable_reload[8] = {false, false, false, false, false, false, false, false};"
         << endl;  // for matmul only
}

static void begin_block_loops(ofstream &hlks) {
    hlks << "for(int m_block = 0; m_block < args->num_m_sub_blocks; ++m_block) {" << endl;
    hlks << "for(int n_block = 0; n_block < args->num_n_sub_blocks; ++n_block) {" << endl;
}

// End of batch and macro block loops
static void end_batch_loops(ofstream &hlks) { hlks << "} // batch loop end" << endl; }

static void end_block_loops(ofstream &hlks) {
    hlks << "} // n_block loop end" << endl;
    hlks << "} // m_block loop end" << endl;
}

static void init_debug(ofstream &hlks) { hlks << "unsigned int postcode = 0xfaca0000;" << endl; }

static bool output_mblock_dimensions_match(const tt_scheduled_op_info &op1, const tt_scheduled_op_info &op2) {
    return op1.output_dim.mblock_m == op2.output_dim.mblock_m && op1.output_dim.mblock_n == op2.output_dim.mblock_n;
}

////
Net2Hlks::Net2Hlks(const std::string &netlist_file, const std::string &output_dir) {
    this->netlist_file = netlist_file;
    this->output_dir = output_dir;
    this->parsed_netlist.parse_file(netlist_file);
}

Net2Hlks::Net2Hlks(const netlist_parser &parsed_netlist, const std::string &output_dir) {
    this->parsed_netlist = parsed_netlist;
    this->output_dir = output_dir;
}

Net2Hlks::~Net2Hlks() {}

const tt_dim_info &Net2Hlks::get_input_dim(const tt_scheduled_op_info &curr_op, int input_index) {
    return this->fused_subops_info[curr_op.name].inputs[input_index].input_dim;
}

bool Net2Hlks::is_matmul_bcast_ublock(const tt_scheduled_op_info &curr_op) {
    tt_dim_info input0_dim = this->get_input_dim(curr_op, 0);
    return input0_dim.ublock_ct > 0 && curr_op.u_kt > input0_dim.ublock_ct;
}
bool Net2Hlks::is_matmul_bcast_mblock(const tt_scheduled_op_info &curr_op) {
    tt_dim_info input0_dim = this->get_input_dim(curr_op, 0);
    return input0_dim.mblock_n > 0 && curr_op.m_k > input0_dim.mblock_n;
}

void Net2Hlks::get_tms_params(
    int input,
    const tt_scheduled_op_info &curr_op,
    string &bcast_dim,
    bool &mblock_bcast,
    int &block_tile_dim,
    int &bcast_factor) {
    bool bcast_on_input = false;
    if (curr_op.input_tm_ops.find(input) != curr_op.input_tm_ops.end()) {
        for (auto tm : curr_op.input_tm_ops.at(input)) {
            string bcast_type = tm.first;
            bcast_on_input = true;
            if ((bcast_type == "r_broadcast") || (bcast_type == "c_broadcast")) {  // only scale when we broadcast tiles
                int bcast_value = tm.second.at(0);
                bcast_factor *= bcast_value;
                bcast_dim = bcast_type;
            }
        }
    }

    mblock_bcast = bcast_dim == "r_broadcast"   ? bcast_factor > curr_op.output_dim.ublock_rt
                   : bcast_dim == "c_broadcast" ? bcast_factor > curr_op.output_dim.ublock_ct
                                                : false;

    if (mblock_bcast && !is_matmul(curr_op)) {
        bcast_factor = bcast_dim == "r_broadcast"   ? bcast_factor / curr_op.output_dim.mblock_m
                       : bcast_dim == "c_broadcast" ? bcast_factor / curr_op.output_dim.mblock_n
                                                    : bcast_factor;
    }

    if (is_matmul(curr_op)) {
        mblock_bcast = (input == 0) && bcast_on_input && is_matmul_bcast_mblock(curr_op);

        if (input == 0 && bcast_on_input && is_matmul_bcast_ublock(curr_op)) {
            tt_dim_info input_dim = this->get_input_dim(curr_op, input);
            block_tile_dim = input_dim.ublock_rt * input_dim.ublock_ct;
        } else {
            // If it's not ublock bcast, fallback to using u_kt and output dims since we might not have input dims known
            block_tile_dim =
                (((input == 0) ? curr_op.output_dim.ublock_rt : curr_op.output_dim.ublock_ct) * curr_op.u_kt);
        }
    } else if (is_reduce(curr_op)) {
        block_tile_dim =
            (((curr_op.reduce_dim == Dim::C) ? curr_op.output_dim.ublock_rt : curr_op.output_dim.ublock_ct) *
             curr_op.u_kt) /
            bcast_factor;
    } else {
        block_tile_dim = (curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct) / bcast_factor;
    }
}

Dim Net2Hlks::get_tile_bcast_dim(int input, const tt_scheduled_op_info &curr_op) {
    Dim bcast_dim = Dim::Invalid;
    if (curr_op.input_tm_ops.find(input) != curr_op.input_tm_ops.end()) {
        for (auto tm : curr_op.input_tm_ops.at(input)) {
            string bcast_type = tm.first;
            if (bcast_type == "tile_broadcast") {
                bcast_dim = static_cast<Dim>(tm.second.at(0));
            }
        }
    }
    return bcast_dim;
}

bool Net2Hlks::is_input_forked(const string &name) {
    bool is_forked = false;
    if (forked_input_names.find(name) != forked_input_names.end()) {
        is_forked = true;
    }
    return is_forked;
}

int Net2Hlks::is_input_kernel_broadcasted(const string &name) {
    int is_bcasted = 0;
    if (kernel_broadcast_inputs.find(name) != kernel_broadcast_inputs.end()) {
        is_bcasted = kernel_broadcast_inputs[name];
    }
    return is_bcasted;
}

void Net2Hlks::insert_postcode(ofstream &hlks, int &postcode) {
    hlks << "postcode = 0x" << std::hex << postcode << ";" << endl;
    hlks << "hlk_debug_dump(core_ptr, (uint8_t*)&postcode, 4);" << endl;
    hlks << std::dec << endl;  // reset back to decimal
    postcode++;
}

void Net2Hlks::insert_hlks_header(ofstream &hlks, bool insert_binary_sfpu_header) {
    hlks << "#include <cstdint>" << endl;
    hlks << "#include \"hlks/inc/hlk_api.h\"" << endl;
    if (insert_binary_sfpu_header) {
        hlks << "#include \"hlks/inc/binary_sfpu_inner_loop.h\"" << endl;
    }
    hlks << "constexpr int MAX_NUM_INPUTS = 16;" << endl;
    hlks << "struct hlk_args_t {" << endl;
    hlks << "std::int32_t block_tile_dim;" << endl;
    hlks << "std::int32_t block_cnt;" << endl;
    hlks << "std::int32_t batch_cnt;" << endl;
    hlks << "std::int32_t num_m_sub_blocks;" << endl;
    hlks << "std::int32_t num_n_sub_blocks;" << endl;
    hlks << "std::int32_t num_tiles_per_m_sub_block;" << endl;
    hlks << "std::int32_t num_tiles_per_n_sub_block;" << endl;
    hlks << "std::int32_t gradient_op;" << endl;
    hlks << "std::int32_t transpose;" << endl;
    hlks << "};" << endl;

    hlks << "#include \"hlks/inc/wait_pop_tiles_utils.h\"" << endl;

    hlks << "TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {" << endl;
}

void Net2Hlks::insert_pack_relu_config(ofstream &hlks, const tt_scheduled_op_info &curr_op) {
    uint32_t relu_config = 0;

    if (curr_op.relu_en) {
        std::uint32_t relu_threshold;
        if (netlist_utils::is_int_format(curr_op.output_data_format)) {
            relu_threshold = static_cast<uint32_t>(curr_op.relu_threshold);
        } else {
            relu_threshold = netlist_utils::is_exp_a_format(curr_op.output_data_format)
                                 ? netlist_utils::conv_float_to_u16a(curr_op.relu_threshold)
                                 : netlist_utils::conv_float_to_u16b(curr_op.relu_threshold);
        }

        relu_config = (uint32_t)(static_cast<uint32_t>(curr_op.relu_en) | static_cast<uint32_t>(curr_op.relu_mode) |
                                 (relu_threshold << 16));
        hlks << "hlk_relu_config(core_ptr, 0x" << std::hex << relu_config << ");" << endl;
        hlks << std::dec;  // reset back to decimal as std::hex is sticky
    }
}

void Net2Hlks::disable_pack_relu_config(ofstream &hlks, const tt_scheduled_op_info &curr_op) {
    if (curr_op.relu_en) {
        // Disable relu if it was enabled
        hlks << "hlk_relu_config(core_ptr, 0);" << endl;
    }
}

void Net2Hlks::insert_sfpu_relu(
    ofstream &hlks, const tt_scheduled_op_info &curr_op, const std::map<std::string, std::string> &in) {
    if (curr_op.relu_mode != ReluMode::Min && curr_op.relu_mode != ReluMode::Max) {
        ERROR("Unsupported or invalid relu mode set for dest!");
    }

    std::string relu_dim_str = curr_op.relu_mode == ReluMode::Max ? "Max" : "Min";
    std::uint32_t relu_threshold = netlist_utils::conv_float_to_u16a(curr_op.relu_threshold);

    int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;

    hlks << "hlk_unary_sfpu_init<SfpuOp::Relu" << relu_dim_str
         << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0)) << ");" << endl;
    hlks << "for(int t = 0; t < " << out_block_tile_dim << " ; ++t) {" << endl;
    hlks << "hlk_unary_sfpu_op<SfpuOp::Relu" << relu_dim_str
         << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0)) << ", t, (int)Dim::RC, 0x" << std::hex
         << relu_threshold << ");" << endl;
    hlks << "}" << endl;
    hlks << std::dec;  // reset back to decimal as std::hex is sticky
}

void Net2Hlks::insert_pack_reduce_mask_config(ofstream &hlks, const tt_scheduled_op_info &curr_op) {
    string reduce_dim = curr_op.reduce_dim == Dim::R ? "Dim::R" : "Dim::C";
    hlks << "hlk_reduce_mask_config<" << reduce_dim << ">(core_ptr);" << endl;
}

void Net2Hlks::insert_pack_reduce_mask_clear(ofstream &hlks) { hlks << "hlk_reduce_mask_clear(core_ptr);" << endl; }

static std::string insert_packer_reconfig(
    ofstream &hlks, bool output_and_intermed_format_match, std::string last_input_packer, string out) {
    if (!output_and_intermed_format_match) {
        if (!last_input_packer.empty()) {
            hlks << "hlk_reconfig_packer_df(core_ptr, HlkOperand::" << last_input_packer << ", HlkOperand::" << out
                 << ");" << endl;
        } else {
            hlks << "hlk_reconfig_packer_df(core_ptr, HlkOperand::" << out << ");" << endl;
        }
    }

    return out;
}

void Net2Hlks::insert_pack_output(
    ofstream &hlks, const tt_scheduled_op_info &curr_op, bool output_and_intermed_format_match) {
    string out = curr_op.output == "output" ? "out0" : curr_op.output;
    int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;
    hlks << "hlk_wait_for_free_tiles(core_ptr, HlkOperand::" << out << ", " << out_block_tile_dim << ");" << endl;
    last_input_packer = insert_packer_reconfig(hlks, output_and_intermed_format_match, last_input_packer, out);
    hlks << "for(int t = 0; t < " << out_block_tile_dim << " ; ++t) {" << endl;
    hlks << "hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::" << out << ");" << endl;
    hlks << "}" << endl;
    hlks << "hlk_push_tiles(core_ptr, HlkOperand::" << out << ", " << out_block_tile_dim << ");" << endl;
}

void Net2Hlks::insert_wait_tiles(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    int input,
    const tt_scheduled_op_info &curr_op,
    string &bcast_dim,
    bool &mblock_bcast,
    int &block_tile_dim,
    int m_k) {
    bool is_reduce_matmul = is_matmul(curr_op) && ((curr_op.m_k > 1) || (curr_op.u_kt > 1));
    bool is_bcast_matmul = is_matmul(curr_op) && (!is_reduce_matmul);

    string block_scaler = "";
    // If input is forked we need to increment wait tile count as oid increments and input is used in the schedule with
    // matmul && m_k>1
    if (is_input_forked(curr_op.input_names.at(input)) && forked_input_names[curr_op.input_names.at(input)] > 1) {
        if (m_k > 1) {
            block_scaler = "(oid+1)*";
        }
    }

    if (mblock_bcast) {
        if (bcast_dim == "r_broadcast") {
            if (m_k > 1) {
                ERROR("Matmul reduce with mblock broadcast r is not supported!");
            }
            hlks << "if (m_block == 0) {" << endl;
            hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", (n_block+1)*"
                 << block_tile_dim << ");" << endl;
            hlks << "}" << endl;
        } else if (bcast_dim == "c_broadcast") {
            if (m_k > 1) {
                hlks << "if (oid == 0) {" << endl;
            } else {
                hlks << "if (n_block == 0) {" << endl;
            }
            hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", "
                 << block_scaler << block_tile_dim << ");" << endl;
            hlks << "}" << endl;
        } else {
            ERROR("Invalid or unsupported broadcast dim!");
        }
    } else {
        if ((is_reduce_matmul || is_bcast_matmul)) {
            if (input == 1) {
                int column_num_tiles = curr_op.m_k * curr_op.u_kt * curr_op.output_dim.ublock_ct;
                block_scaler =
                    curr_op.output_dim.mblock_n > 1 ? "n_block * " + to_string(column_num_tiles) + " + " : block_scaler;
                if (curr_op.m_k > 1) {
                    block_scaler += "(oid+1)*";
                }
                hlks << "if (m_block == 0) {" << endl;
                hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", "
                     << block_scaler << block_tile_dim << ");" << endl;
                hlks << "}" << endl;
            } else {  // input 0
                if (curr_op.output_dim.mblock_n > 1) {
                    block_scaler = m_k > 1 ? "(oid+1)*" : block_scaler;
                    hlks << "if (n_block == 0) {" << endl;
                    hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", "
                         << block_scaler << block_tile_dim << ");" << endl;
                    hlks << "}" << endl;
                } else {
                    hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", "
                         << block_scaler << block_tile_dim << ");" << endl;
                }
            }
        } else {
            hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input)) << ", "
                 << block_scaler << block_tile_dim << ");" << endl;
        }
    }
}

void Net2Hlks::insert_wait_inputs(
    ofstream &hlks, const std::map<std::string, std::string> &in, const tt_scheduled_op_info &curr_op, int m_k) {
    for (int input = 0; input < curr_op.input_names.size(); input++) {
        string bcast_dim = " ";
        int block_tile_dim = 0;
        bool mblock_bcast = false;
        int bcast_factor = 1;
        if (input > 0) {
            if (curr_op.input_names.at(input) == curr_op.input_names.at(input - 1)) {
                break;
            }
        }
        if (is_input_dest_reg(curr_op, input)) {
            continue;
        }

        string in_name = curr_op.input_names.at(input);
        if (is_input_forked(in_name)) {
            // Input is forked. We need to insert only one wait and skip others
            if (std::find(forked_input_skip_wait.begin(), forked_input_skip_wait.end(), in_name) !=
                forked_input_skip_wait.end()) {
                // We need to skip wait
                continue;
            } else {
                forked_input_skip_wait.push_back(in_name);
            }
        }

        if (is_input_kernel_broadcasted(in_name)) {
            continue;  // don't insert wait if input is single tile broadcasted
        }

        get_tms_params(input, curr_op, bcast_dim, mblock_bcast, block_tile_dim, bcast_factor);
        insert_wait_tiles(hlks, in, input, curr_op, bcast_dim, mblock_bcast, block_tile_dim, m_k);
    }
}

void Net2Hlks::insert_pop_tiles(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    int input,
    const tt_scheduled_op_info &curr_op,
    string &bcast_dim,
    bool &mblock_bcast,
    int &block_tile_dim,
    int m_k) {
    bool is_reduce_matmul = is_matmul(curr_op) && ((curr_op.m_k > 1) || (curr_op.u_kt > 1));
    bool is_bcast_matmul = is_matmul(curr_op) && (!is_reduce_matmul);
    string in_name = curr_op.input_names.at(input);

    if (should_pop_input(curr_op, input) || should_pop_last_input(curr_op, input) ||
        (!is_intermed(in_name) && !is_input_forked(in_name))) {
        if (mblock_bcast) {
            if (bcast_dim == "r_broadcast") {
                if (m_k > 1) {
                    ERROR("Matmul reduce with mblock broadcast r is not supported!");
                }
                hlks << "if ((m_block == " << curr_op.output_dim.mblock_m - 1 << ") && " << endl;
                hlks << "(n_block == " << curr_op.output_dim.mblock_n - 1 << ")) {" << endl;
                hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", (n_block+1)*" << block_tile_dim
                     << ");" << endl;
                hlks << "}" << endl;
            } else if (bcast_dim == "c_broadcast") {
                if (m_k > 1) {
                    hlks << "if (oid == " << m_k - 1 << ") {" << endl;
                } else {
                    hlks << "if (n_block == " << curr_op.output_dim.mblock_n - 1 << ") {" << endl;
                }
                hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", " << block_tile_dim << ");"
                     << endl;
                hlks << "}" << endl;
            } else {
                ERROR("Invalid or unsupported broadcast dim!");
            }
        } else {
            if (input == 0) {
                if (is_bcast_matmul) {
                    hlks << "if (n_block == " << curr_op.output_dim.mblock_n - 1 << ") {" << endl;
                    hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", " << block_tile_dim << ");"
                         << endl;
                    hlks << "}" << endl;
                } else if (is_reduce_matmul && curr_op.output_dim.mblock_n > 1) {
                    string curr_oid =
                        (curr_op.m_k > 1) ? (" && (oid == " + to_string(curr_op.m_k - 1) + "))") : ((string) ")");
                    hlks << "if ((n_block == " << curr_op.output_dim.mblock_n - 1 << ")" << curr_oid << " {" << endl;
                    hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", "
                         << curr_op.m_k * block_tile_dim << ");" << endl;
                    hlks << "}" << endl;
                } else {
                    hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", " << block_tile_dim << ");"
                         << endl;
                }
            } else {
                if (is_reduce_matmul || is_bcast_matmul) {
                    string curr_oid =
                        (curr_op.m_k > 1) ? (" && (oid == " + to_string(curr_op.m_k - 1) + "))") : ((string) ")");
                    hlks << "if ((m_block == " << curr_op.output_dim.mblock_m - 1 << ") && (n_block == " <<  curr_op.output_dim.mblock_n - 1 <<")" << curr_oid << " {" << endl;
                    hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", "
                         << curr_op.m_k * block_tile_dim * curr_op.output_dim.mblock_n << ");" << endl;
                    hlks << "}" << endl;
                } else {
                    hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << in.at(in_name) << ", " << block_tile_dim << ");"
                         << endl;
                }
            }
        }
    }
}

void Net2Hlks::insert_pop_inputs(
    ofstream &hlks, const std::map<std::string, std::string> &in, const tt_scheduled_op_info &curr_op, int m_k) {
    for (int input = 0; input < curr_op.input_names.size(); input++) {
        string bcast_dim = " ";
        int block_tile_dim = 0;
        bool mblock_bcast = false;
        int bcast_factor = 1;
        if (input > 0) {
            if (curr_op.input_names.at(input) == curr_op.input_names.at(input - 1)) {
                break;
            }
        }
        if (is_input_dest_reg(curr_op, input)) {
            continue;
        }

        string in_name = curr_op.input_names.at(input);
        if (is_input_kernel_broadcasted(in_name)) {
            continue;  // don't insert pop if input is single tile broadcasted inputs
        }
        get_tms_params(input, curr_op, bcast_dim, mblock_bcast, block_tile_dim, bcast_factor);
        insert_pop_tiles(hlks, in, input, curr_op, bcast_dim, mblock_bcast, block_tile_dim, m_k);
    }
}

static void insert_unpacker_reconfig(
    ofstream &hlks,
    const string &a_config,
    const string &a_prev_config,
    const string &b_config = "",
    const string &b_prev_config = "") {
    bool reconfig_a = !a_config.empty() && a_config != a_prev_config;
    bool reconfig_b = !b_config.empty() && b_config != b_prev_config;

    if (!reconfig_a && !reconfig_b)
        return;

    hlks << "hlk_reconfig_unpacker_df";
    if (reconfig_a && reconfig_b) {
        bool force_reconfig_ab = a_prev_config.empty() || b_prev_config.empty();
        if (force_reconfig_ab) {
            hlks << "(core_ptr, HlkOperand::" << a_config << ", HlkOperand::" << b_config;
        } else {
            hlks << "(core_ptr, HlkOperand::" << a_prev_config << ", HlkOperand::" << a_config
                 << ", HlkOperand::" << b_prev_config << ", HlkOperand::" << b_config;
        }
    } else if (reconfig_a) {
        if (a_prev_config.empty()) {
            hlks << "_srca(core_ptr, HlkOperand::" << a_config;
        } else {
            hlks << "_srca(core_ptr, HlkOperand::" << a_prev_config << ", HlkOperand::" << a_config;
        }
    } else if (reconfig_b) {
        if (b_prev_config.empty()) {
            hlks << "_srcb(core_ptr, HlkOperand::" << b_config;
        } else {
            hlks << "_srcb(core_ptr, HlkOperand::" << b_prev_config << ", HlkOperand::" << b_config;
        }
    }
    hlks << ");" << endl;
}

void Net2Hlks::insert_unpacker_init(
    ofstream &hlks, const map<string, string> &in, const tt_scheduled_op_info &curr_op, bool reset_state) {
    log_assert(
        curr_op.input_names.size() >= 1 && curr_op.input_names.size() <= 2,
        "In fused ops we expect always one or two input params!");

    const bool has_two_operands = curr_op.input_names.size() == 2;

    if (!has_two_operands) {
        // For unary ops check that we don't have tile_broadcast since it's not supported
        for (auto &&tms : curr_op.input_tm_ops) {
            for (auto &&tm : tms.second) {
                if (tm.first == "tile_broadcast") {
                    log_error(
                        "For unary ops tile broadcast is not supported as in that case input goes to srcB and unpaker "
                        "for srcB needs to be reconfigured insteaad of srcA");
                }
            }
        }
    }

    // We need to force reconfigure state of both unpackers
    // as we can't determin the previous state during code generation.
    if (reset_state) {
        last_input_unpackerA = last_input_unpackerB = "";
    }

    string cur_op_in0 = is_input_dest_reg(curr_op, 0) ? "" : in.at(curr_op.input_names[0]);
    string cur_op_in1 = "";
    if (has_two_operands && !is_input_dest_reg(curr_op, 1)) {
        cur_op_in1 = in.at(curr_op.input_names[1]);
    }

    if (is_matmul(curr_op)) {
        // For matmul input0 goes to srcB and input1 goes to srcA
        std::swap(cur_op_in0, cur_op_in1);
    }

    insert_unpacker_reconfig(hlks, cur_op_in0, last_input_unpackerA, cur_op_in1, last_input_unpackerB);

    if (!cur_op_in0.empty()) {
        last_input_unpackerA = cur_op_in0;
    }

    if (!cur_op_in1.empty()) {
        last_input_unpackerB = cur_op_in1;
    }
}

void Net2Hlks::insert_init(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    const tt_scheduled_op_info &curr_op,
    const tt_scheduled_op_info &prev_op_in_schedule,
    bool inputs_and_intermed_formats_match,
    bool reset_state,
    bool is_quantization_op) {
    if (!inputs_and_intermed_formats_match) {
        insert_unpacker_init(hlks, in, curr_op, reset_state);
    }

    if (curr_op.cast_dest_fp32_to_fp16_a) {
        // insert sfpu op to convert dest from Float32 to float16a

        hlks << "hlk_unary_sfpu_init<SfpuOp::CastFp32ToFp16a>(core_ptr, HlkOperand::"
             << in.at(curr_op.input_names.at(0)) << ");" << endl;

        int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;
        hlks << "for(int t = 0; t < " << out_block_tile_dim << "; ++t) {" << endl;
        hlks << "hlk_unary_sfpu_op<SfpuOp::CastFp32ToFp16a>(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
             << ", t, (int)Dim::RC);" << endl;
        hlks << "}" << endl;
    }

    if (netlist_utils::is_valid_binary_op(curr_op.type)) {
        BinaryOp binary_op = netlist_utils::get_binary_op(curr_op.type);

        Dim curr_op_tile_bcast_dim = get_tile_bcast_dim(1, curr_op);
        Dim prev_op_tile_bcast_dim = get_tile_bcast_dim(1, prev_op_in_schedule);

        if (reset_state || curr_op.type != prev_op_in_schedule.type ||
            curr_op_tile_bcast_dim != prev_op_tile_bcast_dim ||
            is_input_dest_reg(curr_op, 0) != is_input_dest_reg(prev_op_in_schedule, 0) ||
            is_input_dest_reg(curr_op, 1) != is_input_dest_reg(prev_op_in_schedule, 1) ||
            curr_op.zero_point != prev_op_in_schedule.zero_point) {
            if (!is_quantization_op) {
                bool dest_reuse = is_output_dest_reg(prev_op_in_schedule);
                string dest_string = dest_reuse ? ", BinaryOpDstReuse::DstToSrcA" : "";

                if ((curr_op.input_names.size() == 2) && is_input_dest_reg(curr_op, 1) &&
                    is_output_dest_reg(prev_op_in_schedule)) {
                    dest_string = ", BinaryOpDstReuse::DstToSrcB";
                }
                string tile_bcast = curr_op_tile_bcast_dim != Dim::Invalid ? "bcast_" : "";
                string tile_bcast_dim =
                    curr_op_tile_bcast_dim != Dim::Invalid ? (curr_op_tile_bcast_dim == Dim::R ? "R" : "C") : "None";

                string input0 = !dest_reuse ? (",HlkOperand::" + in.at(curr_op.input_names.at(0)) + ",") : "";
                string input1 = !dest_reuse ? ("HlkOperand::" + in.at(curr_op.input_names.at(1))) : "";
                string transpose_and_acc_to_dest = !dest_reuse ? ", 0, 0" : "";
                string dst_fun_string = !dest_reuse ? "" : "_reuse_dest";

                hlks << "hlk_binary_op" << dst_fun_string
                     << "_init_short<BinaryOp::" << netlist_utils::binary_op_to_string(binary_op) << dest_string
                     << ", Dim::" << tile_bcast_dim << ">(core_ptr" << input0 << input1 << transpose_and_acc_to_dest
                     << ");" << endl;
            } else {
                float zero_point = curr_op.zero_point;
                string hlk_init;
                if (binary_op == BinaryOp::Quantization) {
                    hlk_init = "hlk_sfpu_quant_int32_init";
                } else if (binary_op == BinaryOp::Dequantization) {
                    hlk_init = "hlk_sfpu_dequant_int32_init";
                    zero_point = -zero_point;
                } else if (binary_op == BinaryOp::Requantization) {
                    hlk_init = "hlk_sfpu_requant_int32_init";
                } else if (binary_op == BinaryOp::Add) {
                    hlk_init = "hlk_sfpu_add_int32_init(core_ptr);";
                } else if (binary_op == BinaryOp::Maximum) {
                    hlk_init = "hlk_unary_sfpu_init<SfpuOp::Max>(core_ptr, HlkOperand::out0);";
                } else {
                    log_fatal("Unknown binary op type");
                }

                if (netlist_utils::is_valid_binary_quantisation_op(curr_op.type)) {
                    union {
                        float f;
                        int32_t i;
                    } f2i = {.f = zero_point};
                    hlks << hlk_init << "(core_ptr, " << f2i.i << ");" << endl;
                } else {
                    hlks << hlk_init;
                }

                if (is_input_dest_reg(curr_op)) {
                    hlks << "hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::"
                            << in.at(curr_op.input_names.at(0)) << ", false, false);" << endl;
                }
            }
        }
    } else if (
        netlist_utils::is_valid_sfpu_op(curr_op.type) ||
        netlist_utils::get_unary_op(curr_op.type) == UnaryOp::Datacopy) {
        if (reset_state ||
            !(netlist_utils::is_valid_sfpu_op(prev_op_in_schedule.type) ||
              netlist_utils::get_unary_op(prev_op_in_schedule.type) == UnaryOp::Datacopy) ||
            is_input_dest_reg(curr_op) != is_input_dest_reg(prev_op_in_schedule)) {
            if (!is_input_dest_reg(curr_op)) {
                hlks << "hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
                     << ", false, false);" << endl;
            }
        }
        if (curr_op.type != prev_op_in_schedule.type && curr_op.type != "nop" && curr_op.type != "datacopy") {
            string sfpu_init_args = "";
            SfpuOp sfpu_op = netlist_utils::get_sfpu_op(curr_op.type);
            if (SfpuOp::Dropout == sfpu_op) {
                sfpu_init_args = ", " + std::to_string(curr_op.seed);
            }

            hlks << "hlk_unary_sfpu_init<SfpuOp::" << netlist_utils::sfpu_op_to_string(sfpu_op)
                 << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0)) << sfpu_init_args << ");" << endl;
        }
    } else if (is_matmul(curr_op)) {
        hlks << "hlk_mm_tile_init_short(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
             << ", HlkOperand::" << in.at(curr_op.input_names.at(1)) << ", false);" << endl;
    } else if (is_reduce(curr_op)) {
        string reduce_type = curr_op.reduce_type == ReduceFunc::Max ? "ReduceFunc::Max" : "ReduceFunc::Invalid";
        string reduce_dim = curr_op.reduce_dim == Dim::R ? "Dim::R" : "Dim::C";

        hlks << "hlk_reduce_tile_init_short<" << reduce_type << ", " << reduce_dim << ">(core_ptr, false);" << endl;
    } else {
        ERROR("Not supported op init!!!");
    }
}

void Net2Hlks::insert_hw_config(
    ofstream &hlks,
    const tt_scheduled_op_info &curr_op,
    map<string, string> &in,
    bool insert_hlk_dbg_feature_disable,
    bool int8_fused_op) {
    // To make things independant from what is the first scheduled op in fused_op hlk_hw_config_single_operand is issued
    // as hw config. In hlk for fused op we will reconfigure unpack, op, packer for each scheduled op anyway.
    hlks << "hlk_hw_config_single_operand(core_ptr, HlkOperand::" << in[curr_op.input_names.at(0)] << ", false);"
         << endl;

    if (int8_fused_op) {
        hlks << "hlk_hw_config_int8_fpu_math(core_ptr);" << endl;
    }

    if (insert_hlk_dbg_feature_disable) {
        hlks << "// Insert workaround for bbe bug #1372" << endl;
        hlks << "hlk_dbg_feature_disable(core_ptr);" << endl;
    }
}

void Net2Hlks::insert_matmul(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    const tt_scheduled_op_info &curr_op,
    int id,
    bool matmul_loop_start,
    bool inputs_and_intermed_formats_match) {
    int out_block_tile_dim = curr_op.output_dim.ublock_ct * curr_op.output_dim.ublock_rt;
    if (matmul_loop_start) {
        hlks << "if (enable_reload[" << id << "]) {" << endl;

        hlks << "hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::" << curr_op.output << ", false, false);"
             << endl;

        if (!inputs_and_intermed_formats_match) {
            insert_unpacker_reconfig(hlks, curr_op.output, last_input_unpackerA);
        }

        hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << curr_op.output << ", " << out_block_tile_dim << ");" << endl;
        hlks << "for (int i = 0; i < " << out_block_tile_dim << "; i++) {" << endl;
        hlks << "hlk_copy_tile_to_dst(core_ptr, HlkOperand::" << curr_op.output << ", i, i, false);" << endl;
        hlks << "}" << endl;
        hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << curr_op.output << ", " << out_block_tile_dim << ");" << endl;
        hlks << "hlk_mm_tile_init_short(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
             << ", HlkOperand:: " << in.at(curr_op.input_names.at(1)) << ", false);" << endl;

        if (!inputs_and_intermed_formats_match) {
            insert_unpacker_reconfig(hlks, last_input_unpackerA, curr_op.output);
        }

        hlks << "}" << endl;
    }

    hlks << "int dst_index_" << id << " = 0;" << endl;
    int inner_r = curr_op.output_dim.ublock_rt;
    int inner_c = curr_op.output_dim.ublock_ct;
    int inner_d = curr_op.u_kt;
    string in0_offset = curr_op.output_dim.mblock_n > 1 && curr_op.m_k > 1
                            ? ("oid*" + to_string(curr_op.output_dim.ublock_rt * curr_op.u_kt) + " + ")
                            : "";
    if (is_input_forked(curr_op.input_names.at(0)) && matmul_loop_start && !should_pop_input(curr_op, 0)) {
        in0_offset = "oid*" + to_string(curr_op.output_dim.ublock_rt * curr_op.u_kt) + " + ";
    }
    string in1_offset = curr_op.m_k > 1 ? "oid*" + to_string(curr_op.u_kt * inner_c) + " + " : "";
    hlks << "for (int in_r = 0; in_r < " << inner_r << "; in_r++) {" << endl;
    hlks << "for (int in_d = 0; in_d < " << inner_d << "; in_d++) {" << endl;

    // Extract input index for kernel broadcast if needed
    int input_index[2] = {get_external_input_index(curr_op, 0), get_external_input_index(curr_op, 1)};

    if (is_input_kernel_broadcasted(curr_op.input_names.at(0))) {
        hlks << "int in0_index = (batch*" << to_string(curr_op.m_k * inner_d * inner_r * curr_op.output_dim.mblock_m)
             << " + ";
        hlks << "m_block*" << to_string(curr_op.m_k * curr_op.output_dim.ublock_rt * curr_op.u_kt) << " + ";
        if (matmul_loop_start) {
            hlks << "oid*" << to_string(curr_op.output_dim.ublock_rt * curr_op.u_kt) << " + ";
        }
        hlks << "in_r*" << inner_d << " + in_d)";
        hlks << "% kernel_broadcast[" << input_index[0] << "];" << endl;
    } else if (is_matmul_bcast_ublock(curr_op)) {
        int in0_ublock_ct = this->get_input_dim(curr_op, 0).ublock_ct;
        hlks << "int in0_index = (" << in0_offset << "in_r*" << in0_ublock_ct << " + (in_d % " << in0_ublock_ct << "));"
             << endl;
    } else {
        hlks << "int in0_index = (" << in0_offset << "in_r*" << inner_d << " + in_d);" << endl;
    }

    if (!is_input_kernel_broadcasted(curr_op.input_names.at(1))) {
        int column_num_tiles = curr_op.m_k * curr_op.u_kt * curr_op.output_dim.ublock_ct;
        hlks << "int in1_index = n_block * " << column_num_tiles << " + (" << in1_offset << "in_d*" << inner_c << ");" << endl;
    }
    if (is_input_kernel_broadcasted(curr_op.input_names.at(1))) {
        hlks << "int in1_index = (batch*" << to_string(curr_op.m_k * inner_d * inner_c * curr_op.output_dim.mblock_n)
             << " + ";
        hlks << "n_block*" << to_string(curr_op.output_dim.ublock_ct * curr_op.u_kt) << " + ";
        if (matmul_loop_start) {
            hlks << "oid*" << to_string(curr_op.output_dim.mblock_n * curr_op.output_dim.ublock_ct * curr_op.u_kt)
                 << " + ";
        }
        hlks << "in_d*" << inner_c << ")";
        hlks << "% kernel_broadcast[" << input_index[1] << "];" << endl;
    }

    hlks << "hlk_mm_tile<kernel_broadcast["<<input_index[0]<<"], kernel_broadcast["<<input_index[1]<<"]>(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
         << " , HlkOperand::" << in.at(curr_op.input_names.at(1)) << ", in0_index, in1_index, dst_index_" << id << ""
         << ", false, "
         << inner_c << ");" << endl;
    hlks << "}" << endl;
    hlks << "dst_index_" << id << "+=" << inner_c << ";" << endl;
    hlks << "}" << endl;
    if (matmul_loop_start) {
        hlks << "enable_reload[" << id << "] = true;" << endl;
    }
}

void Net2Hlks::insert_binary_op(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    const tt_scheduled_op_info &curr_op,
    const tt_scheduled_op_info &prev_op_in_schedule,
    bool matmul_loop_start,
    bool is_binary_sfpu_op,
    bool is_32bit_dest) {
    BinaryOp binary_op = netlist_utils::get_binary_op(curr_op.type);
    Dim curr_op_tile_bcast_dim = Dim::Invalid;
    curr_op_tile_bcast_dim = get_tile_bcast_dim(1, curr_op);
    string tile_bcast = curr_op_tile_bcast_dim != Dim::Invalid ? "_bcast" : "";
    string bcast_template_arg = "None";
    if (curr_op_tile_bcast_dim != Dim::Invalid) {
        if (curr_op_tile_bcast_dim == Dim::R) {
            bcast_template_arg = "R";
        } else {
            bcast_template_arg = "C";
        }
    }

    int in_bcast_factor[2] = {1, 1};
    int in_block_tile_dim[2] = {0, 0};
    bool in_mblock_bcast[2] = {false, false};
    string in_bcast_dim[2] = {" ", " "};
    string in_index[2] = {"", ""};

    int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;
    int out_block_cnt = curr_op.output_dim.mblock_m * curr_op.output_dim.mblock_n;

    for (int input = 0; input < curr_op.input_names.size(); input++) {
        get_tms_params(
            input,
            curr_op,
            in_bcast_dim[input],
            in_mblock_bcast[input],
            in_block_tile_dim[input],
            in_bcast_factor[input]);
        in_index[input] = in_bcast_factor[input] == 1
                              ? "t"
                              : ((in_bcast_dim[input] == "c_broadcast")
                                     ? ((string) "t" + "/" + to_string(in_bcast_factor[input]))
                                     : (in_mblock_bcast[input]
                                            ? ((string) "n_block*" +
                                               to_string(out_block_tile_dim / in_bcast_factor[input]) + (string) " + ")
                                            : "") +
                                           "t%" + to_string(out_block_tile_dim / in_bcast_factor[input]));
        if (is_input_forked(curr_op.input_names.at(input)) && matmul_loop_start && !should_pop_input(curr_op, input)) {
            string block_scaler = "oid*" + to_string(out_block_tile_dim);
            in_index[input] = block_scaler + " + (" + in_index[input] + ")";
        }
        if (is_input_kernel_broadcasted(curr_op.input_names.at(input))) {
            int input_index = get_external_input_index(curr_op, input);
            in_index[input] = "(batch*" + to_string(out_block_tile_dim * out_block_cnt) + " + m_block*" +
                              to_string(curr_op.output_dim.mblock_n * out_block_tile_dim) + " + n_block*" +
                              to_string(out_block_tile_dim) + " + " + in_index[input] + ") % kernel_broadcast[" +
                              to_string(input_index) + "]";
        }
    }

    if (!is_binary_sfpu_op) {
        hlks << "for(int t = 0; t < " << out_block_tile_dim << "; ++t) {" << endl;
        if (is_output_dest_reg(prev_op_in_schedule)) {
            // clear fp32 dest by default unless we do cast to fp16_a. only valid for multiply op when dest acc is fp32
            // bug fix: tenstorrent/budabackend#1475
            string clear_fp32_dest = "";
            if (curr_op.type.find("multiply") != string::npos) {
                clear_fp32_dest = curr_op.cast_dest_fp32_to_fp16_a ? ", false" : ", true";
            }

            string dst_string = "DstToSrcA";
            int operand_index = 1;
            if ((curr_op.input_names.size() == 2) && (is_input_dest_reg(curr_op, 1))) {
                dst_string = "DstToSrcB";
                operand_index = 0;
            }

            hlks << "hlk_binary_op_reuse_dest<BinaryOp::" << netlist_utils::binary_op_to_string(binary_op)
                 << ", BinaryOpDstReuse::" << dst_string << ", Dim::" << bcast_template_arg << ">(core_ptr, "
                 << "HlkOperand::" << in.at(curr_op.input_names.at(operand_index)) << ", " << in_index[operand_index] << ", t"
                 << clear_fp32_dest << ");" << endl;
        } else {
            string transpose =
                tile_bcast == "" ? ", false" : ", true";  // add transpose flag if we are not broadcasting
            hlks << "hlk_binary_op<BinaryOp::" << netlist_utils::binary_op_to_string(binary_op)
                 << ", Dim::" << bcast_template_arg << ">(core_ptr, "
                 << "HlkOperand::" << in.at(curr_op.input_names.at(0))
                 << ", HlkOperand::" << in.at(curr_op.input_names.at(1)) << ", " << in_index[0] << ", " << in_index[1]
                 << ", t" << transpose << ");" << endl;
        }
        hlks << "}" << endl;
    } else {
        // Insert pack relu config, as binary sfpu ops can do both compute + pack in a single
        // loop iteration.
        insert_pack_relu_config(hlks, curr_op);

        bool input_from_dest = is_input_dest_reg(curr_op);
        if (!input_from_dest) {
            const int half_dest_size = is_32bit_dest ? 2 : 4;
            const int num_iter = out_block_tile_dim > half_dest_size ? 2 : 1;
            hlks << "binary_sfpu_inner_loop<BinaryOp::" << netlist_utils::binary_op_to_string(binary_op)
                 << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0))
                 << ", HlkOperand::" << in.at(curr_op.input_names.at(1))
                 << ", HlkOperand::" << ((curr_op.output == "output") ? "out0" : curr_op.output) << ", "
                 << out_block_tile_dim << ", m_block * args->num_m_sub_blocks + n_block"
                 << ", false"
                 << ", " << half_dest_size << ", " << num_iter << ","
                 << is_input_kernel_broadcasted(curr_op.input_names.at(0)) << ", "
                 << is_input_kernel_broadcasted(curr_op.input_names.at(1)) << ");" << std::endl;
            // binary_sfpu_inner_loop will reconfigure unpacker a internally.
            last_input_unpackerA = in.at(curr_op.input_names.at(1));
        } else {
            int input_index = is_input_dest_reg(curr_op, 0) ? 1 : 0;
            hlks << "binary_sfpu_inner_loop_from_dest<BinaryOp::" << netlist_utils::binary_op_to_string(binary_op)
                 << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(input_index))
                 << ", HlkOperand::" << ((curr_op.output == "output") ? "out0" : curr_op.output) << ", "
                 << out_block_tile_dim << ", m_block * args->num_m_sub_blocks + n_block, "
                 << is_input_kernel_broadcasted(curr_op.input_names.at(input_index)) << ");" << std::endl;
        }

        disable_pack_relu_config(hlks, curr_op);
    }
}

void Net2Hlks::insert_unary_op(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    const tt_scheduled_op_info &curr_op,
    const tt_scheduled_op_info &prev_op_in_schedule,
    bool matmul_loop_start) {
    int in_bcast_factor = 1;
    int in_block_tile_dim = 0;
    bool in_mblock_bcast = false;
    string in_bcast_dim = "";
    string in_index = "";

    int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;
    int out_block_cnt = curr_op.output_dim.mblock_m * curr_op.output_dim.mblock_n;

    get_tms_params(0, curr_op, in_bcast_dim, in_mblock_bcast, in_block_tile_dim, in_bcast_factor);
    in_index =
        in_bcast_factor == 1
            ? "t"
            : ((in_bcast_dim == "c_broadcast")
                   ? ((string) "t" + "/" + to_string(in_bcast_factor))
                   : (in_mblock_bcast
                          ? ((string) "n_block*" + to_string(out_block_tile_dim / in_bcast_factor) + (string) " + ")
                          : "") +
                         "t%" + to_string(out_block_tile_dim / in_bcast_factor));

    if (is_input_forked(curr_op.input_names.at(0)) && matmul_loop_start && !should_pop_input(curr_op, 0)) {
        string block_scaler = "oid*" + to_string(out_block_tile_dim);
        in_index = block_scaler + " + (" + in_index + ")";
    }

    if (is_input_kernel_broadcasted(curr_op.input_names.at(0))) {
        int input_index = get_external_input_index(curr_op, 0);
        in_index = "(batch*" + to_string(out_block_tile_dim * out_block_cnt) + " + m_block*" +
                   to_string(curr_op.output_dim.mblock_n * out_block_tile_dim) + " + n_block*" +
                   to_string(out_block_tile_dim) + " + " + in_index + ") % kernel_broadcast[" + to_string(input_index) +
                   "]";
    }

    hlks << "for(int t = 0; t < " << out_block_tile_dim << "; ++t) {" << endl;
    if (!is_output_dest_reg(prev_op_in_schedule)) {
        hlks << "hlk_copy_tile_to_dst(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0)) << ", " << in_index
             << ", t, false);" << endl;
    }
    if (!(curr_op.type == "nop" || curr_op.type == "datacopy")) {
        string vector_mode = curr_op.vector_mode == Dim::RC ? "RC" : curr_op.vector_mode == Dim::R ? "R" : "C";
        string sfpu_op_args = "";
        if (SfpuOp::LRelu == netlist_utils::get_sfpu_op(curr_op.type)) {
            std::int32_t slope_u16b = netlist_utils::conv_float_to_u16b(curr_op.slope);
            sfpu_op_args = ", " + std::to_string(slope_u16b);
        } else if (SfpuOp::Power == netlist_utils::get_sfpu_op(curr_op.type)) {
            sfpu_op_args = ", " + std::to_string(curr_op.exponent);
        } else if (SfpuOp::Dropout == netlist_utils::get_sfpu_op(curr_op.type)) {
            float tmp_p = curr_op.probability * (float)std::numeric_limits<std::uint16_t>::max();
            std::int32_t scale_u16b = netlist_utils::conv_float_to_u16b(curr_op.scale);
            sfpu_op_args = ", " + std::to_string((std::int32_t)tmp_p) + ", " + std::to_string(scale_u16b);
        }

        SfpuOp sfpu_op = netlist_utils::get_sfpu_op(curr_op.type);
        hlks << "hlk_unary_sfpu_op<SfpuOp::" << netlist_utils::sfpu_op_to_string(sfpu_op)
             << ">(core_ptr, HlkOperand::" << in.at(curr_op.input_names.at(0)) << ", t, (int)Dim::" << vector_mode
             << sfpu_op_args << ");" << endl;
    }
    hlks << "}" << endl;
}

void Net2Hlks::insert_reduce_op(
    ofstream &hlks,
    const std::map<std::string, std::string> &in,
    const tt_scheduled_op_info &curr_op,
    int id,
    bool reduce_loop_start,
    bool inputs_and_intermed_formats_match) {
    int in_bcast_factor = 1;
    int in_block_tile_dim = 0;
    bool in_mblock_bcast = false;
    string in_bcast_dim = "";
    string in_index = "";

    int out_block_tile_dim = curr_op.output_dim.ublock_rt * curr_op.output_dim.ublock_ct;

    get_tms_params(0, curr_op, in_bcast_dim, in_mblock_bcast, in_block_tile_dim, in_bcast_factor);
    in_index =
        in_bcast_factor == 1
            ? "t" + to_string(id)
            : ((in_bcast_dim == "c_broadcast")
                   ? ((string) "t" + to_string(id) + "/" + to_string(in_bcast_factor))
                   : (in_mblock_bcast
                          ? ((string) "n_block*" + to_string(out_block_tile_dim / in_bcast_factor) + (string) " + ")
                          : "") +
                         "t" + to_string(id) + "%" + to_string(out_block_tile_dim / in_bcast_factor));

    // Extract input index for kernel broadcast if needed
    int input_index = get_external_input_index(curr_op, 0);

    int num_tiles_per_m_sub_block = curr_op.reduce_dim == Dim::R ? curr_op.u_kt : curr_op.output_dim.ublock_rt;
    int num_tiles_per_n_sub_block = curr_op.reduce_dim == Dim::R ? curr_op.output_dim.ublock_ct : curr_op.u_kt;
    int num_m_sub_blocks = curr_op.reduce_dim == Dim::R ? curr_op.m_k : curr_op.output_dim.mblock_m;
    int num_n_sub_blocks = curr_op.reduce_dim == Dim::R ? curr_op.output_dim.mblock_n : curr_op.m_k;
    string reduce_type = curr_op.reduce_type == ReduceFunc::Max ? "ReduceFunc::Max" : "ReduceFunc::Invalid";
    string reduce_dim = curr_op.reduce_dim == Dim::R ? "Dim::R" : "Dim::C";
    string dst_index = curr_op.reduce_dim == Dim::R ? "in_c" : "in_r";

    string oid_index =
        reduce_loop_start ? "oid*" + to_string(num_tiles_per_m_sub_block * num_tiles_per_n_sub_block * num_n_sub_blocks)
                          : "0";

    if (is_input_kernel_broadcasted(curr_op.input_names.at(0))) {
        in_index =
            "(batch*" +
            to_string(num_tiles_per_m_sub_block * num_tiles_per_n_sub_block * num_m_sub_blocks * num_n_sub_blocks) +
            " + " + oid_index + " + " + in_index + ")% args->kernel_brodcast[" + to_string(input_index) + "]";
    }

    if (reduce_loop_start) {
        hlks << "if (enable_reload[" << id << "]) {" << endl;
        hlks << "hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::" << curr_op.output << ", false, false);"
             << endl;
        hlks << "hlk_wait_tiles(core_ptr, HlkOperand::" << curr_op.output << ", " << out_block_tile_dim << ");" << endl;
        if (!inputs_and_intermed_formats_match) {
            insert_unpacker_reconfig(hlks, curr_op.output, last_input_unpackerA);
        }
        hlks << "for (int i = 0; i < " << out_block_tile_dim << "; i++) {" << endl;
        hlks << "hlk_copy_tile_to_dst(core_ptr, HlkOperand::" << curr_op.output << ", i, i, false);" << endl;
        hlks << "}" << endl;
        hlks << "hlk_pop_tiles(core_ptr, HlkOperand::" << curr_op.output << ", " << out_block_tile_dim << ");" << endl;
        hlks << "hlk_reduce_tile_init_short<" << reduce_type << ", " << reduce_dim << ">(core_ptr, false);" << endl;
        if (!inputs_and_intermed_formats_match) {
            insert_unpacker_reconfig(hlks, last_input_unpackerA, curr_op.output);
        }
        hlks << "}" << endl;
    }

    string t = "t" + to_string(id);
    hlks << "int " << t << " = 0;" << endl;
    hlks << "for(int in_r = 0; in_r < " << num_tiles_per_m_sub_block << "; in_r++) { " << endl;
    hlks << "// reduce a row within a block" << endl;
    hlks << "for(int in_c = 0; in_c < " << num_tiles_per_n_sub_block << ";in_c++) { " << endl;
    hlks << "hlk_reduce_tile<" << reduce_type << ", " << reduce_dim << ">(core_ptr," << in.at(curr_op.input_names.at(0))
         << ", " << in_index << ", " << dst_index << ", 1.0f);" << endl;
    hlks << "" << t << "++; " << endl;
    hlks << "} " << endl;
    hlks << "} " << endl;

    if (reduce_loop_start) {
        hlks << "enable_reload[" << id << "] = true;" << endl;
    }

    // Even though reduce doesn't use input1, it will overrride settings for unpacker config for input1 and set it as
    // Float32 look under llk_unpack_reduce_init()
    if (inputs_and_intermed_formats_match) {
        // In case we have single input data format, reconfigure unpacker for input1 to default value.
        insert_unpacker_reconfig(hlks, "", "", "in0", "");
    } else {
        // Invalidate state of unpacker for input1 so that next op requiring unpacker for input1 will reconfigure it.
        last_input_unpackerB = "";
    }
}

// quantisation ops, maximum and binary add with int32 inputs are implemented as a binary SFPU ops
// so they need special threatment.
static bool is_binary_sfpu_op(const tt_fused_op_info &fused_op, const tt_scheduled_op_info &curr_op) {
    if (!netlist_utils::is_valid_binary_op(curr_op.type)) {
        return false;
    }
    BinaryOp op = netlist_utils::get_binary_op(curr_op.type);

    bool is_binary_spfu_op = netlist_utils::is_valid_binary_quantisation_op(curr_op.type) || op == BinaryOp::Maximum;
    if (op == BinaryOp::Add) {
        for (const auto &input_name : curr_op.input_names) {
            if (fused_op.get_input_data_format(input_name) == DataFormat::Int32) {
                is_binary_spfu_op = true;
            }
        }
    }
    return is_binary_spfu_op;
}

void Net2Hlks::insert_hlks_body(ofstream &hlks, const tt_fused_op_info &fused_op) {
    int matmul_or_reduce_id = 0;
    int postcode = 0xfaca0000;

    // Get list of all forked input names
    forked_input_names.clear();
    forked_input_skip_wait.clear();
    for (auto forked_input : fused_op.forked_input_names) {
        forked_input_names[forked_input.first] = forked_input.second;
    }

    kernel_broadcast_inputs = get_kernel_broadcast_inputs(fused_op);

    tt_scheduled_op_info first_op = fused_op.schedules.at(0).scheduled_ops.at(0);
    map<string, string> first_op_input_mapping = get_input_to_operand_map(first_op);

    if (debug) {
        init_debug(hlks);
    }

    // If any of the sheduled ops needs to do the dest cast
    // float32 -> float16a than we need to insert hlk_dbg_feature_disable
    // in hw init
    bool insert_hlk_dbg_feature_disable = false;
    for (const auto &schedule : fused_op.schedules) {
        for (const auto &curr_op : schedule.scheduled_ops) {
            if (curr_op.cast_dest_fp32_to_fp16_a) {
                insert_hlk_dbg_feature_disable = true;
                break;
            }
        }
    }

    bool is_int8_fused_op = fused_op.accumulation_data_format == DataFormat::Int32;
    // Fused ops doing int8 math needs to need to insert hlk_dbg_feature_disable
    // workaround for bug tenstorrent/budabackend#1948
    insert_hlk_dbg_feature_disable |= is_int8_fused_op;

    insert_hw_config(hlks, first_op, first_op_input_mapping, insert_hlk_dbg_feature_disable, is_int8_fused_op);

    if (fused_op.inputs_and_intermed_formats_match) {
        // check if we have matmul inside the fused op
        for (const auto &schedule : fused_op.schedules) {
            if (is_schedule_with_matmul(schedule)) {
                // In case we have matmul in fused op and all inputs data formats match
                // run dummy unpacker_reconfig to set proper value for "tile_size"
                // This is important for Float32 data format.
                insert_unpacker_reconfig(hlks, "in0", "", "in0", "");

                break;
            }
        }
    }

    hlks << "};" << endl;  // end hlk_setup_kernel
    hlks << "TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {" << endl;

    // Insert wait for single bcast tile inputs
    kernel_broadcast_wait_tiles(hlks, fused_op);
    begin_batch_loops(hlks);
    kernel_broadcast_wait_tiles_t(hlks, fused_op);
    begin_block_loops(hlks);

    tt_scheduled_op_info prev_op_in_schedule;
    for (const auto &schedule : fused_op.schedules) {
        bool has_loop = schedule_has_loop(schedule);
        int matmul_m_k = schedule_matmul_m_k(schedule);
        auto last_op_in_schedule = schedule.scheduled_ops.back();
        bool has_matmul_mblock_bcast =
            schedule_has_matmul_bcast(schedule) && is_matmul_bcast_mblock(last_op_in_schedule);
        bool has_matmul_with_intermed_input0_and_non_column_input1 = false;
        if (matmul_m_k > 0) {
            has_matmul_with_intermed_input0_and_non_column_input1 = is_matmul_with_intermed_input0_and_non_column_input1(schedule.scheduled_ops.back());
        }

        prev_op_in_schedule.type = "invalid";
        prev_op_in_schedule.output = "invalid";

        begin_schedule(hlks, schedule);

        for (const auto &curr_op : schedule.scheduled_ops) {
            bool is_matmul_op = is_matmul(curr_op);
            bool op_needs_oid_guard = (has_loop && has_matmul_mblock_bcast && !is_matmul_op);

            if (is_matmul_op && has_matmul_with_intermed_input0_and_non_column_input1) {
                hlks << "}" << endl; // end f (n_block == 0) { for matmul schedule with intermed input0 and non column input1
            }
            hlks << "// ----------------------------- " << endl;
            hlks << "// OP: " << curr_op.name << endl;
            hlks << "// ----------------------------- " << endl;

            std::map<std::string, std::string> in = get_input_to_operand_map(curr_op);

            if (is_macro_block_index_check_needed_for_op(fused_op, schedule, curr_op)) {
                begin_macro_block_index_check(hlks, curr_op);
            }

            if (op_needs_oid_guard) {
                hlks << "if (oid == 0) {" << endl;
            }

            // if curr_op is first op in the schedule or its dimensions don't match the previous op dimensions,
            // we can't count on the state before the curr_op when adding inits and packer/unpacker reconfigs
            bool reset_state = is_first_op_in_schedule(schedule, curr_op) ||
                               !output_mblock_dimensions_match(curr_op, prev_op_in_schedule);

            bool is_binary_op = netlist_utils::is_valid_binary_op(curr_op.type);
            bool is_binary_sfpu = is_binary_sfpu_op(fused_op, curr_op);

            if (is_binary_sfpu) {
                last_input_packer = insert_packer_reconfig(
                    hlks,
                    fused_op.output_and_intermed_format_match,
                    last_input_packer,
                    curr_op.output == "output" ? "out0" : curr_op.output);
            }

            insert_init(
                hlks,
                in,
                curr_op,
                prev_op_in_schedule,
                fused_op.inputs_and_intermed_formats_match,
                reset_state,
                is_binary_sfpu);
            if (debug) {
                insert_postcode(hlks, postcode);
            }

            insert_wait_inputs(hlks, in, curr_op, matmul_m_k);

            if (!is_output_dest_reg(prev_op_in_schedule) && !is_binary_sfpu) {
                hlks << "hlk_acquire_dst(core_ptr);" << endl;
            }

            if (is_binary_op) {
                bool is_32bit_dest = fused_op.accumulation_data_format == DataFormat::Int32 ||
                                     fused_op.accumulation_data_format == DataFormat::Float32;

                insert_binary_op(
                    hlks, in, curr_op, prev_op_in_schedule, schedule_has_matmul_loop(schedule), is_binary_sfpu, is_32bit_dest);
            } else if (
                netlist_utils::is_valid_sfpu_op(curr_op.type) ||
                (netlist_utils::get_unary_op(curr_op.type) == UnaryOp::Datacopy)) {
                insert_unary_op(hlks, in, curr_op, prev_op_in_schedule, schedule_has_matmul_loop(schedule));
            } else if (is_matmul(curr_op)) {
                insert_matmul(
                    hlks,
                    in,
                    curr_op,
                    matmul_or_reduce_id,
                    schedule_has_loop(schedule),
                    fused_op.inputs_and_intermed_formats_match);
                matmul_or_reduce_id++;
            } else if (is_reduce(curr_op)) {
                insert_reduce_op(
                    hlks,
                    in,
                    curr_op,
                    matmul_or_reduce_id,
                    schedule_has_loop(schedule),
                    fused_op.inputs_and_intermed_formats_match);
                matmul_or_reduce_id++;
            } else {
                ERROR("Not supported op!!!");
            }

            insert_pop_inputs(hlks, in, curr_op, matmul_m_k);

            if (reset_state) {
                last_input_packer = "";
            }

            if (!is_binary_sfpu) {
                if (!is_output_dest_reg(curr_op)) {
                    insert_pack_relu_config(hlks, curr_op);

                    if (is_reduce(curr_op)) {
                        // Configure masks to zero out rows/columns needed for reduce
                        if (schedule_has_loop(schedule)) {
                            hlks << "if (block == " << curr_op.m_k - 1 << ") {" << endl;
                        }
                        insert_pack_reduce_mask_config(hlks, curr_op);
                        if (schedule_has_loop(schedule)) {
                            hlks << "} //end if block" << endl;
                        }
                    }

                    insert_pack_output(hlks, curr_op, fused_op.output_and_intermed_format_match);
                    hlks << "hlk_release_dst(core_ptr);" << endl;

                    if (is_reduce(curr_op)) {
                        // Reconfigure masks to their default state after reduce
                        if (schedule_has_loop(schedule)) {
                            hlks << "if (block == " << curr_op.m_k - 1 << ") {" << endl;
                        }
                        insert_pack_reduce_mask_clear(hlks);
                        if (schedule_has_loop(schedule)) {
                            hlks << "} //end if block" << endl;
                        }
                    }

                    disable_pack_relu_config(hlks, curr_op);
                } else {
                    if (curr_op.relu_en) {
                        insert_sfpu_relu(hlks, curr_op, in);
                    }
                }
            }

            if (op_needs_oid_guard) {
                hlks << "} //end for oid guard" << endl;
            }

            if (is_macro_block_index_check_needed_for_op(fused_op, schedule, curr_op)) {
                end_macro_block_index_check(hlks, curr_op);
            }

            prev_op_in_schedule = curr_op;
        }

        end_schedule(hlks, schedule, matmul_or_reduce_id);
    }

    end_block_loops(hlks);
    kernel_broadcast_pop_tiles_t(hlks, fused_op);
    end_batch_loops(hlks);

    kernel_broadcast_pop_tiles(hlks, fused_op);
}

void Net2Hlks::insert_hlks_footer(ofstream &hlks) { hlks << "}" << endl; }

void Net2Hlks::output_hlks() {
    log_debug(tt::LogNet2Hlks, "Parsing fused ops..");

    for (auto fused_op_it : this->parsed_netlist.fused_ops_op_map) {
        string fused_op_id = fused_op_it.first;
        log_debug(tt::LogNet2Hlks, "Parsing fused op {}", fused_op_id);

        tt_fused_op_info fused_op_info = fused_op_it.second;
        string hlks_filename = this->output_dir + "/fused_op_" + fused_op_it.first + "_stream.cpp";

        if (!fused_op_info.custom_kernel_path.empty()) {
            std::filesystem::copy_file(fused_op_info.custom_kernel_path, hlks_filename);
            log_info(
                tt::LogNet2Hlks, "Skipping fused op {} kernel generation as it has custom kernel path", fused_op_id);
            continue;
        }

        this->fused_subops_info = netlist_utils::parse_fused_op_info(fused_op_info);

        log_debug(tt::LogNet2Hlks, "Generating fused op hlks {} ...", fused_op_it.first);
        ofstream hlks(hlks_filename);
        bool has_binary_sfpu_op = false;
        for (const auto &schedule : fused_op_it.second.schedules) {
            for (const auto &curr_op : schedule.scheduled_ops) {
                if (is_binary_sfpu_op(fused_op_info, curr_op)) {
                    has_binary_sfpu_op = true;
                    break;
                }
            }
            if (has_binary_sfpu_op) {
                break;
            }
        }

        insert_hlks_header(hlks, has_binary_sfpu_op);
        insert_hlks_body(hlks, fused_op_it.second);
        insert_hlks_footer(hlks);
    }
}