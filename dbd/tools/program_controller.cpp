#include "program_controller.hpp"

void set_architecture() {
    const char *env_arch = std::getenv("ARCH_NAME");
    if (env_arch == nullptr) {
        tt::ARCH arch = detect_arch();
        stringstream ss;
        ss << arch;
        setenv("ARCH_NAME", ss.str().c_str(), 0);
        log_info(tt::LogTest, "Setting architecture to {}", ss.str());
    }
}

ProgramConfig::ProgramConfig(const std::string& netlist_path)
    : _netlist_path(netlist_path) 
    {
        set_architecture();
        _netlist = netlist_workload_data(netlist_path);
         _test_config = verif::test_config::read_from_yaml_file(netlist_path);
    }

tt::ARCH ProgramConfig::get_architecture() const {
    return _netlist.device_info.arch;
}

StimulusConfig ProgramConfig::get_stimulus_config() const {
    if (_test_config.stimulus_configs.empty())
        return ConfigBuilder::build_default_stimulus();

    return _test_config.stimulus_configs;
}

ComparsionConfig ProgramConfig::get_comparsion_config() const {
    if (_test_config.comparison_configs.empty())
        return ConfigBuilder::build_default_comparsion();

    return _test_config.comparison_configs;
}

bool ProgramConfig::is_host_queue(const tt_queue_info& q_info) const {
    return q_info.input == "HOST";
}

std::vector<std::string> ProgramConfig::get_input_queue_names() const {
    std::vector<std::string> input_queue_names;
    for (const auto& io : _netlist.queues) {
        const tt_queue_info& q_info = io.second.my_queue_info;

        if (is_host_queue(q_info))
            input_queue_names.push_back(q_info.name);
    }

    return input_queue_names;
}

std::vector<std::string> ProgramConfig::get_output_queue_names() const {
    std::vector<std::string> output_queue_names;
    for (const auto& io : _netlist.queues) {
        const tt_queue_info& q_info = io.second.my_queue_info;

        if (!is_host_queue(q_info))
            output_queue_names.push_back(q_info.name);
    }
    return output_queue_names;
}

const tt_queue_info& ProgramConfig::get_queue_info(const string& q_name) const {
    return _netlist.queues.find(q_name)->second.my_queue_info;
}

int ProgramConfig::get_entry_count(const string& q_name) const {
    return get_queue_info(q_name).input_count;
}

const std::string& ProgramConfig::get_netlist_path() const {
    return _netlist_path;
}

ProgramParameters ProgramConfig::get_program_parameters(const string& program_name) {
    ProgramParameters parameters;
    std::vector<tt_instruction_info> instructions = _netlist.programs[program_name].get_program_trace();

    // Find parameters in program
    std::vector<std::string> params;
    for (size_t i = 0; i < instructions.size(); ++i)
        if (instructions[i].opcode == INSTRUCTION_OPCODE::Param)
            for (const auto &var : instructions[i].vars)
                params.push_back(std::get<0>(var));

    // Set parameter values
    for (size_t i = 0; i < params.size(); ++i) {
        const int c_default_value = 1;
        int param_value = c_default_value;
        // try to find parameter in test configuration
        const std::string variable_prefix = "$p_";
        if (params[i].rfind(variable_prefix, 0) == 0) {
            string test_param_name = params[i].substr(variable_prefix.length());
            param_value = verif::test_config::get<int>(_test_config.test_args, test_param_name, c_default_value);
        }
        parameters.insert({params[i], std::to_string(param_value)});
    }

    return parameters;
}

std::vector<std::string> ProgramConfig::get_program_order() const {
    return _netlist.program_order;
}


ProgramController::ProgramController(const ProgramConfig& config)
    : _program_config(config) {
    }

void ProgramController::run() {
    initialize();
    fill_input_queues();
    run_programs();
    process_results();
}

void ProgramController::initialize() {
    get_backend()->initialize();
}

void ProgramController::run_programs() {
    for (const std::string& program_name : _program_config.get_program_order()) {
        log_info(tt::LogTest, "Running program {}", program_name);
        get_backend()->run_program(program_name, _program_config.get_program_parameters(program_name));
        get_backend()->wait_for_idle();
        log_info(tt::LogTest, "Running done {}", program_name);
    }
}

void ProgramController::process_results() {
    for (const std::string& q_name : _program_config.get_output_queue_names()) {
        for (int entry_id = 0; entry_id < _program_config.get_entry_count(q_name); ++entry_id) {
            log_info(tt::LogTest, "Processing results queue_name {} entry_id {}", q_name, entry_id);
            std::shared_ptr<tt_tensor> tensor = get_backend()->pop_tensor(q_name);
            get_tensor_writer().write_tensor(q_name, entry_id, tensor);
        }
    }
}

void ProgramController::fill_input_queues() {
    for (const std::string& q_name : _program_config.get_input_queue_names()) {
        for (int i = 0; i < _program_config.get_entry_count(q_name); ++i) {
            log_info(tt::LogTest, "Filling inputs queue name {} entry index {}", q_name, i);
            std::shared_ptr<tt_tensor> tensor =
                get_tensor_builder().build_tensor(
                    _program_config.get_queue_info(q_name),
                    i,
                    get_backend()->get_metadata(q_name));
            get_backend()->push_tensor(q_name, tensor);
        }
    }
}

const ProgramConfig& ProgramController::get_program_config() const { return _program_config; }

CommonProgramController::CommonProgramController(const ProgramConfig& config, const std::string& bin_in_path, const std::string& bin_out_path, bool silicon)
    : ProgramController(config)
    , _tensor_rw(bin_in_path, bin_out_path, get_program_config().get_stimulus_config()) {
        _backend = BackendFactory::create(config.get_netlist_path(), silicon);
    }

CommonProgramController::CommonProgramController(const ProgramConfig& config, const std::string& bin_in_path, const std::string& bin_out_path, std::shared_ptr<IBackend> backend)
    : ProgramController(config)
    , _tensor_rw(bin_in_path, bin_out_path, get_program_config().get_stimulus_config()) {
        _backend = backend;
    }

std::shared_ptr<IBackend> CommonProgramController::get_backend() {
    return _backend;
}

ITensorWriter& CommonProgramController::get_tensor_writer() {
    return _tensor_rw;
}

ITensorBuilder& CommonProgramController::get_tensor_builder() {
    return _tensor_rw;
}

DiffChecker::DiffChecker(const ProgramConfig& config, const std::string& bin_path1, const std::string& bin_path2)
: _program_config(config)
, _t1(bin_path1)
, _t2(bin_path2)
, _verification_failed(false) {

}

void DiffChecker::run_check() {
    _verification_failed = false;
    for (auto q_name : _program_config.get_output_queue_names()) {
        for (int i = 0; i < _program_config.get_entry_count(q_name); ++i) {
            diff_check(q_name, i);
        }
    }
}

void DiffChecker::diff_check(const std::string& q_name, int entry_id) {

    tt::tt_tensor_metadata md = get_tensor_metadata_for_queue(_program_config.get_queue_info(q_name));
    std::shared_ptr<tt_tensor> tensor_1 = _t1.build_tensor(
        _program_config.get_queue_info(q_name), entry_id, md);
    std::shared_ptr<tt_tensor> tensor_2 = _t2.build_tensor(
        _program_config.get_queue_info(q_name), entry_id, md);

    if (not verif::comparison::compare_tensors(
        *tensor_2,
        *tensor_1,
        verif::comparison::get_config(q_name, _program_config.get_comparsion_config()))) {
        _verification_failed = true;
        log_error("Queue {}, entry idx={} mismatched", q_name, entry_id);
    }
}

bool DiffChecker::is_success() const { return !_verification_failed; }
