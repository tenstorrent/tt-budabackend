#pragma once
#include "run_config.hpp"
#include "runtime.hpp"
#include "utils.hpp"


class IBackend {
    public:
        virtual ~IBackend() {}
        virtual void push_tensor(const string& q_name, std::shared_ptr<tt_tensor> tensor) = 0;

        virtual void initialize() = 0;
        virtual void run_program(const string& program_name, const ProgramParameters& parameters) = 0;

        virtual void wait_for_idle() = 0;

        virtual tt_tensor_metadata get_metadata(const string& q_name) = 0;

        virtual std::shared_ptr<tt_tensor> pop_tensor(const string& q_name) = 0;
};

class BackendFactory {
    public:
        static std::shared_ptr<IBackend> create(const string& netlist_path, bool silicon);
        static std::shared_ptr<IBackend> create_silicon_debug(const std::string& netlist);
        static std::shared_ptr<IBackend> create_golden(const string& netlist_path);
        static std::shared_ptr<IBackend> create_silicon(const string& netlist_path);
};