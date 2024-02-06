#include "arguments.hpp"
#include <vector>
#include <iostream>
#include <iomanip>

/*static*/
void ProgramArgumentsParser::print_usage() {
    size_t name_max = 0;
    size_t description_max = 0;
    for (auto argument : default_program_arguments) {
        if (argument.second.name.length() > name_max)
            name_max = argument.second.name.length();
        if (argument.second.description.length() > description_max)
            description_max = argument.second.description.length();
    }

    std::cout << "Usage:" << std::endl;
    std::cout << usage_header << std::endl;
    std::cout << std::endl;
    std::cout << "Arguments:" << std::endl;

    const int c_indent = 5;
    for (auto argument : default_program_arguments) {
        ProgramArgument& arg = argument.second;
        std::cout << std::left << std::setw(name_max + c_indent) << arg.name << std::setw(description_max + c_indent) << arg.description << "Default: " << arg.value << std::endl;
    }
}

/*static*/
ProgramArguments ProgramArgumentsParser::parse_arguments(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);
    ProgramArgumentsMap arguments = default_program_arguments;
    size_t i = 1;
    while (i < input_args.size()) {
        if (arguments.find(input_args[i]) != arguments.end()) {
            ProgramArgument& argument = arguments[input_args[i]];
            if (argument.type != ProgramArgument::BOOLEAN) {
                ++i;
                argument.value = input_args[i];
            }
            else {
                argument.value = BOOLEAN_TRUE;
            }
            ++i;
        }
        else {
            print_usage();
            exit(1);
        }
    }

    for (auto arg : arguments) {
        if (arg.second.required && arg.second.value.empty()) {
            print_usage();
            exit(1);
        }
    }

    return ProgramArguments(arguments);
}