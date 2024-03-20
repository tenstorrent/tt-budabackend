// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_program.hpp"

#include <algorithm>

#include "netlist_utils.hpp"
#include "utils/logger.hpp"

string get_variable_name(const string &input) {
    string var_name = input;
    var_name.erase(std::remove(var_name.begin(), var_name.end(), '$'), var_name.end());
    return var_name;
}

bool is_variable(const string &input) { return input.find("$") != std::string::npos; }
bool is_constant(const string &input) { return input.compare(0, 3, "$c_") == 0;}
bool is_param(const string &input) { return input.compare(0, 3, "$p_") == 0;}
bool is_immediate(const string &input) {
    return (not is_variable(input)) and (input != "");  // Not variable and valid immediate
}

bool is_bool_immediate(const string &input) {
    string tmp = input;
    if (is_immediate(input)) {
        std::transform(input.begin(), input.end(), tmp.begin(), ::tolower);
        if ((tmp == "false") or (tmp == "true")) {
            return true;
        }
    }
    return false;
}
bool get_bool_immediate(const string &input) {
    string tmp = input;
    std::transform(input.begin(), input.end(), tmp.begin(), ::tolower);
    if ((tmp == "false") or (tmp == "0")) {
        return false;
    } else if ((tmp == "true") or (tmp == "1")) {
        return true;
    } else {
        log_fatal("{} is an invalid boolean immediate...", input);
        return false;
    }
}
int get_immediate(const string &input) {
    if (is_bool_immediate(input)) {
        return get_bool_immediate(input);
    } else {
        return stoi(input);
    }
}

////////////////////////////////
// netlist_program
////////////////////////////////
void netlist_program::reset() {
    program_counter = PC::START;
    breakpoint_pc = PC::INVALID;
    loop_stack.clear();
    // We should only clear variables that are not static
    std::vector<std::string> local_vars = {};
    for (const auto &var_it : variables) {
        if ((var_it.second.type == VARIABLE_TYPE::LOCAL) or (var_it.second.type == VARIABLE_TYPE::PARAM)) {
            local_vars.push_back(var_it.first);
        }
    }
    for (const auto &local_var : local_vars) {
        variables.erase(local_var);
    }
}
void netlist_program::clear() {
    this->reset();
    program_trace.clear();
    variables.clear();
}
bool netlist_program::done() const {return get_current_instruction().opcode == INSTRUCTION_OPCODE::EndProgram; }
bool netlist_program::breakpoint() const { return program_counter == breakpoint_pc; }
const tt_instruction_info& netlist_program::get_current_instruction() const {
    if ((uint32_t)program_counter >= program_trace.size())
        log_fatal("PC went beyond program instructions...");
    return program_trace.at(program_counter);
}
const std::vector<tt_instruction_info> &netlist_program::get_program_trace() const { return program_trace; }
std::string netlist_program::get_name() const { return name; }
int netlist_program::get_current_pc() const { return program_counter; }
int netlist_program::get_breakpoint_pc() const { return breakpoint_pc; }
program_variable netlist_program::get_variable(const string& variable_string) const {
    return get_variable_from_map(variable_string, variables);
}
VARIABLE_TYPE netlist_program::get_variable_type(const string& variable_string) const {
    VARIABLE_TYPE result = VARIABLE_TYPE::INVALID;
    string var_name = get_variable_name(variable_string);
    if (has_variable(variable_string)) {
        result = variables.at(var_name).type;
    } else {
        log_fatal("Accessing an uninitialized variable {}", variable_string);
    }
    return result;
}
bool netlist_program::has_variable(const string& variable_string) const {
    bool result = false;
    if (is_variable(variable_string)) {
        string var_name = get_variable_name(variable_string);
        if (variables.find(var_name) != variables.end()) {
            result = true;
        }
    }
    return result;
}
void netlist_program::set_variable(const string& variable_string, int value, VARIABLE_TYPE type) {
    if (is_immediate(variable_string)) {
        log_fatal(
            "Error Converting string {} to variable Missing Variable marker '$' ", variable_string);
    }
    if (not has_variable(variable_string)) {
        // Not a global or a previous variable, just assign to local variables
        variables.insert({get_variable_name(variable_string), {.type = type, .value = value}});
    } else {
        log_trace(tt::LogNetlist, "set_variable variable={} to value={}", variable_string, value);
        if (type != VARIABLE_TYPE::INVALID) {
            variables.at(get_variable_name(variable_string)).type = type;
        }
        variables.at(get_variable_name(variable_string)).value = value;
        log_trace(tt::LogNetlist, "set_variable variable={} to value={}", variable_string, value);
    }
}
std::unordered_map<string, int> netlist_program::get_variables() {
    std::unordered_map<string, int> variables_snapshot = {};
    for (const auto &var_it : variables) {
        variables_snapshot.insert({var_it.first, var_it.second.value});
    }
    return variables_snapshot;
}
std::unordered_map<string, int> netlist_program::get_variables_of_type(VARIABLE_TYPE type) {
    std::unordered_map<string, int> variables_snapshot = {};
    for (const auto &var_it : variables) {
        if (type == var_it.second.type) {
            variables_snapshot.insert({var_it.first, var_it.second.value});
        }
    }
    return variables_snapshot;
}
void netlist_program::set_params(const std::map<std::string, std::string> &parameters) {
    for (const auto &parameter_it : parameters) {
        log_trace(tt::LogNetlist, "set_parameter parameter={} to value={}", parameter_it.first, parameter_it.second);
        if (this->parameters.find(parameter_it.first) != this->parameters.end()) {
            this->parameters.at(parameter_it.first) = parameter_it.second;
        } else {
            this->parameters.insert({parameter_it.first, parameter_it.second});  // Convert to unordered
        }
        log_trace(tt::LogNetlist, "set_parameter parameter={} to value={}", parameter_it.first, parameter_it.second);
    }
}

int netlist_program::get_curr_loop_curr_iter() const {
    if (this->loop_stack.size() > 0){
        return this->loop_stack.back().curr_iter;
    }else{
        return -1;
    }
}

int netlist_program::get_curr_loop_last_iter() const {
    if (this->loop_stack.size() > 0){
        return this->loop_stack.back().last_iter;
    }else{
        return -1;
    }
}


bool netlist_program::is_first_iter_entire_loop_stack() const {
    if (this->loop_stack.size() > 0){
        bool all_first_iter = true;
        for (int depth=0; depth<this->loop_stack.size(); depth++){
            all_first_iter &= loop_stack.at(depth).curr_iter == 0;
        }
        return all_first_iter;
    }else{
        return true;
    }
}

void netlist_program::set_curr_loop_is_on_device() {
    log_assert(this->loop_stack.size() > 0, "{} : Loopstack is empty, cannot set looping_on_device", __FUNCTION__);
    this->loop_stack.back().is_on_device_loop = true;
}

bool netlist_program::get_curr_loop_is_on_device() const {
    return this->loop_stack.size() == 0 ? 0 : this->loop_stack.back().is_on_device_loop;
}

bool netlist_program::is_loop_on_device_stack_first_iter() const {
    return get_curr_loop_is_on_device() && is_first_iter_entire_loop_stack();
}

bool netlist_program::is_loop_on_device_stack_not_first_iter() const {
    return get_curr_loop_is_on_device() && !is_first_iter_entire_loop_stack();
}

bool netlist_program::is_loop_on_device_last_iter() const {
    return get_curr_loop_is_on_device() && this->loop_stack.back().curr_iter == this->loop_stack.back().last_iter;
}

// True only if current loop on device is in final iteration and no other loops on device.
bool netlist_program::is_loop_on_device_stack_last_iter() const {
    int loop_stack_depth = this->loop_stack.size();
    bool is_final_loop_on_device =  get_curr_loop_is_on_device() &&
                                    this->loop_stack.back().curr_iter == this->loop_stack.back().last_iter &&
                                    (loop_stack_depth == 1 || !this->loop_stack.at(loop_stack_depth-2).is_on_device_loop);
    return is_final_loop_on_device;
}


netlist_program &netlist_program::operator=(const netlist_program &other) {
    if (this == &other) {
        return *this;
    }
    loop_stack = other.loop_stack;
    program_counter = other.program_counter;
    program_trace = other.program_trace;
    breakpoint_pc = other.breakpoint_pc;
    variables = other.variables;
    return *this;
}

netlist_program &netlist_program::operator=(const int pc) {
    program_counter = pc;
    return *this;
}
void netlist_program::operator++() {
    const tt_instruction_info &current_instruction = get_current_instruction();
    // Branching handler on PC increment
    if (current_instruction.opcode == INSTRUCTION_OPCODE::EndLoop) {
        log_assert(loop_stack.size() > 0, "Unexpected EndLoop at program: {} pc: {}", name, program_counter);
        program_loop_info &loop = loop_stack.back();
        if (loop++) {
            // if still looping
            // decrement loop count on top of stack
            // return pc to instruction at top of loop pc stack
            program_counter = loop.pc;
        } else {
            // if count done pop loop stacks
            // continue execution
            loop_stack.pop_back();
            program_counter++;
            log_trace(tt::LogNetlist, "Loop Done: PC={}", program_counter);
        }
    } else {
        program_counter++;
    }
}
void netlist_program::operator++(int) {
    operator++();
}
void netlist_program::operator--() {
    program_counter--;
}
void netlist_program::operator--(int) {
    operator--();
}

ostream &operator<<(ostream &os, const netlist_program &t) {
    os << "netlist_program{";
    os << "  .program_counter = 0x" << std::hex << t.get_current_pc() << ", ";
    os << "  .breakpoint_pc = 0x" << t.get_breakpoint_pc() << std::dec << ", ";
    for (auto it : t.get_program_trace()) {
        os << it << std::endl;
    }
    os << "}";
    return os;
}

void netlist_program::run_instruction_with_execute_callback(std::function<void(netlist_program &)> execute_callback) {
    run_instruction_with_callbacks({}, execute_callback, {});
}
void netlist_program::set_ignore_runtime_parameters(const bool &value) { this->ignore_runtime_parameters = value; }
void netlist_program::run_instruction_with_callbacks(
    std::function<void(netlist_program &)> pre_instrn_callback,
    std::function<void(netlist_program &)> execute_callback,
    std::function<void(netlist_program &)> post_instrn_callback) {
    const tt_instruction_info& instrn = get_current_instruction();
    if (breakpoint()) {
        log_warning(tt::LogNetlist, "Hit Breakpoint -- Looping on break");
        return;
    }
    log_trace(tt::LogNetlist, "Running {}", instrn);

    // Pre instruction
    if (pre_instrn_callback)
        pre_instrn_callback(*this);
    // Run instruction
    if (instrn.opcode == INSTRUCTION_OPCODE::Var) {
        for (const auto &var : instrn.vars) {
            if (has_variable(std::get<0>(var))) {
                if ((get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::LOCAL) or
                    (get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::PARAM) or
                    (get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::STATIC)) {
                    log_fatal(
                        "Local Variable={} declaration masks a previous param/static/local variable declaration",
                        std::get<0>(var));
                } else {
                    log_fatal("Internal Error -- Not Supported Variable Type detected");
                }
            }
            set_variable(std::get<0>(var), std::get<1>(var), VARIABLE_TYPE::LOCAL);
        }
    } else if (instrn.opcode == INSTRUCTION_OPCODE::StaticVar) {
        for (const auto &var : instrn.vars) {
            if (not has_variable(std::get<0>(var))) {
                set_variable(std::get<0>(var), std::get<1>(var), VARIABLE_TYPE::STATIC);
            } else {
                if ((get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::LOCAL) or
                    (get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::PARAM)) {
                    log_fatal(
                        "Static Variable={} declaration masks a previous local/param variable declaration",
                        std::get<0>(var));
                } else if (get_variable_type(std::get<0>(var)) == VARIABLE_TYPE::STATIC) {
                    log_trace(
                        tt::LogNetlist,
                        "Static Variable={} exists and is {}",
                        std::get<0>(var),
                        get_variable(std::get<0>(var)).value);
                } else {
                    log_fatal("Internal Error -- Not Supported Variable Type detected");
                }
            }
        }
    } else if (instrn.opcode == INSTRUCTION_OPCODE::Param) {
        for (const auto &var : instrn.vars) {
            string variable_string = std::get<0>(var);
            if (this->ignore_runtime_parameters) {
                // Ignore runtime parameters --> Make it all initialized to a default value
                set_variable(variable_string, 1, VARIABLE_TYPE::PARAM);
            } else if (not has_variable(variable_string)) {
                if (this->parameters.find(variable_string) != this->parameters.end()) {
                    if (is_immediate(parameters.at(variable_string))) {
                        int param_value = get_immediate(parameters.at(variable_string));
                        log_trace(tt::LogNetlist, "Program {} param {}={}", this->name, variable_string, param_value);
                        set_variable(variable_string, param_value, VARIABLE_TYPE::PARAM);
                    } else {
                        log_fatal(
                            "Param Variable={} has a non-immediate value={} passed in during run_program for "
                            "program={}",
                            variable_string,
                            parameters.at(variable_string),
                            this->name);
                    }
                } else {
                    log_fatal(
                        "Param Variable={} is declared as a param for program={} but is not passed in during "
                        "run_program",
                        variable_string,
                        this->name);
                }
            } else {
                if ((get_variable_type(variable_string) == VARIABLE_TYPE::LOCAL) or
                    (get_variable_type(variable_string) == VARIABLE_TYPE::PARAM) or
                    (get_variable_type(variable_string) == VARIABLE_TYPE::STATIC)) {
                    log_fatal(
                        "Param Variable={} declaration masks a previous param/static/local variable declaration",
                        variable_string);
                } else {
                    log_fatal("Internal Error -- Not Supported Variable Type detected");
                }
            }
        }
    } else if (instrn.opcode == INSTRUCTION_OPCODE::VarInst) {
        if (instrn.varinst_opcode == VAR_INSTRUCTION_OPCODE::Set) {
            log_assert(instrn.vars.size() == 2, "Expected 2 variables for Load varinst");
            set_variable(std::get<0>(instrn.vars[0]), get_variable(std::get<0>(instrn.vars[1])).value);
        }
        if (instrn.varinst_opcode == VAR_INSTRUCTION_OPCODE::Add) {
            log_assert(instrn.vars.size() == 3, "Expected 3 variables for + varinst");
            set_variable(
                std::get<0>(instrn.vars[0]),
                get_variable(std::get<0>(instrn.vars[1])).value + get_variable(std::get<0>(instrn.vars[2])).value);
        }
        if (instrn.varinst_opcode == VAR_INSTRUCTION_OPCODE::Mul) {
            log_assert(instrn.vars.size() == 3, "Expected 3 variables for * varinst");
            set_variable(
                std::get<0>(instrn.vars[0]),
                get_variable(std::get<0>(instrn.vars[1])).value * get_variable(std::get<0>(instrn.vars[2])).value);
        }
        if (instrn.varinst_opcode == VAR_INSTRUCTION_OPCODE::Inc) {
            log_assert(instrn.vars.size() == 2, "Expected 2 variables for increment varinst");
            set_variable(
                std::get<0>(instrn.vars[0]),
                get_variable(std::get<0>(instrn.vars[0])).value + get_variable(std::get<0>(instrn.vars[1])).value);
        }
        if (instrn.varinst_opcode == VAR_INSTRUCTION_OPCODE::IncWrap) {
            log_assert(instrn.vars.size() == 3, "Expected 3 variables for increment varinst");
            int old_val = get_variable(std::get<0>(instrn.vars[0])).value;
            int increment = get_variable(std::get<0>(instrn.vars[1])).value;
            int mod_wrap = get_variable(std::get<0>(instrn.vars[2])).value;
            // Offset needs to account for when we go below 0, so we will wrap, it is the LCM that is > increment but
            // multiple of mod_wrap
            int offset = 0;
            while ((offset + increment) < 0) {
                offset += mod_wrap;
            }
            int result = (old_val + increment + offset) % mod_wrap;
            log_trace(tt::LogNetlist, "IncWrap: {}=({}+{})\%{}", result, old_val, increment, mod_wrap);
            set_variable(std::get<0>(instrn.vars[0]), result);
        }
    } else if (instrn.opcode == INSTRUCTION_OPCODE::AllocateQueue) {
        std::vector<std::string> queues;
        for (const auto &var : instrn.vars) {
            string variable_string = std::get<0>(var);
            queues.push_back(variable_string);
        }
        log_trace(tt::LogNetlist, "AllocateQueue: {}", fmt::join(queues, ", "));
    } else if (instrn.opcode == INSTRUCTION_OPCODE::DeallocateQueue) {
        std::vector<std::string> queues;
        for (const auto &var : instrn.vars) {
            string variable_string = std::get<0>(var);
            queues.push_back(variable_string);
        }
        log_trace(tt::LogNetlist, "DeallocateQueue: {}", fmt::join(queues, ", "));
    } else if (instrn.opcode == INSTRUCTION_OPCODE::Loop) {
        // push return trace index on loop stack
        int loop_count = get_variable(instrn.loop_count).value;
        log_trace(tt::LogNetlist, "Loop Start: PC={} loop_count:{} = {}", program_counter, instrn.loop_count, loop_count);
        if (loop_count < 1) {
            log_fatal("Loop count needs to be >= 1 -- loop_count:{} = {}", instrn.loop_count, loop_count);
        }
        // push loop info to stack
        loop_stack.push_back({0, loop_count - 1, program_counter + 1});
    } else if (instrn.opcode == INSTRUCTION_OPCODE::Execute) {
        execute_callback(*this);
    }
    
    // Post instruction
    if (post_instrn_callback)
        post_instrn_callback(*this);
   
    // Increment Program Counter
    (*this)++;
}
