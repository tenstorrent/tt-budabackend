// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "netlist_info_types.hpp"

// Encapsulated specialty members
enum PC : std::uint32_t {
    START = 0x00000000,
    INVALID = 0xABADFACE,
};
enum class VARIABLE_TYPE {
    LOCAL = 0,
    STATIC = 1,
    PARAM = 2,  // Behaves as local, but needs to be passed in during run_program
    INVALID = 4,
};
struct program_variable {
    VARIABLE_TYPE type = VARIABLE_TYPE::LOCAL;
    int value = 0;
};

struct program_loop_info {
    int curr_iter = 0;
    int last_iter = 0;
    int pc = 0;
    bool is_on_device_loop = false;
    bool operator++(int) {
        if (curr_iter < last_iter) {
            curr_iter++;
            return true;
        } else {
            return false;
        }
    };
};

//! Netlist program controls the whole PC/program counter stuff including assignment etc
class netlist_program {
   public:
    //! Resets program and deletes program trace
    void clear();
    //! Resets program execution
    void reset();
    //! Status check for program
    bool done() const;
    bool breakpoint() const;
    //! Get program name
    std::string get_name() const;
    //! Get PC
    int get_current_pc() const;
    int get_breakpoint_pc() const;
    //! Variable Getters and setters
    program_variable get_variable(const string& variable) const;
    VARIABLE_TYPE get_variable_type(const string& variable_string) const;
    bool has_variable(const string& variable) const;
    void set_variable(const string& variable, int value, VARIABLE_TYPE type = VARIABLE_TYPE::INVALID);
    std::unordered_map<string, int> get_variables_of_type(VARIABLE_TYPE type);
    std::unordered_map<string, int> get_variables();

    //! Set Params
    void set_params(const std::map<std::string, std::string>& parameters);

    //! Get current instruction
    const tt_instruction_info& get_current_instruction() const;
    const std::vector<tt_instruction_info> &get_program_trace() const;  // FIXME: Rename to get_program_instruction_list

    //! Get current loop's current iteration and last iteration
    int get_curr_loop_curr_iter() const;
    int get_curr_loop_last_iter() const;

    //! Is it the first iteration for all loops (entire loop stack)?
    bool is_first_iter_entire_loop_stack() const;

    //! For setting and getting whether current loop is looping-on-device
    void set_curr_loop_is_on_device();
    bool get_curr_loop_is_on_device() const;
    bool is_loop_on_device_stack_first_iter() const;
    bool is_loop_on_device_stack_not_first_iter() const;
    bool is_loop_on_device_stack_last_iter() const;
    bool is_loop_on_device_last_iter() const;

    //! This is the main function that will run the current instruction in the program and increment pc to next PC.
    //! caller needs to implement the callback function when the current instruction is an "execute" opcode
    void run_instruction_with_execute_callback(std::function<void(netlist_program&)> execute_callback);
    void run_instruction_with_callbacks(
        std::function<void(netlist_program&)> pre_instrn_callback,
        std::function<void(netlist_program&)> execute_callback,
        std::function<void(netlist_program&)> post_instrn_callback);

    //! Operators that control assignment of PC etc.
    netlist_program& operator=(const netlist_program& other);
    netlist_program& operator=(const int pc);
    void operator++();
    void operator++(int);
    void operator--();
    void operator--(int);

    //! Constructors
    netlist_program(std::string name, std::vector<tt_instruction_info> program_trace) :
        program_trace(program_trace), name(name){};
    netlist_program(){};

    // This is so that backends that don't care about runtime_parameters can treat them as DC.
    void set_ignore_runtime_parameters(const bool& value);

   private:
    // epoch program execution variables and data structures
    bool ignore_runtime_parameters = false;
    bool program_done = false;
    int program_counter = PC::START;
    int breakpoint_pc = PC::INVALID;
    std::vector<program_loop_info> loop_stack = {};
    std::unordered_map<string, string> parameters = {};
    std::unordered_map<string, program_variable> variables = {};
    std::vector<tt_instruction_info> program_trace = {};
    std::string name;
};
string get_variable_name(const string& input);
bool is_variable(const string& input);
bool is_immediate(const string& input);
bool is_constant(const string &input);
bool is_param(const string &input);
void assert_constants_not_modified();
bool is_bool_immediate(const string& input);
bool get_bool_immediate(const string& input);
int get_immediate(const string& input);

template <typename T>
T get_variable_from_map(const string& variable_string, const std::unordered_map<string, T>& variables, const string& immediate_default = "") {
    T result{};
    string var_string = variable_string.empty()? immediate_default : variable_string;
    if (is_immediate(var_string)) {
        try {
            if constexpr (std::is_same<T, int>::value) {
                result = get_immediate(var_string);
            } else if constexpr (std::is_same<T, program_variable>::value) {
                result.value = get_immediate(var_string);
            } else {
                throw std::runtime_error("Cannot get_variable from a map that isn't of program_variable or int");
            }
        } catch (const std::exception& e) {
            log_error("{}", e.what());
            log_fatal(
                "Error Converting string {} to immediate value.  Either missing Variable marker$ or invalid immediate",
                var_string);
        }
    } else if (is_variable(var_string)) {
        string var_name = get_variable_name(var_string);
        if (variables.find(var_name) != variables.end()) {
            result = variables.at(var_name);
        } else {
            log_fatal("Accessing an uninitialized variable {}", var_string);
        }
    } else {
        log_fatal("var_string={} is not a valid variable or immediate format", var_string);
    }
    return result;
}

ostream& operator<<(ostream& os, const netlist_program& t);
