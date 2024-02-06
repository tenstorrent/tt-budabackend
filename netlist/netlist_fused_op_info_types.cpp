// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_fused_op_info_types.hpp"

#include "common/size_lib.hpp"
#include "hlks/inc/hlk_api.h"
#include "model/tensor.hpp"
#include "netlist_basic_info_types.hpp"
#include "netlist_utils.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"
#include <array>

using namespace std;

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

ostream& operator<<(ostream& os, const tt_scheduled_op_info& t) {
    os << "tt_scheduled_op_info{";
    os << " .name = " << t.name << ",";
    os << " .type = " << t.type << ",";
    os << " .input_names = " << t.input_names << ",";
    os << " .output = " << t.output << ",";
    os << " .dim_info = " << t.output_dim << ",";
    os << " .m_k = " << t.m_k << ",";
    os << " .u_kt = " << t.u_kt << ",";
    os << " .vector_mode = " << t.vector_mode << ",";
    os << " .reduce_dim = " << t.reduce_dim << ",";
    os << " .z = " << t.z << ",";
    os << " .reduce_type = " << netlist_utils::get_reduce_func_string(t.reduce_type) << ",";
    os << " .relu_en = " << t.relu_en << ",";
    os << " .relu_threshold = " << t.relu_threshold << ",";
    os << " .relu_mode = " << netlist_utils::get_relu_mode_string(t.relu_mode) << ",";
    os << " .output_dim = " << t.output_dim << ",";
    os << " .output_data_format = " << t.output_data_format << ",";
    os << " .input_tm_ops = {";
    for (const auto& input_tm_it : t.input_tm_ops) {
        os << " .input_" << input_tm_it.first << "_tms = [";
        for (const auto& tm : input_tm_it.second) {
            os << " .tm = " << get<0>(tm) << ", .args = " << get<1>(tm);
        }
        os << "]";
    }
    os << " .inputs_to_pop = {";
    for (const auto& input_to_pop : t.inputs_to_pop) {
        os << "" << input_to_pop << ",";
    }
    os << "}";
    os << " .inputs_to_pop_last = {";
    for (const auto& input_to_pop_last : t.inputs_to_pop_last) {
        os << "" << input_to_pop_last << ",";
    }
    os << "}";
    os << "}";
    os << "}";
    return os;
}

static bool is_input(string input_name) { return input_name.find("input") == 0; }
static bool is_dest(string input_name) { return input_name == "dest"; }
static bool is_intermed(string input_name) { return input_name.find("intermed") == 0; }
DataFormat tt_fused_op_info::get_input_data_format(string input_name) const {
    if (is_input(input_name)) {
        int input_id = atoi(&input_name[5]);
        return input_data_formats[input_id];
    } else if (is_dest(input_name)) {
        return accumulation_data_format;
    } else if (is_intermed(input_name)) {
        return intermed_data_format;
    } else {
        throw std::runtime_error("Invalid input name: " + input_name);
    }
}

void verify(const tt_scheduled_op_info& t, tt::ARCH arch, const tt_fused_op_info& fused_op_info, bool last_op_in_schedule) {
    log_assert(t.name != "", "Name must be set for op in tt_scheduled_op_info");
    log_assert(t.type != "", "Type must be set for op in tt_scheduled_op_info");
    log_assert(
        t.type != "fused_op",
        "Multiple levels of fused op nesting is not allowed. fused_op_id={}",
        t.name);

    log_assert(
        last_op_in_schedule ||
            (!netlist_utils::is_valid_matmul_op(t.type) && !netlist_utils::is_valid_reduce_op(t.type)),
        "Matmul/reduce op {} must be the last one in schedule.",
        t.name);

    // verify inputs
    log_assert(t.input_names.size() > 0, "Must have inputs for op in tt_scheduled_op_info");

    if (netlist_utils::is_valid_matmul_op(t.type)) {
        log_assert(
            is_input(t.input_names[1]) || (t.m_k == 1),
            "matmul op {} cannot have {} as input 1, only fused op input buffer can be on the input 1 if mblock has "
            "more than 1 ublocks",
            t.name,
            t.input_names[1]);
    }

    if (netlist_utils::is_valid_reduce_op(t.type) and fused_op_info.accumulation_data_format == DataFormat::Float32) {
        // tenstorrent/budabackend#1464
        for (const auto& input_data_format : fused_op_info.input_data_formats) {
            log_assert(
                !netlist_utils::is_exp_a_format(input_data_format),
                "Fused op {} with reduce and dest accumulation in F32 cannot have inputs of a-type (issue #1464)",
                fused_op_info.name);
        }
    }

    log_assert(t.inputs_to_pop.size() <= t.input_names.size(), "Can't pop more items than there are inputs");
    for (const auto& input_to_pop : t.inputs_to_pop) {
        bool found_input = false;
        for (const auto& input : t.input_names) {
            if (input_to_pop == input) {
                found_input = true;
            }
        }
        log_assert(
            found_input,
            "input_to_pop={} cannot be found in the input list for op={}",
            input_to_pop,
            t.name);

        log_assert(
            t.inputs_to_pop_last.find(input_to_pop) == t.inputs_to_pop_last.end(),
            "input_to_pop={} duplicated in the list for inputs_to_pop_last",
            input_to_pop);
    }
    for (const auto& input_to_pop_last : t.inputs_to_pop_last) {
        log_assert(
            t.inputs_to_pop.find(input_to_pop_last) == t.inputs_to_pop.end(),
            "input_to_pop_last={} duplicated in the list for inputs_to_pop",
            input_to_pop_last);
    }

    bool is_requant_dequant_op = t.type == "requantization" || t.type == "dequantization";
    bool is_quantization_op = t.type == "quantization";
    bool is_any_quant_op = is_requant_dequant_op || is_quantization_op;

    const bool is_int_fused_op = fused_op_info.accumulation_data_format == DataFormat::Int32;
    if (is_int_fused_op) {
        log_assert(netlist_utils::is_valid_binary_op(t.type) || netlist_utils::get_unary_op(t.type) == UnaryOp::Datacopy, "Only binary ops and datacopy are supported in int fused ops");
        if (!is_any_quant_op) {
            for (int input_index = 0; input_index < t.input_names.size(); input_index++) {
                if (is_input(t.input_names[input_index])) {
                    if (netlist_utils::get_unary_op(t.type) == UnaryOp::Datacopy ||
                        netlist_utils::get_binary_op(t.type) == BinaryOp::Add ||
                        netlist_utils::get_binary_op(t.type) == BinaryOp::Maximum) {
                        auto input_df = fused_op_info.get_input_data_format(t.input_names[input_index]);
                        log_assert(
                            input_df == DataFormat::Int8 || input_df == DataFormat::Int32,
                            "In int fused ops, add, maximum and datacopy sub op inputs have to be Int8 or Int32");
                    } else {
                        log_assert(
                            fused_op_info.get_input_data_format(t.input_names[input_index]) == DataFormat::Int8,
                            "In int fused ops, subtract and multiply sub op inputs have to be Int8");
                    }
                }
            }
        } else if (is_quantization_op) {
            log_assert(
                is_input(t.input_names[0]) && is_input(t.input_names[1]),
                "For quantization ops both operands have to be op inputs");
            log_assert(
                fused_op_info.get_input_data_format(t.input_names[0]) == DataFormat::Float32 &&
                fused_op_info.get_input_data_format(t.input_names[1]) == DataFormat::Float32,
                "Both inputs to quantization have to Float32");
        } else if (is_requant_dequant_op) {
            log_assert(!is_dest(t.input_names[0]),  "requantization/dequantization op {} can't have dest as input0", t.name);

            bool use_dest = is_dest(t.input_names[1]);
            if (use_dest) {
                log_assert(
                    t.output_dim.ublock_ct * t.output_dim.ublock_rt <= 2,
                    "requantization/dequantization op {} can support up to 2 tiles in micro block dimension if dest is used as input1",
                    t.name);

                log_assert(
                    is_input(t.input_names[0]) &&
                        fused_op_info.get_input_data_format(t.input_names[0]) == DataFormat::Float32,
                    "requantization/dequantization op require input0 to be input and to be Float32 if dest is used as input1");

            } else {
                auto df_in0 = fused_op_info.get_input_data_format(t.input_names[0]);
                auto df_in1 = fused_op_info.get_input_data_format(t.input_names[1]);
                log_assert(
                    df_in0 == DataFormat::Int8 || df_in0 == DataFormat::Int32,
                    "requantization/dequantization op {} require input0 to be Int8 or Int32",
                    t.name);
                log_assert(df_in1 == DataFormat::Float32, "requantization/dequantization op {} require input1 to be Float32", t.name);
            }
        }
    }

    if (is_any_quant_op) {
        log_assert(is_int_fused_op, "Quantization ops are only supported in int fused ops");
    }

    for (int index = 0; index < t.input_names.size(); index++) {
        string input = t.input_names[index];
        if (input == "dest") {
            log_assert(index != 1 || arch != tt::ARCH::GRAYSKULL, "On grayskull dest can be used only as input 1");
            log_assert(!netlist_utils::is_valid_matmul_op(t.type), "Dest is forbiden as input for matmul scheduled op");
            log_assert(!netlist_utils::is_valid_reduce_op(t.type), "Dest is forbiden as input for reduce scheduled op");
            log_assert(t.type != "maximum", "Dest is forbiden as input for maximum scheduled op");
            log_assert(
                !is_int_fused_op || is_requant_dequant_op,
                "In int fused ops only requant and dequant ops can have dest as input!");
        }
    }

    if (is_dest(t.output)) {
        log_assert(
            !is_binary_sfpu_op(fused_op_info, t),
            "Binary sfpu ops (quantization, requantization, dequantization, maximum and add with Int32 inputs) can't "
            "have "
            "dest as output");
    }

    // verify output
    log_assert(t.output != "", "Output must be set for op in tt_scheduled_op_info");

    if ((netlist_utils::is_valid_matmul_op(t.type) or netlist_utils::is_valid_reduce_op(t.type)) and t.m_k > 1) {
        log_assert(
            t.output.find("intermed") == 0,
            "matmul/reduce op {} cannot output to {} when m_k>1, it can only output to an intermediate buffer",
            t.name,
            t.output);
    }

    verify(t.output_dim);
}

ostream& operator<<(ostream& os, const tt_schedule_info& t) {
    os << "tt_schedule_info{";
    os << " .scheduled_ops = " << t.scheduled_ops << ",";
    os << "}";
    return os;
}
void verify(const tt_schedule_info& t, tt::ARCH arch, const tt_fused_op_info& fused_op_info) {
    log_assert(t.scheduled_ops.size() > 0, "Must have scheduled_ops in tt_schedule_info");
    for (int scheduled_op_index = 0; scheduled_op_index < t.scheduled_ops.size(); scheduled_op_index++) {
        bool last_op_in_schedule = scheduled_op_index == t.scheduled_ops.size() - 1;
        verify(t.scheduled_ops[scheduled_op_index], arch, fused_op_info, last_op_in_schedule);
    }
}

ostream& operator<<(ostream& os, const tt_fused_op_info& t) {
    os << "tt_fused_op_info{";
    os << " .name = " << t.name << ",";
    os << " .num_inputs = " << t.num_inputs << ",";
    os << " .num_intermediates = " << t.num_intermediates << ",";

    os << " .input_names_to_rel_names_map = {";
    for (const auto& input_it : t.input_names_to_rel_names_map) {
        os << input_it.first << " = " << input_it.second << ", ";
    }
    os << "}";
    os << " .forked_input_names = " << t.forked_input_names << ",";
    os << " .schedules = " << t.schedules << ",";
    os << "}";
    return os;
}

// function to verify the correctness of TM in the fused op
// to verify broadcast, we need to know the input dimension
//
static void verify_tm(const tt_fused_op_info& t) {
    // needed for broadcast verification
    std::unordered_map<std::string, tt_dim_info> input_dim_info;

    for (const auto& schedule_info : t.schedules) {
        for (const auto& scheduled_op_info : schedule_info.scheduled_ops) {
            for (const auto& [input, tm_ops] : scheduled_op_info.input_tm_ops) {
                const std::string& input_name = scheduled_op_info.input_names.at(input);

                bool has_r_broadcast = false;
                bool has_c_broadcast = false;
                for (const auto& [tm_name, params] : tm_ops) {
                    TmOp tm_op = netlist_utils::get_tm_op(tm_name);
                    if (tm_op == TmOp::rBroadcast) {
                        has_r_broadcast = true;
                    } else if (tm_op == TmOp::cBroadcast) {
                        has_c_broadcast = true;
                    } else if (tm_op == TmOp::zBroadcast) {
                        log_fatal("Fused op does not support internal z broadcast");
                    } else if (tm_op == TmOp::TileBroadcast) {
                        log_assert(
                            input == 1 && netlist_utils::is_valid_binary_op(scheduled_op_info.type) &&
                                !is_binary_sfpu_op(t, scheduled_op_info),
                            "TileBroadcast is only supported on input1 for fpu binary ops. Sub, mul and add with non "
                            "Int32 inputs.");
                    }
                }

                tt_dim_info input_dim;
                if (has_r_broadcast || has_c_broadcast) {
                    log_assert(
                        input_name.find("intermed") == 0,
                        "Broadcast inside the fused op is only supported on the intermediate buffers. {} has bad input "
                        "{}.",
                        scheduled_op_info.name,
                        input_name);

                    auto it = input_dim_info.find(input_name);
                    log_assert(it != input_dim_info.end(), "Input {} not found", input_name);
                    input_dim = it->second;

                    log_assert(
                        !is_binary_sfpu_op(t, scheduled_op_info),
                        "Broadcast inside the fused op is not supported on binary sfpu ops (quantization, "
                        "requantization, dequantization, maximum and add with Int32 inputs)");
                }

                if (has_r_broadcast) {
                    // ISSUES: 1064 and 1065 Broadcasted dimension needs to be 1 in both mblock and ublock
                    log_assert(
                        input_dim.mblock_m == 1 && input_dim.ublock_rt == 1, 
                        "Fused op supports r broadcast only when mblock_m and ublock_rt are both 1: [1, n] [1, c] -> "
                        "[m, n] [r, c]. {} has bad input {}",
                        scheduled_op_info.name,
                        input_name);

                    const auto& output_dim = scheduled_op_info.output_dim;
                    if (output_dim.mblock_m != 1) {
                        log_assert(
                            input_dim.mblock_n == 1,
                            "Fused op r broadcast requires mblock_n == 1 if the broadcast is increasing mblock_m "
                            "dimension. {} has bad input {}",
                            scheduled_op_info.name,
                            input_name);
                    }

                    log_assert(
                        !netlist_utils::is_valid_matmul_op(scheduled_op_info.type), 
                        "Fused op supports only c broadcast on input0 for matmul. {} is invalid.",
                        scheduled_op_info.name);
                }

                if (has_c_broadcast) {
                    if (netlist_utils::is_valid_matmul_op(scheduled_op_info.type)) {
                        log_assert(
                            (input_dim.mblock_n == 1 || (input_dim.mblock_n == scheduled_op_info.m_k)) && input == 0,
                            "Matmul in fused op supports only following scenarios and only on input0: [m, 1] [r, c] -> "
                            "[m, n] [r, k] and [m, n] [r, c] -> [m, n] [r, k]. {} has bad input {}",
                            scheduled_op_info.name,
                            input_name);
                    } else {
                        // ISSUES: 1064 and 1065 Broadcasted dimension needs to be 1 in both mblock and ublock
                        log_assert(
                            input_dim.mblock_n == 1 && input_dim.ublock_ct == 1, 
                            "Fused op supports c broadcast only when mblock_n and ublock_ct are both 1: [m, 1] [r, 1] "
                            "-> [m, n] [r, c]. {} has bad input {}",
                            scheduled_op_info.name,
                            input_name);
                    }
                }

                // BUG: 1135 Broadcast does not work with matmul
                // For each matmul input with TMs, check if TM is broadcast
                if (netlist_utils::is_valid_matmul_op(scheduled_op_info.type)) {
                    log_assert(
                        !has_r_broadcast, "Feature gap (fused_ops) - internal R broadcast not supported for matmul");
                }

                // BUG: 1134 Fused op internal rc broadcast does not work
                log_assert(
                    !(has_r_broadcast && has_c_broadcast),
                    "Feature gap (fused_ops) - internal rc broadcast not supported");
            }

            // keep track about last output dimension
            // it will override the previous one with same name which is ok
            input_dim_info[scheduled_op_info.output] = scheduled_op_info.output_dim;
        }
    }
}

// Verify correctness of intermed buffer usage.
static void verify_intermed_buffers(const tt_fused_op_info& t) {
    struct intermed_buffer_usage {
        intermed_buffer_usage() = default;

        // Buffer is active if some sheduled op had output to it
        // but it wasn't poped yet.
        bool is_active = false;
        // Index of the fused op shedule where producing op for this buffer was last found.
        int producing_schedule = -1;
        bool is_produced_by_matmul_or_reduce = false;
        // True if buffer was produce by sheduled op which has output macro block dim == [1, 1]
        bool is_mblock_1_1 = false;
    };

    // Temporary info for intermed buffer states
    array<intermed_buffer_usage, tt::constants::MAX_NUM_INTERMEDIATE_BUFFERS> intermed_buffers;

    auto is_intermed_buffer = [](string buf_name) { return buf_name.find("intermed") != string::npos; };

    auto intermed_buf_index = [](string buf_name) { return atoi(&buf_name.back()); };

    // Verify intermed buffer usage
    int schedule_index = 0;
    for (const auto& schedule_info : t.schedules) {
        for (const auto& scheduled_op_info : schedule_info.scheduled_ops) {
            for (const auto& input : scheduled_op_info.input_names) {
                if (is_intermed_buffer(input)) {
                    intermed_buffer_usage& buf = intermed_buffers[intermed_buf_index(input)];
                    log_assert(
                        buf.is_active,
                        "Invalid usage of buffer {} at scheduled op {} at {} op. Buffer has to be active before being "
                        "used!",
                        input,
                        scheduled_op_info.name,
                        t.name);

                    // It is ok to consume intermed buffer for current schedule or previous if it doesn't 
                    // end with matmul or reduce.
                    bool is_intermed_from_matching_shedule = buf.producing_schedule == schedule_index;
                    if (!is_intermed_from_matching_shedule) {
                        string last_op_in_schedule_type = t.schedules[buf.producing_schedule].scheduled_ops.back().type;
                        if (!netlist_utils::is_valid_matmul_op(last_op_in_schedule_type) &&
                            !netlist_utils::is_valid_reduce_op(last_op_in_schedule_type)) {
                            is_intermed_from_matching_shedule = true;
                        }
                    }

                    bool is_intermed_ok_to_use =
                        is_intermed_from_matching_shedule || buf.is_mblock_1_1 || buf.is_produced_by_matmul_or_reduce;
                    log_assert(
                        is_intermed_ok_to_use,
                        "Invalid usage of buffer {} at scheduled op {} at {} op. Intermed buffers which are not "
                        "produced wihtin the schedule can "
                        "only be used if they are produced by the matmul or reduce op or if they contain single macro "
                        "block!",
                        input,
                        scheduled_op_info.name,
                        t.name);
                }
            }

            set<string> inputs_to_pop = scheduled_op_info.inputs_to_pop;
            inputs_to_pop.insert(
                scheduled_op_info.inputs_to_pop_last.begin(), scheduled_op_info.inputs_to_pop_last.end());

            for (const auto& input_to_pop : inputs_to_pop) {
                if (is_intermed_buffer(input_to_pop)) {
                    intermed_buffer_usage& buf = intermed_buffers[intermed_buf_index(input_to_pop)];
                    log_assert(
                        buf.is_active,
                        "Invalid usage of buffer {} at scheduled op {} at {} op. Buffer has to produced before it's "
                        "being poped!",
                        input_to_pop,
                        scheduled_op_info.name,
                        t.name);
                    buf.is_active = false;
                }
            }

            if (is_intermed_buffer(scheduled_op_info.output)) {
                intermed_buffer_usage& buf = intermed_buffers[intermed_buf_index(scheduled_op_info.output)];
                log_assert(
                    !buf.is_active,
                    "Invalid usage of buffer {} at scheduled op {} at {} op. Buffer has to be poped before being "
                    "produced again!",
                    scheduled_op_info.output,
                    scheduled_op_info.name,
                    t.name);

                buf.is_active = true;
                buf.producing_schedule = schedule_index;

                buf.is_produced_by_matmul_or_reduce = netlist_utils::is_valid_matmul_op(scheduled_op_info.type) ||
                                                      netlist_utils::is_valid_reduce_op(scheduled_op_info.type);
                buf.is_mblock_1_1 =
                    scheduled_op_info.output_dim.mblock_m == 1 && scheduled_op_info.output_dim.mblock_n == 1;
            }
        }
        schedule_index++;
    }

    int intermed_index = 0;
    for (const auto& buf : intermed_buffers) {
        log_assert(
            !buf.is_active,
            "Intermed{} buffer produced at schedule index: {}, wasn't poped by the end of the {} op",
            intermed_index,
            buf.producing_schedule,
            t.name);
        intermed_index++;
    }
}
/**
 * Verify whether input and output dimensions match inside fused op.
 * NOTE: Checks not performed for external inputs as dimensions are unknown.
 */
void verify_op_dims(
    const tt_fused_op_info& t, const std::unordered_map<std::string, tt_fused_subop_info>& fused_subops_info) {
    for (const auto& schedule_info : t.schedules) {
        for (const auto& curr_op : schedule_info.scheduled_ops) {
            const tt_dim_info& output_dim = curr_op.output_dim;

            // Check if dimensions of given input are known
            auto is_dim_known = [&curr_op](int input_index) -> bool {
                // Return true if the input isn't an external op input
                return curr_op.input_names[input_index].find("input") != 0;
            };

            // Get info object for input at given index
            auto get_input_info = [&curr_op, &fused_subops_info](int input_index) -> const tt_fused_subop_input_info& {
                auto it = fused_subops_info.find(curr_op.name);
                log_assert(
                    it != fused_subops_info.end(),
                    "Op {} not found in fused op parsed info list",
                    curr_op.name);
                log_assert(
                    it->second.inputs.size() > input_index,
                    "Op {} does not have {} inputs",
                    curr_op.name,
                    input_index + 1);
                return it->second.inputs[input_index];
            };

            // Check whether input at given index matches dimensions to the output for binary and sfpu ops
            auto verify_input_output_dims_match_for_binary_and_unary_ops =
                [&curr_op, &fused_subops_info, &get_input_info, &output_dim, &is_dim_known](int input_index) -> void {
                auto input_info = get_input_info(input_index);
                auto input_dim = input_info.input_dim;
                if (is_dim_known(input_index)) {
                    log_assert(
                        input_dim.mblock_m * input_dim.ublock_rt * input_info.r_bcast_factor ==
                                output_dim.mblock_m * output_dim.ublock_rt &&
                            output_dim.mblock_m >= input_dim.mblock_m && output_dim.ublock_rt >= input_dim.ublock_rt &&
                            input_dim.mblock_n * input_dim.ublock_ct * input_info.c_bcast_factor ==
                                output_dim.mblock_n * output_dim.ublock_ct &&
                            output_dim.mblock_n >= input_dim.mblock_n && output_dim.ublock_ct >= input_dim.ublock_ct,
                        "Dimension mismatch in op {}",
                        curr_op.name);
                }
            };

            if (netlist_utils::is_valid_sfpu_op(curr_op.type) || netlist_utils::get_unary_op(curr_op.type) == UnaryOp::Datacopy) {
                verify_input_output_dims_match_for_binary_and_unary_ops(0);
            } else if (netlist_utils::is_valid_binary_op(curr_op.type)) {
                verify_input_output_dims_match_for_binary_and_unary_ops(0);
                verify_input_output_dims_match_for_binary_and_unary_ops(1);
            } else if (netlist_utils::is_valid_reduce_op(curr_op.type)) {
                auto input_dim = get_input_info(0).input_dim;

                if (curr_op.reduce_dim == Dim::R) {
                    if (is_dim_known(0)) {
                        log_assert(
                            input_dim.mblock_n == output_dim.mblock_n && input_dim.ublock_ct == output_dim.ublock_ct &&
                                input_dim.mblock_m == curr_op.m_k && input_dim.ublock_rt == curr_op.u_kt,
                            "Dimension mismatch in op {}",
                            curr_op.name);
                    }

                    log_assert(
                        output_dim.mblock_m == 1 && output_dim.ublock_rt == 1,
                        
                        "Dimension mismatch in op {}",
                        curr_op.name);
                } else if (curr_op.reduce_dim == Dim::C) {
                    if (is_dim_known(0)) {
                        log_assert(
                            input_dim.mblock_m == output_dim.mblock_m && input_dim.ublock_rt == output_dim.ublock_rt &&
                                input_dim.mblock_n == curr_op.m_k && input_dim.ublock_ct == curr_op.u_kt,
                            "Dimension mismatch in op {}",
                            curr_op.name);
                    }

                    log_assert(
                        output_dim.mblock_n == 1 && output_dim.ublock_ct == 1,
                        "Dimension mismatch in op {}",
                        curr_op.name);
                } else {
                    log_fatal("Op {} has invalid reduce dim {}", curr_op.name, curr_op.reduce_dim);
                }
            } else if (netlist_utils::is_valid_matmul_op(curr_op.type)) {
                auto input0_info = get_input_info(0);
                auto input0_dim = input0_info.input_dim;

                auto input1_dim = get_input_info(1).input_dim;

                if (is_dim_known(0)) {
                    if (input0_info.c_bcast_factor == 1) {
                        log_assert(
                            (input0_dim.mblock_n == curr_op.m_k) && (input0_dim.ublock_ct == curr_op.u_kt),
                            "If there is no broadcast input0 on matmul op, m_k needs to be input0.mblock_n and u_kt "
                            "needs to be input0.ublock_ct. input0.mblock_n is {}, m_k is {}, input0.ublock_ct is {}, "
                            "u_kt is {}",
                            input0_dim.mblock_n,
                            curr_op.m_k,
                            input0_dim.ublock_ct,
                            curr_op.u_kt);
                    } else {
                        // bcast on input 0
                        log_assert(
                            input0_dim.mblock_n * input0_dim.ublock_ct * input0_info.c_bcast_factor ==
                                curr_op.m_k * curr_op.u_kt,
                            "Dimension mismatch in op {} for input0",
                            curr_op.name);
                    }
                    log_assert(
                        input0_dim.mblock_m == output_dim.mblock_m && input0_dim.ublock_rt == output_dim.ublock_rt,
                        "Dimension mismatch in op {} for input0",
                        curr_op.name);
                }

                if (is_dim_known(1)) {
                    log_assert(
                        input1_dim.mblock_m == curr_op.m_k && input1_dim.ublock_rt == curr_op.u_kt &&
                            input1_dim.mblock_n == output_dim.mblock_n && input1_dim.ublock_ct == output_dim.ublock_ct,
                        "Dimension mismatch in op {} for input1",
                        curr_op.name);
                }

                if (is_dim_known(0) && is_dim_known(1)) {
                    log_assert(
                        input1_dim.mblock_m >= input0_dim.mblock_n && input1_dim.ublock_rt >= input0_dim.ublock_ct,
                        "Dimension mismatch in op {}",
                        curr_op.name);
                }
            } else {
                log_fatal("Op {} is of an invalid type", curr_op.name);
            }
        }
    }
}

void verify(const tt_fused_op_info& t, tt::ARCH arch) {
    log_assert(t.name != "", "Name must be set for op in tt_fused_op_info");
    log_assert(t.num_inputs >= 0, "Num Inputs must be set for op in tt_fused_op_info");
    log_assert(t.num_intermediates >= 0, "Num Inputs must be set for op in tt_fused_op_info");
    log_assert(
        t.num_intermediates <= tt::constants::MAX_NUM_INTERMEDIATE_BUFFERS,
        "num_intermediates={} used is more than MAX_NUM_INTERMEDIATE_BUFFERS={}",
        t.num_intermediates,
        tt::constants::MAX_NUM_INTERMEDIATE_BUFFERS);

    bool is_int_fused_op = t.accumulation_data_format == DataFormat::Int32;

    log_assert(
        !is_int_fused_op || arch != tt::ARCH::GRAYSKULL,
        "Int fused op is not supported on grayskull {}",
        t.name);
    if (is_int_fused_op) {
        if (t.schedules.back().scheduled_ops.back().type == "dequantization") {
            log_assert(
                t.output_data_format == DataFormat::Float32,  "If last sub op is dequantization, output must be FP32");
        } else {
            log_assert(
                t.output_data_format == DataFormat::Int32 || t.output_data_format == DataFormat::Int8,
                "If last sub op is not dequantization, output must be Int32 or Int8");
        }
    }

    log_assert(t.schedules.size() > 0, "Must have schedules in tt_fused_op_info");
    for (const auto& schedule_info : t.schedules) {
        verify(schedule_info, arch, t);
    }

    verify_tm(t);
    verify_intermed_buffers(t);

    const auto fused_subops_info = netlist_utils::parse_fused_op_info(t);
    verify_op_dims(t, fused_subops_info);
}
