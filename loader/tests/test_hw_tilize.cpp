// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <chrono>
#include <ctime>

#include "runtime.hpp"
#include "size_lib.hpp"
#include "verif.hpp"

using namespace verif::comparison;
using namespace verif::random;

int tile_header_size = 16;
int exponent_section_size = 64;
int tile_width = 16;
int tile_height = 16;
int num_datums_each_face = tile_width * tile_height;

enum class RandomType {
    None = 0,
    EntireValueNormal = 1,
    EntireValueUniform = 2,
    EntireValueNormalLargeSTD = 3,
    EachPortionNormal = 4,
    Diagonal = 5,
    EntireValueNormalBiasedPos = 6,
    EntireValueNormalBiasedNeg = 7,
    EntireValueNormalSmallVals = 8,
};

const map<std::string, RandomType> arg_to_rand_type {
    {"none", RandomType::None},
    {"normal", RandomType::EntireValueNormal},
    {"normal-large-stddev", RandomType::EntireValueNormalLargeSTD},
    {"normal-bias-pos", RandomType::EntireValueNormalBiasedPos},
    {"normal-bias-neg", RandomType::EntireValueNormalBiasedNeg},
    {"diagonal", RandomType::Diagonal},
    {"uniform", RandomType::EntireValueUniform},
    {"normal-each-portion", RandomType::EachPortionNormal},
    {"normal-small-values", RandomType::EntireValueNormalSmallVals},
};

RandomType get_random_config (string rand_arg) {
    log_assert(arg_to_rand_type.find(rand_arg) != arg_to_rand_type.end(), "Could not find arg {}", rand_arg);
    return arg_to_rand_type.at(rand_arg);
}

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, DataFormat host_format, RandomType random_type, bool init_to_zero) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;

    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    float base_value = (entry_idx%2) ? -16.0f : 16.0f;
    float step_size  = (entry_idx%2) ? -1.0f : 1.0f;

    tt_tensor tensor(md);
    
    if (init_to_zero) {
        tensor.init_to_input_value(0.0);
    } else if (random_type == RandomType::EachPortionNormal) {
        int spread = 127;
        int man_variance_bits = 23;
        tensor.randomize_manual_float(spread, man_variance_bits);
    } else if (random_type == RandomType::EntireValueNormal) {
        float mean = 0.0;
        float stddev = 0.25;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalLargeSTD) {
        float mean = 0.0;
        float stddev = 1000000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueUniform) {
        float lower_bound = -1000000.0;
        float upper_bound = 1000000.0;
        tensor.randomize_uniform(lower_bound, upper_bound);
    } else if (random_type == RandomType::Diagonal) {
        float mean = 0.0;
        float stddev = 1000000.0;
        tensor.randomize_diagonal(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalBiasedPos) {
        float mean = 10000.0;
        float stddev = 10000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalBiasedNeg) {
        float mean = -10000.0;
        float stddev = 10000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::None) {
        tensor.init_to_tile_id(1.0f, 0);
    } else if (random_type == RandomType::EntireValueNormalSmallVals) {
        float mean = 0.0;
        float stddev = 0.001;
        tensor.randomize(mean, stddev);
    } else {
        log_assert(false, "Invalid random type.");
    }
    tensor.pack_data();
    tensor.unpack_data();
    return tensor;
}
std::vector<tt_tensor> pop_from_queue(tt_runtime_workload &workload, tt_dram_io_desc &pop_desc, tt_cluster *cluster)
{
    std::vector<tt_tensor> queue_contents;
    tt_queue_info &queue_info = workload.queues[pop_desc.queue_name].my_queue_info;
    while (!tt::io::is_queue_empty(pop_desc, queue_info, cluster)) {
        tt_tensor observed = tt::io::pop_queue_tilized_output(queue_info, cluster, true, 1 /*pop_count*/, Dim::R /*ublock_scan*/, 0 /*timeout_in_seconds*/); //Unpack done here
        //observed.unpack_data();
        queue_contents.push_back(observed);
    }
    log_assert(!queue_contents.empty(), "Queue was empty.");
    return queue_contents;
}
// HW Tilizer has locally cached pointers, and also is constructed with wrptr=0/rdptr=0 which is not accurate for device
// if sw-tilizer was used first. To workaround these issues, re-allocate io queues and destroy hw tilizer from object cache.
void reallocate_io_queues_and_destroy_hw_tilizer(tt_runtime_config &config, tt_runtime &runtime, tt_runtime_workload &workload){
    runtime.loader->create_and_allocate_io_queues(workload.queues);
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::destroy();
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::destroy();
}
// Ensure HW Tilizer was used in tests that intended it to be used, when using automatic enablement of hw-tilize.
void ensure_hw_tilize_used(tt_runtime_config &config, vector<tt_dram_io_desc> &input_io_desc){
    for (const auto& desc: input_io_desc) {
        bool hw_tilizer_exists = tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::exists(desc.queue_name) or tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(desc.queue_name);
        log_assert(hw_tilizer_exists, "HW Tilizer was expected to be used in this test but was not, constraints violated?");
    }
}

struct tilized_data_info {
    bool is_queue_header;
    int input_idx;
    int ublock_r;
    int ublock_c;
    int mblock_m;
    int nblock_n;
    int t;
    int core_r;
    int core_c;
    int tensor_row_idx;
    int tensor_col_idx;
    tt_tile input_tile;
    tt_tile output_tile;
    vector<uint8_t> bytes;
    DataFormat device_data_format;

    tilized_data_info(bool is_q_header, int input_idx, int ublock_r, int ublock_c,
                    int mblock_m, int nblock_n, int t, int core_r, int core_c, int tensor_row_idx, int tensor_col_idx,
                    tt_tile input_tile, tt_tile output_tile, vector<uint8_t> bytes, DataFormat device_data_format):
                        is_queue_header(is_q_header), input_idx(input_idx), ublock_r(ublock_r), ublock_c(ublock_c),
                        mblock_m(mblock_m), nblock_n(nblock_n), t(t), core_r(core_r), core_c(core_c),
                        tensor_row_idx(tensor_row_idx), tensor_col_idx(tensor_col_idx), input_tile(input_tile),
                        output_tile(output_tile), bytes(bytes), device_data_format(device_data_format) {}
    
    tilized_data_info(bool is_q_header, vector<uint8_t> bytes, int core_r, int core_c, tt_tile input_tile, tt_tile output_tile, DataFormat device_data_format):
                    is_queue_header(is_q_header), bytes(bytes), core_r(core_r), core_c(core_c), input_tile(input_tile), output_tile(output_tile), device_data_format(device_data_format) {}
    string get_desc() {
        string desc = "";
        if (is_queue_header) {
            desc += "queue header for core (" + to_string(core_r) + ", " + to_string(core_c) + ")";
        } else {
            desc += "tile for core (" + to_string(core_r) + ", " + to_string(core_c) + ") input_idx "
                + to_string(input_idx) + " t = " + to_string(t)
                + " mblock-idx (" + to_string(mblock_m) + ", " + to_string(nblock_n) + ") ublock-idx ("
                + to_string(ublock_r) + ", " + to_string(ublock_c) + ")";
        }
        return desc;
    }
    void dump_bytes_to_file(string output_file_path, bool append) {
        std::ofstream output_file;
        if (append) {
            output_file.open(output_file_path, std::ios_base::app);
        } else {
            output_file.open(output_file_path);
        }
        string desc = get_desc();
        output_file << desc << "\n";
        if (is_queue_header) {
            output_file << "Queue Header:" << endl;
            for (auto byte: bytes) {
                output_file << uint32_t(byte) << endl;
            }
        } else {
            output_file << "Tile Header:" << endl;
            for (int i = 0; i < bytes.size(); i++) {
                if (device_data_format == DataFormat::Bfp8 || device_data_format == DataFormat::Bfp8_b) {
                    if (i == tile_header_size) {
                        output_file << "Exponents: " << endl;
                    }
                    if (i == tile_header_size + exponent_section_size) {
                        output_file << "First face:" << endl;
                    }
                    if (i == tile_header_size + exponent_section_size + num_datums_each_face) {
                        output_file << "Second face:" << endl;
                    }
                    if (i == tile_header_size + exponent_section_size + 2*num_datums_each_face) {
                        output_file << "Third face:" << endl;
                    }
                    if (i == tile_header_size + exponent_section_size + 3*num_datums_each_face) {
                        output_file << "Fourth face:" << endl;
                    }
                } else if (device_data_format == DataFormat::Float16 || device_data_format == DataFormat::Float16_b || device_data_format == DataFormat::RawUInt16 || device_data_format == DataFormat::RawUInt32 || device_data_format == DataFormat::Float32) {
                    if (i == tile_header_size) {
                        output_file << "First face:" << endl;
                    }
                    if (i == 2*num_datums_each_face + tile_header_size) {
                        output_file << "Second face:" << endl;
                    }
                    if (i == 2*2*num_datums_each_face + tile_header_size) {
                        output_file << "Third face:" << endl;
                    }
                    if (i == 2*3*num_datums_each_face + tile_header_size) {
                        output_file << "Fourth face:" << endl;
                    }
                } else {
                    log_assert(false, "Invalid data-format");
                }
                output_file << (uint32_t)(bytes.at(i)) << endl;
            }
        }
    }
    void dump_tile_or_header() {
        if (is_queue_header) {
            cout << "queue header: " << endl;
            for (auto byte: bytes) {
                cout << (uint32_t)byte << " ";
            }
            cout << endl;
        } else {
            cout << "tile: " << endl;
            output_tile.dump();
        }
    }
    void print_loc_in_tensor_for_byte(int byte) {
        cout << byte << endl;
        if (is_queue_header) {
            log_info(tt::LogTest, "Inside queue header");
        } else {
            int shared_exponent_num_bytes = 0;
            int num_bytes_datum = 2;
            if (device_data_format == DataFormat::Bfp8 || device_data_format == DataFormat::Bfp8_b) {
                shared_exponent_num_bytes = exponent_section_size;
                num_bytes_datum = 1;
            }
            if (byte < tile_header_size) {
                log_info(tt::LogTest, "Inside tile header");
            } if (byte < tile_header_size + shared_exponent_num_bytes) {
                log_info(tt::LogTest, "In exponent section byte {}", byte - tile_header_size);
            } else {
                int face_id = (byte - (tile_header_size + shared_exponent_num_bytes)) / (num_datums_each_face*num_bytes_datum);
                int row_id = ((byte - (tile_header_size + shared_exponent_num_bytes)) - (num_datums_each_face*num_bytes_datum) * face_id) / (tile_width*num_bytes_datum);
                int col_id = ((byte - (tile_header_size + shared_exponent_num_bytes)) - (num_datums_each_face*num_bytes_datum) * face_id) % (tile_width*num_bytes_datum);
                int datum_id = col_id / num_bytes_datum;
                log_info(tt::LogTest, "Byte index {} which is in face-id {}, row {}, col {}", byte, face_id, row_id, col_id);
            }

        }
    }
};

vector<tilized_data_info> get_tilized_data_info(vector<tt_dram_io_desc> &input_io_desc, tt_runtime_workload &workload, std::vector<tt_tensor> &input_tensors, std::vector<tt_tensor> popped_tensors) {
    
    vector<tilized_data_info> tilized_info;
    int queue_header_size = tt::io::io_queue_header_size_bytes;
    log_assert(input_tensors.size() == popped_tensors.size(), "Invalid number og tensors popped");
    log_assert(input_tensors.size() > 0, "Invalid number of input tensors");
    log_assert(input_io_desc.size() == 1, "Should only have one input queue");
    
    for (tt_dram_io_desc &io : input_io_desc) {
        int num_cores = io.bufq_mapping.size();
        tt_queue_info &queue_info   = workload.queues.at(io.queue_name).my_queue_info;
        auto device_data_format     = queue_info.data_format;
        int num_inputs              = queue_info.entries;
        int bufq_index = 0;
        for (auto bufq_addr: io.bufq_mapping) {
            uint8_t* output_addr = reinterpret_cast<uint8_t*>(io.bufq_mapping.at(bufq_index));
            int addr_offset = 0;
            int gr = bufq_index / queue_info.grid_size.c;
            int gc = bufq_index % queue_info.grid_size.c;
            vector<uint8_t> header_bytes;
            for (int byte = 0; byte < queue_header_size; byte++) {
                
                header_bytes.push_back(*(output_addr + addr_offset));
                addr_offset++;
            }
            tilized_info.push_back(tilized_data_info(true, header_bytes, gr, gc, tt_tile(input_tensors.at(0).get_data_format()), tt_tile(device_data_format), device_data_format));

            for (int input_idx = 0; input_idx < num_inputs; input_idx++) {
                tt_tensor all_tensors_input = input_tensors.at(input_idx);
                tt_tensor output_tensors_input = popped_tensors.at(input_idx);
                for(int zi=0; zi<all_tensors_input.getz(); ++zi) {
                    for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                        for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                            for (int tr=0; tr<queue_info.dim.ublock_rt; ++tr) {
                                for (int tc=0; tc<queue_info.dim.ublock_ct; ++tc) {
                                    int rt_index = gr*queue_info.dim.ublock_rt*queue_info.dim.mblock_m + ubr*queue_info.dim.ublock_rt + tr;
                                    int ct_index = gc*queue_info.dim.ublock_ct*queue_info.dim.mblock_n + ubc*queue_info.dim.ublock_ct + tc;
                                    tt_tile single_tile = all_tensors_input.tile_tensor.at(0).at(zi).at(rt_index).at(ct_index);
                                    tt_tile output_tile = output_tensors_input.tile_tensor.at(0).at(zi).at(rt_index).at(ct_index);
                                    single_tile.data_format = all_tensors_input.get_data_format();
                                    output_tile.data_format = device_data_format;
                                    vector<uint8_t> data_bytes;
                                    for (int byte = 0; byte < tt::size::get_tile_size_in_bytes(device_data_format, true); byte++) {
                                        data_bytes.push_back(*(output_addr + addr_offset));
                                        addr_offset++;
                                    }
                                    tilized_info.push_back(tilized_data_info(false, input_idx, tr, tc, ubr, ubc, zi, gr, gc, rt_index, ct_index, single_tile, output_tile, data_bytes, device_data_format));
                                }
                            }
                        }
                    }
                }
            }
            bufq_index++;
        }
    }
    return tilized_info;
}

void print_sw_and_hw_to_file(vector<tilized_data_info> all_sw_info, vector<tilized_data_info> all_hw_info) {
    log_assert(all_sw_info.size() == all_hw_info.size(), "SW Tilize HW tilize mismatch");
    string sw_tilize_file_path = "sw_tilize.txt";
    string hw_tilize_file_path = "hw_tilize.txt";
    std::ofstream sw_tilize_file(sw_tilize_file_path);
    std::ofstream hw_tilize_file(hw_tilize_file_path);

    for (int i = 0; i < all_sw_info.size(); i++) {
        tilized_data_info sw_info = all_sw_info.at(i);
        tilized_data_info hw_info = all_hw_info.at(i);
        sw_info.dump_bytes_to_file(sw_tilize_file_path, true);
        hw_info.dump_bytes_to_file(hw_tilize_file_path, true);
    }
}

bool compare_sw_and_hw(vector<tilized_data_info> all_sw_info, vector<tilized_data_info> all_hw_info) {
    log_assert(all_sw_info.size() == all_hw_info.size(), "SW Tilize HW tilize mismatch");
    bool all_passed = true;
    for (int i = 0; i < all_sw_info.size(); i++) {
        tilized_data_info sw_info = all_sw_info.at(i);
        tilized_data_info hw_info = all_hw_info.at(i);
        log_assert(sw_info.bytes.size() == hw_info.bytes.size(), "SW Tilize HW tilize mismatch");
        for (int byte = 0; byte < sw_info.bytes.size(); byte++) {
            if (sw_info.bytes.at(byte) != hw_info.bytes.at(byte)) {
                log_error("hw and sw tilize mismatch");
                log_error("for sw block with description: {}", sw_info.get_desc());
                log_error("for hw block with description: {}", hw_info.get_desc());
                tt_shape single_tile_shape;
                single_tile_shape.ct = 1;
                single_tile_shape.rt = 1;
                single_tile_shape.z = 1;
                single_tile_shape.w = 1;
                tt_tensor input_tensor(single_tile_shape, {sw_info.input_tile});
                tt_tensor sw_tensor(single_tile_shape, {sw_info.output_tile});
                tt_tensor hw_tensor(single_tile_shape, {hw_info.output_tile});
                VerifComparisonConfig comparison_config_exact_match;
                comparison_config_exact_match.user_configured = true;
                comparison_config_exact_match.type = ComparisonType::Exact;
                comparison_config_exact_match.verbosity = ComparisonVerbosity::AllFails;
                sw_info.print_loc_in_tensor_for_byte(byte);
                compare_tensors(sw_tensor, hw_tensor, comparison_config_exact_match);
                if (!hw_info.is_queue_header) {
                    log_info(tt::LogTest, "Add following options to command to recreate failure with all tensors initialized to the failing tensor in this test:");
                    log_info(tt::LogTest, "--recreate-failure --fail-entry {} --fail-z {} --fail-r {} --fail-c {}", hw_info.input_idx, hw_info.t, hw_info.tensor_row_idx, hw_info.tensor_col_idx);
                }
                all_passed = false;
                log_assert(false, "SW and HW Tilize mismatch");
            }
        }
    }
    return all_passed;
}

void push_to_hw_and_sw_tilize_pop_and_compare(tt_runtime_config &config, tt_runtime &runtime, tt_runtime_workload &workload,
    vector<tt_dram_io_desc> &input_io_desc, std::vector<tt_tensor> &input_tensors, std::vector<tt_tensor> &all_zero_tensors)
{
    // Step 1: SW (Slow) Tilize and Push
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for SW Tilize with entry_idx: {}", entry_idx);
        return input_tensors.at(entry_idx);
    }, -1, true); // force_sw_tilize=true

    // Records each byte for sw and hw tilizer
    std::vector<tt_tensor> sw_tilize_input_queue_tensors = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());
    vector<tilized_data_info> sw_data = get_tilized_data_info(input_io_desc, workload, input_tensors, sw_tilize_input_queue_tensors);

    reallocate_io_queues_and_destroy_hw_tilizer(config, runtime, workload);

    // Push all zero tensors to reset the values in dram before trying hw_tilize
    // This will ensure the values read from hw-tilize pop are not sw-tilize numbers
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for SW Tilize and reset values with entry_idx: {}", entry_idx);
        return all_zero_tensors.at(entry_idx);
    }, -1, true); // force_sw_tilize=true
    std::vector<tt_tensor> sw_tilize_temp = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());
    reallocate_io_queues_and_destroy_hw_tilizer(config, runtime, workload);

    // Step 2: HW (Fast) Tilize and Push
    // tt::io::push_host_inputs_with_hw_tilize(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for HW Tilize with entry_idx: {}", entry_idx);
        return input_tensors.at(entry_idx);
    }, -1);

    // Make sure HW Tilizer was actually used and yaml doesn't violate constraints.
    ensure_hw_tilize_used(config, input_io_desc);
    std::vector<tt_tensor> hw_tilize_input_queue_tensors = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());    
    vector<tilized_data_info> hw_data = get_tilized_data_info(input_io_desc, workload, input_tensors, hw_tilize_input_queue_tensors);
    //print_sw_and_hw_to_file(sw_data, hw_data);
    
    bool all_passed = compare_sw_and_hw(sw_data, hw_data);
    if (all_passed) {
        log_info(tt::LogTest, "All bytes between hw and sw tilizers match");
    }
    log_assert(all_passed, "HW-SW tilizer results mismatch");
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    int opt_level;
    bool run_silicon;
    std::string netlist_path;
    std::string host_format_;
    int seed;
    std::string random_type_str;
    std::string arch_name;
    bool recreate_failure = false;
    int fail_entry = -1;
    int fail_z = -1;
    int fail_r = -1;
    int fail_c = -1;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 2);
    std::tie(run_silicon, args) = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    std::tie(host_format_, args) = verif_args::get_command_option_and_remaining_args(args, "--host-data-format", "Float32");
    std::tie(seed, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", -1);
    std::tie(random_type_str, args) = verif_args::get_command_option_and_remaining_args(args, "--randomize-tensor", "none");
    DataFormat host_format   = verif::STRING_TO_DATA_FORMAT.at(host_format_); //THIS IS ONLY IN tt:args. Need tio port to verif_args
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");
    
    std::tie(recreate_failure, args) = verif_args::has_command_option_and_remaining_args(args, "--recreate-failure");
    std::tie(fail_entry, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--fail-entry", -1);
    std::tie(fail_z, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--fail-z", -1);
    std::tie(fail_r, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--fail-r", -1);
    std::tie(fail_c, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--fail-c", -1);

    RandomType random_type = get_random_config(random_type_str);

    if (recreate_failure) {
        log_assert(fail_entry != -1 && fail_z != -1 && fail_r != -1 && fail_c != -1, "Failed");
    }

    perf::PerfDesc perf_desc(args, netlist_path);
    VerifComparisonConfig comparison_config = verif::comparison::read_from_yaml_file(netlist_path);
    // Check to see if test-config is defined in netlist
    if (random_type != RandomType::None) {
        if (seed == -1) {
            seed = uint(std::time(NULL));
        }
        log_info(tt::LogTest, "Generating random tensor with seed {}", seed);
        tt_rnd_set_seed(seed);
    }

    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized successfully");

    // Generate input tensors up front - needed, to use wrapper API for hw/sw tilize when it inspects data format.
    std::vector<tt_tensor> input_tensors;
    std::vector<tt_tensor> all_zero_tensors;
    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    for (const auto& desc: input_io_desc) {
        tt_queue_info &queue_info = workload.queues[desc.queue_name].my_queue_info;
        for (int i=0; i<queue_info.entries; i++) {
            input_tensors.push_back(get_tilized_tensor(queue_info, i, host_format, random_type, false));
            all_zero_tensors.push_back(get_tilized_tensor(queue_info, i, host_format, random_type, true));
        }
    }
    if (recreate_failure) {
        tt_tile failed_tile = input_tensors.at(fail_entry).tile_tensor.at(0).at(fail_z).at(fail_r).at(fail_c);
        int num_tiles_in_tensor = input_tensors.at(fail_entry).getw() * input_tensors.at(fail_entry).getz() *
                                input_tensors.at(fail_entry).getrt() * input_tensors.at(fail_entry).getct();
        int num_entries = input_tensors.size();
        tt_tensor failed_tensor_broadcast(input_tensors.at(fail_entry).get_shape(), vector<tt_tile>(num_tiles_in_tensor, failed_tile));
        failed_tensor_broadcast.set_data_format(input_tensors.at(fail_entry).get_data_format());
        tt_tensor_metadata md = input_tensors.at(fail_entry).metadata;
        failed_tensor_broadcast.metadata = md;
        input_tensors.clear();
        input_tensors = vector<tt_tensor>(num_entries, failed_tensor_broadcast);
    }

    push_to_hw_and_sw_tilize_pop_and_compare(config, runtime, workload, input_io_desc, input_tensors, all_zero_tensors);

    runtime.finish();
    return 0;
}