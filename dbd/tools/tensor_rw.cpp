#include "tensor_rw.hpp"
#include "verif/verif_stimulus.hpp"
#include "verif/verif_rand_utils.hpp"

std::string get_path(const std::string& path) {
    if (!path.empty()) {
        if (path.back() != '/') {
            return path + '/';
        }
    }
    return path;
}

void print_tensor(const std::string& q_name, const int entry_id, std::shared_ptr<tt_tensor> tensor) {
    log_info(tt::LogTest, "Writing Tensor[q_name={}, entry_id={}]", q_name, entry_id);
    for (int iw = 0; iw < tensor->getw(); iw++) {
        for (int iz = 0; iz < tensor->getz(); iz++) {
            for (int irt = 0; irt < tensor->getrt(); irt++) {
                for (int ict = 0; ict < tensor->getct(); ict++) {
                    const tt_tile &tile = tensor->tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    std::cout << q_name << " [" << iw << ", " << iz << ", " << irt << ", " << ict << "]" << std::endl;
                    std::cout << tile.get_string() << std::endl;
                }
            }
        }
    }
}

TensorRW::TensorRW(const std::string& path) {
    _in_path = get_path(path);
    _out_path = get_path(path);
}

TensorRW::TensorRW(const std::string& in_path, const std::string& out_path) {
    _in_path = get_path(in_path);
    _out_path = get_path(out_path);
}

TensorRW::TensorRW(const std::string& in_path, const std::string& out_path, const StimulusConfig& stimulus_config) {
    _in_path = get_path(in_path);
    _out_path = get_path(out_path);
    _stimulus_config = stimulus_config;
    verif::random::tt_rnd_set_seed(verif::random::tt_gen_seed());
}

/*virtual*/
std::shared_ptr<tt_tensor> TensorRW::build_tensor(const tt_queue_info& q_info, const int entry_id, tt::tt_tensor_metadata metadata) {
    std::shared_ptr<tt_tensor> tensor = std::make_shared<tt_tensor>(metadata);

    if (!_in_path.empty() && tt::data_binary::does_file_exists(_in_path, q_info.name, entry_id)) {
        log_info(tt::LogTest, "Loading Tensor[q_name={}, entry_id={}] from {}", q_info.name, entry_id, _in_path);
        verif::read_tensor_from_binary(_in_path, q_info, entry_id, *tensor, false);
        return tensor;
    }

    if (!_out_path.empty() && tt::data_binary::does_file_exists(_out_path, q_info.name, entry_id)) {
        log_info(tt::LogTest, "Loading Tensor[q_name={}, entry_id={}] from {}", q_info.name, entry_id, _out_path);
        verif::read_tensor_from_binary(_out_path, q_info, entry_id, *tensor, false);
        return tensor;
    }

    if (!_stimulus_config.empty()) {
        log_info(tt::LogTest, "Generating Tensor[q_name={}, entry_id={}]", q_info.name, entry_id);
        verif::stimulus::generate_tensor(verif::stimulus::get_config(q_info.name, _stimulus_config), *tensor);
        if (!_out_path.empty()) {
            log_info(tt::LogTest, "Saving Tensor[q_name={}, entry_id={}] to {}", q_info.name, entry_id, _in_path);
            verif::write_tensor_to_binary(_in_path, q_info.name, entry_id, *tensor);
        }
        return tensor;
    }

    log_error("Cannot create Tensor[q_name={}, entry_id={}]", q_info.name, entry_id);
    throw std::runtime_error(std::string("Cannot create tensor"));
}

/*virtual*/
void TensorRW::write_tensor(const string& q_name, const int entry_id, std::shared_ptr<tt_tensor> tensor) {
    if (!_out_path.empty()) {
        verif::write_tensor_to_binary(_out_path, q_name, entry_id, *tensor);
        return;
    }

    print_tensor(q_name, entry_id, tensor);
}