// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "llk_tensor_ops.h"
#include "llk_types.h"
#include "test.h"
#include "tests_lib.h"
#include "utils/math.h"

using namespace std;
using namespace llk;
using namespace tests;
using namespace tests::test_config_api;

// Customization for architecture
#define NARCHES 2
static int arch_enumerant_g;
static int arch_data_shift_g;
static int arch_data_stride_g;
static int arch_test1_result_g;

union FC {
    const float f;
    const uint32_t i;

    FC(int in) : i(in) {}
    explicit FC(float in) : f(in) {}
    const float cf() { return f; }
    const uint32_t ci() { return i; }
};

static const vector<float> test1_results;

static const vector<float> test2_results;

static const vector<float> test3_results[NARCHES] = {
    {
        // Grayskull
        10.0, 20.0, 30.0, FC(0x403f0000).cf(),              // 0
        FC(0x403f0000).cf(), -INFINITY, 120.0f, -INFINITY,  // 4
        25.0f, 28.0f, 20.0f, 20.0f,                         // 8
        20.0f, 20.0f, 20.0f, 20.0f,                         // 12
        20.0f, 20.0f, 20.0f, 20.0f,                         // 16
        20.0f, 20.0f, 20.0f, 20.0f,                         // 20
        20.0f, 20.0f, 20.0f, 20.0f,                         // 24
        20.0f, 20.0f, 20.0f, 20.0f,                         // 28
        20.0f, 20.0f, 20.0f, 20.0f,                         // 32
        20.0f, 20.0f, 20.0f, 20.0f,                         // 36
        20.0f, 20.0f, 20.0f, 20.0f,                         // 40
        20.0f, 20.0f, 20.0f, 20.0f,                         // 44
        20.0f, 20.0f, 20.0f, 20.0f,                         // 48
        20.0f, 20.0f, 20.0f, 20.0f,                         // 52
        20.0f, 20.0f, 20.0f, 20.0f,                         // 56
        20.0f, 20.0f, FC(0x3b000000).cf() ,20.0f,           // 60
    },
    {
        // Wormhole
        10.0f, 20.0f, 30.0f, 1.005f,
        1.005f,FC(0x3f803f80).cf(), 1.005f, FC(0x3f808f80).cf(), // 4
        FC(0x3f808f80).cf(), 1.005f, FC(0x3f803f80).cf(), FC((int)0xffff8f80).cf(), // 8
        1.005f,FC(0x3ba30000).cf(), FC(0x3f80a000).cf(), 25.0f, // 12
        28.0f,0.8373f, 1.9921875f, 1.99609375f,             // 16
        130560.0f, 130592.0f, 65408.0f, 130816.0f,          // 20
        0.000121831894f, 0.000060915947f, 20.0f, 20.0f,     // 24
        20.0f, 20.0f, 20.0f, 20.0f,                         // 28
        20.0f, 20.0f, 20.0f, 20.0f,                         // 32
        20.0f, 20.0f, 20.0f, 20.0f,                         // 36
        20.0f, 20.0f, 20.0f, 20.0f,                         // 40
        20.0f, 20.0f, 20.0f, 20.0f,                         // 44
        20.0f, 20.0f, 20.0f, 20.0f,                         // 48
        20.0f, 20.0f, 20.0f, 20.0f,                         // 52
        20.0f, 20.0f, 20.0f, 20.0f,                         // 56
        20.0f, 20.0f, FC(0x3b000000).cf() ,20.0f,           // 60
    }
};

static const vector<float> test4_results = {
     64.0,  64.0,  66.0,  65.0, // 0
     65.0,  67.0,  68.0,  69.0, // 4
     70.0,  71.0,  72.0, -11.0, // 8
    120.0, 120.0, 120.0, -15.0, // 12
    120.0, 120.0, 180.0, 160.0, // 16
    180.0, 160.0, 180.0, 200.0, // 20
    220.0, 240.0, 270.0, 260.0, // 24
    270.0, 290.0, 340.0, 320.0, // 28
};

static const vector<float> test5_results[NARCHES] = {
    {
        // XXXX Note: result 1 is unexpected, should be 0 but is instead close to 0
        FC(0x3f310000).cf(), FC(0x00c00000).cf(), -1.0,      1.4375, // 0
        FC(0x3f560000).cf(),-0.5,         1.0,        -1.0, // 4
        FC(0x3b000000).cf(),-0.671875,   -0.343750,   -0.765625, // 8
        240.0,              33.0,       280.0,        35.5, // 12
        318.0,              36.5,       354.0,       374.0, // 16
        394.0,             414.0,       444.0,       458.0, // 20
        -21.0,             494.0,       512.0,       564.0, // 24
        532.0,               9.0,         9.5,        11.5, // 28
    },
    {
        FC(0x3f56594b).cf(), 0.0,        -1.0,         0.5, // 0
          1.5,              -1.5,         1.0,         0.0, // 4 // XXXX 
          0.0,               0.0,         0.0,         0.1627, // 8
        240.0,              33.0,       280.5,        35.5, // 12
        319.5,              36.5,       355.0,       375.5, // 16
        394.5,             414.5,       445.0,       461.0, // 20
         21.0,             495.0,       515.0,       567.0, // 24
        532.0,               9.0,         9.5,        11.5, // 28
    }
};

static const vector<float> test6_results = {
     256.0,    1024.0,    1024.0,    1024.0, // 0
     256.0,     256.0,     512.0,     256.0, // 4
    1024.0,     512.0,     256.0,    1024.0, // 8
       4.0,       8.0,      16.0,      32.0, // 12
       4.0,       8.0,      16.0,      32.0, // 16
       4.0,       8.0,      16.0,      32.0, // 20
      64.0,     128.0,     256.0,     512.0, // 24
     256.0,     256.0,     256.0,      16.0  // 28
};

static const vector<float> test7_results[NARCHES] = {
    {
        64.0,    30.0,       32.0,      33.0, // 0
        34.0,    32.0,       64.0,      35.0, // 4
      1024.0,    -1.625,   1960.0,    1024.0, // 8
      -128.0,  -128.0,      -14.0,    -128.0, // 12
      -128.0,  -256.0,      -18.0,    -256.0, // 16
       -20.0,  -256.0,      -22.0,    -256.0, // 20
       -24.0,   -25.0,    -1024.0,     -27.0, // 24
     -1024.0,  1712.0,      176.0,    1872.0, // 28
    },
    {
        64.0,    30.0,       32.0,      33.0, // 0
        34.0,    32.0,       64.0,      35.0, // 4
      1024.0,    -1.625,   1960.050049,1960.050049, // 8
      -128.0,  -128.0,      -14.0,    -128.0, // 12
      -128.0,  -256.0,      -18.0,    -256.0, // 16
       -20.0,  -256.0,      -22.0,    -256.0, // 20
       -24.0,   -25.0,    -1024.0,     -27.0, // 24
     -1024.0,  1712.0,      176.0,    1872.0, // 28
    }
};

static const vector<float> test8_results = {
     0.0,    16.0,    16.0,     16.0, // 0
    16.0,    20.0,    20.0,     20.0, // 4
    20.0,    22.0,   100.0,    100.0, // 8
    24.0,    26.0,    32.0,     16.0, // 12
    48.0,    31.0,    14.0,     -3.0, // 16
  1712.0,   176.0,  1872.0,   1712.0, // 20
   176.0,  1872.0,  1712.0,    176.0, // 24
  1872.0,    32.0,    64.0,     32.0  // 28
};

static const vector<float> test9_results = {
     0.0,   600.0,    800.0,    50.0, // 0
    60.0,   256.0,     16.0,    38.0, // 4
    55.0,    30.0,     60.0,    40.0, // 8
    60.0,    -6.0,     -6.0,   -15.0, // 12
    32.0,   -17.0,     32.0,   -19.0, // 16
    32.0,   -21.0,     32.0,   -23.0, // 20
    64.0,   -25.0,     64.0,   -27.0, // 24
    64.0,                             // 28
};

static const vector<float> test10_results = {
     0.0,     20.0,     22.0,    24.0,  // 0
    26.0,     28.0,     30.0,    -7.0,  // 4
   128.0,    256.0,    -30.0,           // 8
};

static const vector<float> test11_results[NARCHES] = {
    {
         0.0,      0.125,    -0.125,    -0.375, // 0
        -0.625,    0.875,    -0.5,       0.25,  // 4
        -0.25,    -0.25,     -0.75,       .75,  // 8
        -1.0,                                   // 12
    },
    {
         0.0,      0.125,    -0.125,     0.125, // 0
        -0.125,    0.375,    -0.5,       0.25,  // 4
        -0.25,     0.25,     -0.25,       .75,  // 8
        -1.0,     -3.5,      10.0,     -20.5,   // 12
         3.5,     -3.5,      10.0,     -25.0,   // 16
         3.5,     -3.5,       8.0,     -14.5,   // 20
       -23.0,     36.0,      55.0,       3.5,   // 24
       -23.0,     46.0,      67.0,       3.5,   // 28
    }
};

static const vector<float> test12_results[NARCHES] = {
    {
          0.0,     35.0,     -35.0,     25.0, // 0
        -25.0,     38.0,     -32.0,     64.0, // 4
        128.0,   4096.0,      16.0,   -128.0, // 8
       1976.0,  // Grayskull bug, otherwise 1024.12
                   -1.625,    30.0,     -1.0,
        111.0,    121.0,    131.0,     141.0, // 16
        105.0,    -105.0,                     // 20
    },
    {
          0.0,     35.0,     -35.0,     25.0, // 0
        -25.0,     38.0,     -32.0,     64.0, // 4
        128.0,   4096.0,      16.0,   -128.0, // 8
       1960.050049,-1.625,    30.0,     -1.0, // 12
        121.0,    131.0,     141.0,    105.0, // 16
       -105.0,                                // 20
    },
};

static const vector<float> test13_results[NARCHES] = {
    {
        20.0,    -30.0,     40.0,     50.0, // 0
        60.0,     70.0,   8096.0,    110.0, // 4
      -120.0,    130.0,    280.0,    150.0, // 8
       128.0,      2.0,    160.0,    144.0, // 12
        42.5,    180.0,    245.0,    200.0, // 16
      -210.0,    220.0,    280.0,    150.0, // 20
       -35.0,    240.0,                     // 24
    },
    {
        20.0,    -30.0,     40.0,     50.0, // 0
        60.0,     70.0,   8110.0,    110.0, // 4
      -120.0,    130.0,    280.0,    150.0, // 8
       128.0,      2.0,    160.0,    136.0, // 12
        42.5,    180.0,   245.006256,200.0, // 16
      -210.0,    220.0,    280.0,    150.0, // 20
       -35.0,    240.0,                     // 24
    },
};

static const vector<float> test14_results = {
       -250.0,       160.0,    -250.0,    260.0, // 0
       -270.0,       280.0,    -290.0,    300.0, // 4
       -310.0,       320.0,     -20.0,     30.0, // 8
        -40.0,        50.0,     -59.0,     71.0, // 12
        -80.0,        90.0,     -90.0,    100.0, // 16
       -100.0,       110.0,    -110.0,    120.0, // 20
       -120.0,       130.0,     -90.0,    110.0, // 24
  114819072.0,  21233664.0,                      // 28
};

// Tests below are wormhole only
static const vector<float> test15_results = {
         8.0,          8.0,       8.0,      8.0, // 0
         8.0,          8.0,       8.0,      8.0, // 4
        16.0,         16.0,      16.0,     16.0, // 8
        16.0,         16.0,      16.0,     16.0, // 12
        24.0,         24.0,      24.0,     24.0, // 16
        24.0,         24.0,      24.0,     24.0, // 20
};

static const vector<float> test16_results = {
          3.0,         2.0,       3.0,      2.0, // 0
          2.0,         3.0,       2.0,      3.0, // 4
          FC(0x4b2bbaab).cf(), FC(0x4b2bbaab).cf(), FC(0x3fa98000).cf(), FC(0x3faa0000).cf(), // 8
         48.0,        64.0,      80.0,     96.0, // 12
        112.0,       128.0,     130.0,    132.0, // 16
};

static const vector<float> test17_results = {
          0.0,        -1.0,      23.0,     -2.0, // 0
          1.0,        20.0,      20.0            // 4
};

static bool check_sfpi_result(Tensor &output, Tensor &expected_output) {
    bool ok = true;

    assert(output.tile_tensor.size() == 1);
    assert(output.tile_tensor[0].size() == 1);
    assert(output.tile_tensor[0][0].size() == 1);
    assert(output.tile_tensor[0][0][0].size() == 1);

    c_tile &out_tile = output.tile_tensor[0][0][0][0];
    c_tile &ex_tile = expected_output.tile_tensor[0][0][0][0];

#if 0
    // Print out all the output and expected output
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float actual = out_tile.value(x, y, 0, 0).f;
            cout << actual << " ";
        }
        cout << endl;
    }
        cout << endl;
        cout << endl;

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float actual = ex_tile.value(x, y, 0, 0).f;
            cout << actual << " ";
        }
        cout << endl;
    }
#endif

    int el = 0;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 32; x++) {
            int xc = x & 0xf;
            int yc = ((x & 0x10) >> 4) | (y << 1);
            float expected = ex_tile.value(xc, yc, 0, 0).f;
            float actual = out_tile.value(x, y, 0, 0).f;
            if (!(expected == 0 && actual == 0) && FC(expected).ci() != FC(actual).ci()) {
                ok = false;
                cout << setw(2) << dec << (el >> arch_data_shift_g);
                if (el != (el >> arch_data_shift_g << arch_data_shift_g)) {
                    cout << ".5";
                } else {
                    cout << "  ";
                }
                cout << " Expected:" << setw(8) << expected << " 0x" << hex << setw(8) << FC(expected).ci();
                cout << " Actual:" << setw(8) << actual << " 0x" << hex << setw(8) << FC(actual).ci();
                cout << endl;
            }

            el++;
        }
    }

    return ok;
}

static void make_default_vec(vector<float>& v, bool negate) {
    v.resize(1024);
    for (int i = 0; i < 64; i++) {
        if (i % arch_data_stride_g == 0) {
            v[i] = negate ? (-i >> arch_data_shift_g) : (i >> arch_data_shift_g);
        } else {
            v[i] = 0.0f;
        }
    }
    for (int i = 64; i < 1024; i++) {
        v[i] = 0.0f;
    }
}

static void overlay_results(vector<float>& out, const vector<float>& in) {
    for (int i = 0; i < in.size() * arch_data_stride_g; i++) {
        if (i % arch_data_stride_g == 0) {
            out[i] = in[i / arch_data_stride_g];
        }
    }
}

//! Generate expected tensors and input tensors from test_config
void tests::sfpi::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &expected_output0) {
    bool to_stdout = false;

    input0 = Tensor(
        test_config.tensor_configs["input0"].dims,
        test_config.tensor_configs["input0"].name,
        test_config.tensor_configs["input0"].data_format);

    expected_output0 = Tensor(
        test_config.tensor_configs["output0"].dims,
        "expected_output0",
        test_config.tensor_configs["output0"].data_format);

    // Stimulus generation
    vector<float> input0_data;
    vector<float> input1_data;

    // Output generation
    vector<float> output;
    make_default_vec(input0_data, false);
    if (test_config.extra_config["math-op"].compare("test1") == 0) {
        make_default_vec(output, false);
        for (int i = 0; i < 64; i++) {
            output[i] = (i % arch_data_stride_g == 0) ? FC(arch_test1_result_g).cf() : 0.0f;
        }
    } else if (test_config.extra_config["math-op"].compare("test2") == 0) {
        make_default_vec(output, true);
    } else if (test_config.extra_config["math-op"].compare("test3") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test3_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test4") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test4_results);
    } else if (test_config.extra_config["math-op"].compare("test5") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test5_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test6") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test6_results);
    } else if (test_config.extra_config["math-op"].compare("test7") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test7_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test8") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test8_results);
    } else if (test_config.extra_config["math-op"].compare("test9") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test9_results);
    } else if (test_config.extra_config["math-op"].compare("test10") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test10_results);
    } else if (test_config.extra_config["math-op"].compare("test11") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test11_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test12") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test12_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test13") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test13_results[arch_enumerant_g]);
    } else if (test_config.extra_config["math-op"].compare("test14") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test14_results);
    } else if (test_config.extra_config["math-op"].compare("test15") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test15_results);
    } else if (test_config.extra_config["math-op"].compare("test16") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test16_results);
    } else if (test_config.extra_config["math-op"].compare("test17") == 0) {
        make_default_vec(output, true);
        overlay_results(output, test17_results);
    }

    input0.populate_with_vector(input0_data);
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);

    expected_output0.populate_with_vector(output);
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::sfpi::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    bool fp32_dest_acc_en = false;
    if (config.extra_config.find("dest-acc") != config.extra_config.end() &&
        config.device != "grayskull") {
        fp32_dest_acc_en = (config.extra_config["dest-acc"] == "fp32_acc");
    }

    // Pull information from config.
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format, fp32_dest_acc_en);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format, fp32_dest_acc_en);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
    string output0_data_src_format = to_str(output0_packer_formats.first);
    string output0_data_dst_format = to_str(output0_packer_formats.second);

    string loop_count = "1";
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = config.extra_config["loop-count"];
    }
    string block_count = "1";
    if (config.extra_config.find("block-count") != config.extra_config.end()) {
        block_count = config.extra_config["block-count"];
    }
    string block_tile_dim = to_string(config.tensor_configs["input0"].dims.num_tiles() / stoul(block_count));
    if (config.extra_config.find("block-tile-dim") != config.extra_config.end()) {
        block_tile_dim = config.extra_config["block-tile-dim"];
    }
    if (config.extra_config.find("num-tiles-output0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "output0", atol(config.extra_config["num-tiles-output0-buf"].c_str()));
    }
    if (config.extra_config.find("num-tiles-input0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "input0", atol(config.extra_config["num-tiles-input0-buf"].c_str()));
    }
    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", "1"},
        {"__dst_tile_cols__", block_tile_dim},
        {"__block_cnt__", block_count},
        {"__arg_loop_count__", loop_count},
    };

    set_sfpu_params_from_extra_config(common_kernel_params, config.extra_config);
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

void customize_for_architecture(const TestConfig& config)
{
    if (config.device == "grayskull") {
        printf("ARCH grayskull\n");
        arch_enumerant_g = 0;
        arch_data_shift_g = 0;
        arch_data_stride_g = 1;
        arch_test1_result_g = 0x3fa60000;
    } else if (config.device == "wormhole") {
        printf("ARCH wormhole\n");
        arch_enumerant_g = 1;
        arch_data_shift_g = 1;
        arch_data_stride_g = 2;
        arch_test1_result_g = 0x3fa66666;
    } else if (config.device == "wormhole_b0") {
        printf("ARCH wormhole_b0\n");
        arch_enumerant_g = 1;
        arch_data_shift_g = 1;
        arch_data_stride_g = 2;
        arch_test1_result_g = 0x3fa66666;
    } else {
        throw "Unknown architecture";
    }
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::sfpi::generate_test_configs(
    TestArgs args, string test_name, YAML::Node &test_descriptor) {

    map<string, TestConfig> test_configs;
    // Build a default test_config from yaml specification. + provide user specified kernel_parameter mapping
    TestConfig config(args, test_name, test_descriptor);
    update_kernel_parameter_mapping(config);

    // Directed test specified in yaml is run
    test_configs.insert({"directed_yaml", config});

    return test_configs;
}

//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::sfpi::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "sfpi";
    Device device;
    string test_descriptor_file_path = args.test_descriptor;
    YAML::Node test_descriptor = YAML::LoadFile(test_descriptor_file_path);

    // Generate test configs to be run
    map<string, TestConfig> test_configs = generate_test_configs(args, test_name, test_descriptor);
    map<string, bool> test_results;
    bool result = true;
    for (auto const &it : test_configs) {
        // Run test configs in a loop or in separate threads if needed
        string config_string = it.first;
        TestConfig test_config = it.second;
        customize_for_architecture(test_config);
        std::string output_yaml_name = test_config_api::get_uniquified_output_folder(test_config) + "test.yaml";
        std::cout << "Dumping config yaml: " << output_yaml_name << std::endl;
        dump_config_as_yaml(test_config, output_yaml_name);
        cout << " -- RUNNING LLK_TEST::" << test_name << "::" << config_string << endl;
        // Tensor allocation and generation of stimilus from test_config
        Tensor input0;
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, input0, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {
            std::map<std::string, uint32_t> tensor_address_map = {
                {"input0", test_address_map::INPUT_DATA_BASE}, {"output0", test_address_map::OUTPUT_DATA_BASE}};
            set_tensor_addresses(test_config, tensor_address_map);
            // Configure streams for tensor
            prepare_test(device, test_config);
            // Write Inputs
            write_tensor(device, input0, test_config);
            device.run();
            if (test_config.extra_config["math-op"].compare("zero_output") == 0) {
                device.wait_completion(200000, {llk::ThreadType::Ncrisc, llk::ThreadType::Pack});
            } else {
                device.wait_completion(700000);  // might need long time for sfpu to finish. timeout takes longer
            }
            // Read Output
            read_tensor(device, output0, test_config);
            // Check results
            config_result = check_sfpi_result(
                output0,
                expected_output0);
        } catch (...) {
        }
        device.stop();
        test_results.insert({config_string, config_result});
    }
    return test_results;
}
