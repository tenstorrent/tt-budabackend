#include "program_controller.hpp"
#include "arguments.hpp"
#include <filesystem>

const std::string NETLIST        = "--netlist";
const std::string BIN_INPUT      = "--bin-input";
const std::string SILICON_OUTPUT = "--silicon-output";
const std::string GOLDEN_OUTPUT  = "--golden-output";
const std::string DEBUDA_STUB    = "--debuda-stub";

ProgramArgumentsMap ProgramArgumentsParser::default_program_arguments = {
    {NETLIST,        {NETLIST,               "", "Path to netlist file",                                               ProgramArgument::STRING,  true  }},
    {BIN_INPUT,      {BIN_INPUT,             "", "Path to input binary files, and generated input files",              ProgramArgument::STRING,  false }},
    {SILICON_OUTPUT, {SILICON_OUTPUT,        "", "Netlist will run on silicon and data will be stored on this path.",  ProgramArgument::STRING,  false }},
    {GOLDEN_OUTPUT,  {GOLDEN_OUTPUT,         "", "Netlist will run on golden and data will be stored on this path.",   ProgramArgument::STRING,  false }},
    {DEBUDA_STUB,    {DEBUDA_STUB,           "", "Starts backend without initialization, so debuda can connect.",      ProgramArgument::BOOLEAN, false }},
};

std::string ProgramArgumentsParser::usage_header = 
"Golden Run:  ./build/test/dbd/tools/run --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --bin-input t_bin/in --golden-output t_bin/out_g\n"
"Silicon Run: ./build/test/dbd/tools/run --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --bin-input t_bin/in --silicon-output t_bin/out\n"
"Diff:        ./build/test/dbd/tools/run --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --silicon-output t_bin/out --golden-output t_bin/out_g\n"
"Debuda Stub: ./build/test/dbd/tools/run --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --debuda-stub\n";

void run_program(const ProgramConfig& config, const std::string& input_dir, const std::string& output_dir, bool on_silicon) {
    std::filesystem::create_directories(input_dir);
    std::filesystem::create_directories(output_dir);
    CommonProgramController program(
        config,
        input_dir,
        output_dir,
        on_silicon);
    program.run();
}

void run_golden(const ProgramConfig& config, const std::string& input_dir, const std::string& output_dir) {
    run_program(config, input_dir, output_dir, false);
}

void run_silicon(const ProgramConfig& config, const std::string& input_dir, const std::string& output_dir) {
    run_program(config, input_dir, output_dir, true);
}

int run_diff(const ProgramConfig& config, const std::string& silicon_out, const std::string& golden_out) {
    DiffChecker diff(config, silicon_out, golden_out);
    diff.run_check();
    return diff.is_success() ? 0 : 1;
}

void run_debuda_stub_only(const std::string& netlist) {
    std::shared_ptr<IBackend> backend = BackendFactory::create_silicon_debug(netlist);
    std::cout<< "Press any key...";
    std::cin.ignore();
}

bool should_run_golden(const ProgramArguments& arguments) {
    return arguments.has(BIN_INPUT) && arguments.has(GOLDEN_OUTPUT);
}

bool should_run_silicon(const ProgramArguments& arguments) {
    return arguments.has(BIN_INPUT) && arguments.has(SILICON_OUTPUT);
}

bool should_run_diff(const ProgramArguments& arguments) {
    return arguments.has(GOLDEN_OUTPUT) && arguments.has(SILICON_OUTPUT);
}

bool should_start_debuda_stub_only(const ProgramArguments& arguments) {
    return arguments.has(NETLIST) && arguments.has(DEBUDA_STUB);
}

int main(int argc, char** argv) {

    ProgramArguments arguments = ProgramArgumentsParser::parse_arguments(argc, argv);

    if (should_start_debuda_stub_only(arguments)) {
        run_debuda_stub_only(arguments.value(NETLIST));
        return 0;
    }

    ProgramConfig config(arguments.value(NETLIST));

    if (should_run_golden(arguments)) {
        run_golden(config, arguments.value(BIN_INPUT), arguments.value(GOLDEN_OUTPUT));
    }

    if (should_run_silicon(arguments)) {
        run_silicon(config, arguments.value(BIN_INPUT), arguments.value(SILICON_OUTPUT));
    }

    if (should_run_diff(arguments)) {
        return run_diff(config, arguments.value(SILICON_OUTPUT), arguments.value(GOLDEN_OUTPUT));
    }

    return 0;
}