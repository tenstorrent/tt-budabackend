#pragma once
#include "run_config.hpp"
#include "verif/verif_utils.hpp"
#include <exception>

class ITensorBuilder {
    public:
        virtual std::shared_ptr<tt_tensor> build_tensor(const tt_queue_info& q_info, const int entry_id, tt::tt_tensor_metadata metadata) = 0;
};

class ITensorWriter {
    public:
        virtual void write_tensor(const std::string& q_name, const int entry_id, std::shared_ptr<tt_tensor> tensor) = 0;
};

class TensorRW : public ITensorBuilder, public ITensorWriter {
    public:
        TensorRW(const std::string& path);
        TensorRW(const std::string& in_path, const std::string& out_path);
        TensorRW(const std::string& in_path, const std::string& out_path, const StimulusConfig& stimulus_config);
        virtual std::shared_ptr<tt_tensor> build_tensor(const tt_queue_info& q_info, const int entry_id, tt::tt_tensor_metadata metadata);
        virtual void write_tensor(const std::string& q_name, const int entry_id, std::shared_ptr<tt_tensor> tensor);

    private:
        std::string _in_path;
        std::string _out_path;
        StimulusConfig _stimulus_config;
};
