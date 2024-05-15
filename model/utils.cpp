// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "utils.hpp"

#include <memory>
#include <fmt/core.h>

namespace tt {

namespace utils {
string pretty_print_bytes(uint64_t bytes)
    {
        const char* suffixes[7];
        suffixes[0] = "B";
        suffixes[1] = "KB";
        suffixes[2] = "MB";
        suffixes[3] = "GB";
        suffixes[4] = "TB";
        suffixes[5] = "PB";
        uint32_t s = 0; // which suffix to use
        double count = bytes;
        while (count >= 1024 && s < 6)
        {
            s++;
            count /= 1024;
        }
        return fmt::format("{:.8f} {}", count, suffixes[s]);
    }
} // namespace utils


namespace args {
std::string strip(std::string const &s) {
    std::string whitespace = " \t\n";
    std::size_t start = s.find_first_not_of(whitespace);
    std::size_t end = s.find_last_not_of(whitespace);
    end += bool(end != std::string::npos);
    return s.substr(start, end);
}

std::tuple<std::string, std::vector<std::string>> get_command_option_and_remaining_args(
    const std::vector<std::string> &args, const std::string &option, const std::optional<std::string> &default_value) {
    std::vector<std::string> remaining_args = args;
    std::vector<std::string>::const_iterator option_pointer =
        std::find(std::begin(remaining_args), std::end(remaining_args), option);
    if (option_pointer != std::end(remaining_args) and (option_pointer + 1) != std::end(remaining_args)) {
        std::string value = *(option_pointer + 1);
        remaining_args.erase(option_pointer, option_pointer + 2);
        return {value, remaining_args};
    }
    if (not default_value.has_value()) {
        throw std::runtime_error("Option not found!");
    }
    return {default_value.value(), remaining_args};
}

std::tuple<std::uint32_t, std::vector<std::string>> get_command_option_uint32_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value) {
    std::vector<std::string> remaining_args = args;
    std::string param;
    if (default_value.has_value()) {
        std::tie(param, remaining_args) =
            get_command_option_and_remaining_args(args, option, std::to_string(default_value.value()));
    } else {
        std::tie(param, remaining_args) = get_command_option_and_remaining_args(args, option);
    }
    return {std::stoul(param, 0, 0), remaining_args};
}

std::tuple<std::int32_t, std::vector<std::string>> get_command_option_int32_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value) {
    std::vector<std::string> remaining_args = args;
    std::string param;
    if (default_value.has_value()) {
        std::tie(param, remaining_args) =
            get_command_option_and_remaining_args(args, option, std::to_string(default_value.value()));
    } else {
        std::tie(param, remaining_args) = get_command_option_and_remaining_args(args, option);
    }
    return {std::stoi(param, 0, 0), remaining_args};
}

std::tuple<double, std::vector<std::string>> get_command_option_double_and_remaining_args(
    const std::vector<std::string> &args, const std::string &option, const std::optional<double> &default_value) {
    std::vector<std::string> remaining_args = args;
    std::string param;
    if (default_value.has_value()) {
        std::tie(param, remaining_args) =
            get_command_option_and_remaining_args(args, option, std::to_string(default_value.value()));
    } else {
        std::tie(param, remaining_args) = get_command_option_and_remaining_args(args, option);
    }
    return {std::stod(param, 0), remaining_args};
}

std::tuple<bool, std::vector<std::string>> has_command_option_and_remaining_args(
    const std::vector<std::string> &args, const std::string &option) {
    std::vector<std::string> remaining_args = args;
    std::vector<std::string>::const_iterator option_pointer =
        std::find(std::begin(remaining_args), std::end(remaining_args), option);
    bool value = option_pointer != std::end(remaining_args);
    if (value) {
        remaining_args.erase(option_pointer);
    }
    return {value, remaining_args};
}

void validate_remaining_args(const std::vector<std::string> &remaining_args) {
    if (remaining_args.size() == 1) {
        // Only executable is left, so all good
        return;
    }
    std::cout << "Remaining args:" << std::endl;
    for (int i = 1; i < remaining_args.size(); i++) {
        std::cout << "\t" << remaining_args.at(i) << std::endl;
    }
    throw std::runtime_error("Not all args were parsed");
}

}  // namespace args
}  // namespace tt
