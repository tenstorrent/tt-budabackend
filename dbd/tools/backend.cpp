#include "backend.hpp"
#include "verif.hpp"

class VerifBackend : public IBackend {
    public:
        VerifBackend(std::shared_ptr<tt_backend> backend) 
        : _backend(backend)
        , _initialized(false) {}

        virtual ~VerifBackend() {
            if (_initialized) {
                log_assert(_backend->finish() == tt::DEVICE_STATUS_CODE::Success);
            }
            log_info(tt::LogTest, "Backend teardown finished");
        }

        void initialize() {
            log_assert(_backend->initialize() == tt::DEVICE_STATUS_CODE::Success);
            _initialized = true;
        }

        void push_tensor(const string& q_name, std::shared_ptr<tt_tensor> tensor) {
            verif::push_tensor(*_backend, q_name, tensor.get(), 0);
        }

        void run_program(const string& program_name, const ProgramParameters& parameters){
            _backend->run_program(program_name, parameters);
        }

        void wait_for_idle() {
            _backend->wait_for_idle();
        }

        tt_tensor_metadata get_metadata(const string& q_name) {
            tt_dram_io_desc q_desc = _backend->get_queue_descriptor(q_name);
            return get_tensor_metadata_for_queue(q_desc);
        }

        std::shared_ptr<tt_tensor> pop_tensor(const string& q_name) {
            std::shared_ptr<tt_tensor> tensor = std::make_shared<tt_tensor>(get_metadata(q_name));
            verif::get_and_pop_tensor(*_backend, q_name, tensor.get());

            return tensor;
        }

    private:
        std::shared_ptr<tt_backend> _backend;
        bool _initialized;
};

std::shared_ptr<tt_backend> create_backend(const string& netlist_path, tt_backend_config config) {
    std::shared_ptr<tt_backend> backend = tt_backend::create(netlist_path, config);
    return backend;
}

tt_backend_config get_backend_config(tt::DEVICE backend_type) {
    tt_backend_config config;
    config.type = backend_type;
    return config;
}

/*static*/
std::shared_ptr<IBackend> BackendFactory::create(const string& netlist_path, bool silicon) {
    if (silicon)
        return create_silicon(netlist_path);
    else
        return create_golden(netlist_path);
}

/*static*/
std::shared_ptr<IBackend> BackendFactory::create_golden(const string& netlist_path) {
    return std::make_shared<VerifBackend>(create_backend(netlist_path, get_backend_config(tt::DEVICE::Golden)));
}

/*static*/
std::shared_ptr<IBackend> BackendFactory::create_silicon(const string& netlist_path) {
    return std::make_shared<VerifBackend>(create_backend(netlist_path, get_backend_config(tt::DEVICE::Silicon)));
}

/*static*/
std::shared_ptr<IBackend> BackendFactory::create_silicon_debug(const std::string& netlist) {
    tt_backend_config config = get_backend_config(tt::DEVICE::Silicon);
    config.mode = DEVICE_MODE::RunOnly;
    return std::make_shared<VerifBackend>(create_backend(netlist, config));
}