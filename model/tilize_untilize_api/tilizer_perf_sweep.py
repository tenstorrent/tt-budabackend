# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from string import Template
import os
import subprocess
import re
import pandas as pd
import xlsxwriter
import time
import math

TILE_DIM = 32

def align_up(dim, alignment):
    new_dim = dim - 1
    return new_dim - (new_dim % alignment) + alignment

def generate_netlist_path_from_configs(netlist_dir, output_format, tensor_dimensions_ls, grid_dim_ls, input_queue_on_host, mmio_mapped_queue, stride, layout):
    output_netlist_path = netlist_dir + "/sweep_netlists/" + "netlist_" + output_format + "_" + "_".join(tensor_dimensions_ls)  + "_" + "_".join(grid_dim_ls)
    if(stride > 0):
        output_netlist_path += "_conv_stride_" + str(stride)
    output_netlist_path += "_" + layout
    if(input_queue_on_host):
        output_netlist_path += "_host.yaml"
    else:
        output_netlist_path += "_dram_"
        if(mmio_mapped_queue):
            output_netlist_path += "mmio.yaml"
        else:
            output_netlist_path += "non_mmio.yaml"
    return output_netlist_path

def generate_netlist_from_tensor_configs(output_format, tensor_dimensions_ls, grid_dim_ls, input_queue_on_host, mmio_mapped_queue, stride, arch, layout):
    netlist_dir = os.path.dirname(os.path.abspath(__file__))
    template_netlist_path = netlist_dir + "/template_netlist.yaml"
    output_netlist_path = generate_netlist_path_from_configs(netlist_dir, output_format, tensor_dimensions_ls, grid_dim_ls, input_queue_on_host, mmio_mapped_queue, stride, layout)

    if(not os.path.exists(output_netlist_path)):
        os.makedirs(os.path.dirname(output_netlist_path), exist_ok=True)
        if(stride > 0):
            
            mblock_m = int(math.ceil((int(tensor_dimensions_ls[1]) * stride * stride) / TILE_DIM) / int(grid_dim_ls[0]))
            mblock_n = int(math.ceil((align_up(int(tensor_dimensions_ls[2]), stride) * align_up(int(tensor_dimensions_ls[3]), stride)) / (stride * stride * TILE_DIM)) / int(grid_dim_ls[1]))
            t = 1
        else:
            mblock_n = int(int(tensor_dimensions_ls[2]) / int(grid_dim_ls[1]))
            mblock_m = int(int(tensor_dimensions_ls[3]) / int(grid_dim_ls[0]))
            t = tensor_dimensions_ls[1]

        tensor_configs = {"DeviceFormat" : output_format, "QueueEntries" : tensor_dimensions_ls[0], "PtrWrap" : str(2 * int(tensor_dimensions_ls[0])), "t" : t, "mblock_n" : mblock_n, "mblock_m" : mblock_m, "grid_r" : grid_dim_ls[0], "grid_c" : grid_dim_ls[1], "layout" : layout}
        if(arch == "grayskull" and output_format == "Float32"):
            tensor_configs["DeviceAccFormat"] = "Float16"
        else:
            tensor_configs["DeviceAccFormat"] = output_format
        if(input_queue_on_host):
            tensor_configs["QueueLocation"] = "host"
            if(grid_dim_ls[1] == "1"):
                tensor_configs["QueueAddress"] = "[0, 0x10000000]"
            elif grid_dim_ls[1] == "2":
                    tensor_configs["QueueAddress"] = "[0, 0x10000000], [0, 0x15000000]"
            elif grid_dim_ls[1] == "4":
                tensor_configs["QueueAddress"] = "[0, 0x10000000], [0, 0x12000000], [0, 0x31000000], [0, 0x16000000]"
            elif grid_dim_ls[1] == "8":
                tensor_configs["QueueAddress"] = "[0, 0x10000000], [0, 0x11000000], [0, 0x12000000], [0, 0x13000000], [0, 0x14000000], [0, 0x15000000], [0, 0x16000000], [0, 0x17000000]"
        else:
            tensor_configs["QueueLocation"]= "dram"
            if(mmio_mapped_queue):
                if grid_dim_ls[1] == "1":
                    tensor_configs["QueueAddress"] = "[0, 0x30000000]"
                elif grid_dim_ls[1] == "2":
                    tensor_configs["QueueAddress"] = "[0, 0x30000000], [0, 0x35000000]"
                elif grid_dim_ls[1] == "4":
                    tensor_configs["QueueAddress"] = "[0, 0x30000000], [0, 0x32000000], [0, 0x34000000], [0, 0x36000000]"
                elif grid_dim_ls[1] == "8":
                    tensor_configs["QueueAddress"] = "[0, 0x30000000], [0, 0x31000000], [0, 0x32000000], [0, 0x33000000], [0, 0x34000000], [0, 0x35000000], [0, 0x36000000], [0, 0x37000000]"
            else:
                if grid_dim_ls[1] == "1":
                    tensor_configs["QueueAddress"] = "[0, 0x20000000]"
                elif grid_dim_ls[1] == "2":
                    tensor_configs["QueueAddress"] = "[0, 0x20000000], [0, 0x35000000]"
                elif grid_dim_ls[1] == "4":
                    tensor_configs["QueueAddress"] = "[0, 0x20000000], [0, 0x22000000], [0, 0x24000000], [0, 0x26000000]"
                elif grid_dim_ls[1] == "8":
                    tensor_configs["QueueAddress"] = "[0, 0x20000000], [0, 0x21000000], [0, 0x22000000], [0, 0x23000000], [0, 0x24000000], [0, 0x25000000], [0, 0x26000000], [0, 0x27000000]"

        def get_subs(template_dict):
            return {f"TEMPLATE_{key}": val for (key,val) in template_dict.items()}
        
        with open(template_netlist_path, "r") as netlist_file:
            netlist = netlist_file.read()
            netlist = Template(netlist).safe_substitute(get_subs(tensor_configs))

        with open(output_netlist_path, "w+") as output_netlist_file:
            output_netlist_file.write(netlist)
    return output_netlist_path

def set_cpu_freq_governor(freq_governor_mode):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cmd = "source " + script_dir + "/set_cpu_freq_mode.sh " + freq_governor_mode
    subprocess.run("bash -c " + "'" + cmd + "'", shell=True) # Running a bash script

def main():
    supported_input_formats = ["Float32", "Float16", "Float16_b", "Int8"]
    supported_output_formats = ["Float32", "Float16", "Float16_b", "Bfp8", "Bfp8_b"]
    supported_cpu_freq_modes = ["performance", "ondemand"]
    default_input_shapes = ["1,1,1,1", "128,1,32,4", "256,1,32,4", "128,1,32,12", "256,1,32,12"]
    default_conv_input_shapes = ["128,3,223,223", "128,3,224,224", "128,3,225,225", "128,3,226,226"]
    io_configs_to_skip = ["Int8_Float32", "Int8_Float16", "Int8_Float16_b"]
    default_queue_layouts = ["flat", "tilized"]
    flat_layout_grid_sizes = [["1", "1"], ["1", "2"], ["1", "4"], ["1", "8"]]
    tilized_layout_grid_sizes = [["1", "1"]]

    parser = argparse.ArgumentParser()
    parser.add_argument("--tensor_dimensions", type = str, default = "*") # 5
    parser.add_argument("--input_format", type = str, default = "*") # 18
    parser.add_argument("--output_format", type = str, default = "*") 
    parser.add_argument("--multithreaded_push", type = str, default = "0") # 2
    parser.add_argument("--freq_governor_mode", type = str, default = "performance") # 2
    parser.add_argument("--input_queue_on_host", type = str, default = "0") # 3
    parser.add_argument("--non_mmio_mapped_input_queue", type = str, default = "0")
    parser.add_argument("--enable_cpu_allocator", type = str, default = "0") # 2
    parser.add_argument("--enable_full_sweep", action = "store_true", default = False)
    parser.add_argument("--convolution_stride", type = int, default = 0)
    parser.add_argument("--arch", type = str, required = True)
    parser.add_argument("--loops", type = str, default = "20")
    parser.add_argument("--output_file", type = str, default = "tilizer_perf_sweep_results.xlsx")
    parser.add_argument("--queue_layout", type = str, default = "*")
    args = parser.parse_args()

    assert args.arch in ["grayskull", "wormhole", "wormhole_b0"], "Invalid arch provided"

    os.environ["ARCH_NAME"] = args.arch
    if(args.tensor_dimensions == "*" or args.enable_full_sweep):
        if(args.convolution_stride > 0):
            tensor_dimensions_ls = [x.split(",") for x in default_conv_input_shapes]
        else:
            tensor_dimensions_ls = [x.split(",") for x in default_input_shapes]
    else:
        tensor_dimensions_ls = [args.tensor_dimensions.split(",")]
        assert len(tensor_dimensions_ls[0]) == 4, "Tensor Dimensions should specify a 4D input with {w, t, mblock_n, mblock_m}"
    if(args.input_format == "*" or args.enable_full_sweep ):
        input_formats_ls = supported_input_formats
    else:
        assert args.input_format in supported_input_formats, "Cannot support input format: " + args.input_format
        input_formats_ls = [args.input_format]

    if(args.output_format == "*" or args.enable_full_sweep):
        output_format_ls = supported_output_formats
    else:
        assert args.output_format in supported_output_formats, "Cannot support input format: " + args.output_format
        output_format_ls = [args.output_format]

    if(args.multithreaded_push == "*" or args.enable_full_sweep):
        multithreaded_push_ls = ["0", "1"]
    else:
        multithreaded_push_ls = [args.multithreaded_push]
    if(args.freq_governor_mode == "*" or args.enable_full_sweep):
        freq_governor_ls = supported_cpu_freq_modes
    else:
        assert args.freq_governor_mode in supported_cpu_freq_modes, "CPU Frequency Governor does not support: " + args.freq_governor_mode
        freq_governor_ls = [args.freq_governor_mode]
    
    if(args.input_queue_on_host == "*" or args.enable_full_sweep):
        host_input_ls = [False, True]
    else:
        assert args.input_queue_on_host == "0" or args.input_queue_on_host == "1", "input_queue_on_host must be true or false"
        host_input_ls = [bool(int(args.input_queue_on_host))]

    if(args.non_mmio_mapped_input_queue == "*" or args.enable_full_sweep):
        dram_q_loc_ls = [False, True]
    else:
        assert args.non_mmio_mapped_input_queue == "0" or args.non_mmio_mapped_input_queue == "1", "non_mmio_mapped_input_queue must be true or false"
        dram_q_loc_ls = [bool(int(args.non_mmio_mapped_input_queue))]

    if(args.convolution_stride > 0):
        print("Running Convolution Benchmarks")
    if(args.enable_cpu_allocator == "*" or args.enable_full_sweep):
        cpu_alloc_en_ls = ["0", "1"]
    else:
        assert args.enable_cpu_allocator == "0" or args.enable_cpu_allocator == "1", "enable_cpu_allocator must be true or false"
        cpu_alloc_en_ls = [args.enable_cpu_allocator]

    if(args.queue_layout == "*" or args.enable_full_sweep):
        queue_layouts = default_queue_layouts
    else:
        assert args.queue_layout == "tilized" or args.queue_layout == "flat"
        queue_layouts = [args.queue_layout]

    start_time = time.time()
    perf_data_raw = []
    bw_string_pattern = r"Average Push BW: (\d+\.\d+)"
    for layout in queue_layouts:
        if(layout == "tilized"):
            grid_dim_ls = tilized_layout_grid_sizes
        else:
            grid_dim_ls = flat_layout_grid_sizes     
        for cpu_freq_mode in freq_governor_ls:
            print(f"Setting Frequency governor to {cpu_freq_mode}")
            set_cpu_freq_governor(cpu_freq_mode)
            for queue_loc in host_input_ls:
                for dram_q_loc in dram_q_loc_ls:
                    if(queue_loc and dram_q_loc):
                        print("Queue is on host, but also mapped to non-mmio space. Skipping to avoid duplicates")
                        continue
                    for cpu_alloc_setting in cpu_alloc_en_ls:
                        for multithreaded_mode in multithreaded_push_ls:
                            for input_format in input_formats_ls:
                                for output_format in output_format_ls:
                                    if(input_format + "_" + output_format in io_configs_to_skip):
                                        print(f"Skipping conversion from {input_format} to {output_format}")
                                        continue
                                    for tensor_dim in tensor_dimensions_ls:
                                        for grid_dim in grid_dim_ls:
                                            if int(grid_dim[1]) <= int(tensor_dim[2]) and int(tensor_dim[2]) % int(grid_dim[1]) == 0:
                                                cmd =  "TT_BACKEND_CPUSET_ALLOCATOR_ENABLE=" +  cpu_alloc_setting + " TT_BACKEND_DISABLE_MULTI_THREADED_PUSH=" + str(int(not(bool(int(multithreaded_mode)))))
                                                netlist_path = generate_netlist_from_tensor_configs(output_format, tensor_dim, grid_dim, queue_loc, not dram_q_loc, args.convolution_stride, args.arch, layout) 
                                                print(f"Running benchmark for input_format = {input_format}, output_format = {output_format}, shape = {','.join(tensor_dim)}.")
                                                cmd += " ./build/test/loader/tests/measure_push_bw --netlist " + netlist_path + " --num-loops " + args.loops + " --host-data-format " + input_format
                                                if(args.convolution_stride > 0):
                                                    cmd += " --stride " + str(args.convolution_stride) 
                                                    cmd += " --input-tensor-shape " + ",".join(tensor_dim)
                                                print("Running: " + cmd)
                                                result = subprocess.run(cmd, shell = True, capture_output = True, text = True)

                                                match = re.search(bw_string_pattern, result.stdout)
                                                if(match):
                                                    average_push_bw = float(match.group(1))
                                                    perf_data_raw.append({"Input Format" : input_format, "Output Format" : output_format, "Tensor Dims" : "x".join(tensor_dim), "CPU Alloc Enabled" : str(bool(int(cpu_alloc_setting))), "Multithreaded" : str(bool(int(multithreaded_mode))),  "CPU Mode" : cpu_freq_mode, "Bandwidth" : average_push_bw, "Grid Dim": "x".join(grid_dim)})
                                                    if(queue_loc):
                                                        perf_data_raw[-1]["Queue Location"] = "Host"
                                                    else:
                                                        if(dram_q_loc):
                                                            perf_data_raw[-1]["Queue Location"] = "Non MMIO"
                                                        else:
                                                            perf_data_raw[-1]["Queue Location"] = "MMIO"

    
    #print(perf_data_raw)

    perf_data_sorted_by_tensor_dims = {}
    for run in perf_data_raw:
        if(not run["Tensor Dims"] in perf_data_sorted_by_tensor_dims):
            perf_data_sorted_by_tensor_dims[run["Tensor Dims"]] = []
        perf_data_sorted_by_tensor_dims[run["Tensor Dims"]].append(run)

    with pd.ExcelWriter(args.output_file, engine = 'xlsxwriter') as writer:
        for tensor_dim in perf_data_sorted_by_tensor_dims:
            print(tensor_dim)
            df = pd.DataFrame(perf_data_sorted_by_tensor_dims[tensor_dim])
            grouped = df.groupby(['CPU Mode', 'Input Format', 'Output Format', 'Multithreaded', 'CPU Alloc Enabled', 'Queue Location', 'Grid Dim'])['Bandwidth'].sum().reset_index()
            pivot_table = pd.pivot_table(grouped, values='Bandwidth', index=['CPU Mode', 'Input Format', 'Output Format', 'Queue Location'], columns=['Grid Dim', 'CPU Alloc Enabled', 'Multithreaded'], aggfunc='first')
            
            pivot_table.to_excel(writer, tensor_dim)
            print(pivot_table)
    print(f"Sweep took {time.time() - start_time} seconds")
    print(f"Perf Sweep Results written to {args.output_file}")

    
if __name__ == "__main__":
    main()