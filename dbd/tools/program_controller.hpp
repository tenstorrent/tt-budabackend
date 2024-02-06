#pragma once
#include "run_config.hpp"
#include "verif.hpp"
#include "backend.hpp"
#include "tensor_rw.hpp"
#include "verif_comparison.hpp"

class ProgramConfig {
    public:
        ProgramConfig(const std::string& netlist_path);
        ~ProgramConfig() {}

        tt::ARCH get_architecture() const;

        StimulusConfig get_stimulus_config() const;

        ComparsionConfig get_comparsion_config() const;

        bool is_host_queue(const tt_queue_info& q_info) const;

        std::vector<std::string> get_input_queue_names() const;

        std::vector<std::string> get_output_queue_names() const;

        const tt_queue_info& get_queue_info(const string& q_name) const;

        int get_entry_count(const string& q_name) const;

        ProgramParameters get_program_parameters(const string& program_name);

        std::vector<std::string> get_program_order() const;

        const std::string& get_netlist_path() const;
    private:
        netlist_workload_data _netlist;
        VerifTestConfig _test_config;
        std::string _netlist_path;
};

class ProgramController {
    public:
        ProgramController(const ProgramConfig& config);
        virtual ~ProgramController() {}
        virtual void run();

    protected:

        void initialize();
        void run_programs();

        virtual void process_results();
        void fill_input_queues();

        const ProgramConfig& get_program_config() const;

    protected:
        virtual std::shared_ptr<IBackend> get_backend() = 0;
        virtual ITensorWriter&  get_tensor_writer() = 0;
        virtual ITensorBuilder& get_tensor_builder() = 0;

    private:
        ProgramConfig _program_config;
};

class CommonProgramController : public ProgramController {
    public:
        CommonProgramController(const ProgramConfig& config, const std::string& bin_in_path, const std::string& bin_out_path, bool silicon);
        CommonProgramController(const ProgramConfig& config, const std::string& bin_in_path, const std::string& bin_out_path, std::shared_ptr<IBackend> backend);

        virtual ~CommonProgramController() {}

    protected:
        virtual std::shared_ptr<IBackend> get_backend();
        virtual ITensorWriter&  get_tensor_writer();
        virtual ITensorBuilder& get_tensor_builder();
    private:
        std::shared_ptr<IBackend> _backend;
        TensorRW _tensor_rw;
};

class DiffChecker {
    public:
        DiffChecker(const ProgramConfig& config, const std::string& bin_path1, const std::string& bin_path2);
        ~DiffChecker() {}
        void run_check();

        void diff_check(const std::string& q_name, int entry_id);

        bool is_success() const;

    private:
        ProgramConfig _program_config;

        TensorRW _t1;
        TensorRW _t2;

        bool _verification_failed;
};