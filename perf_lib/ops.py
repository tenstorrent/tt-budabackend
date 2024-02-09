# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from typing import List
from perf_test_base import (
    OpType,
    ReblockTMPerfLib,
    get_num_input_operands,
    VAR
)

class OpBaseAttr:

    def __init__ (self, op_type: OpType, reblock_mode: ReblockTMPerfLib, config):
        self.op_type = op_type
        self.reblock_mode = reblock_mode
        if config == "":
            return
        
        self.math_fidelity: str = config["math_fidelity"]
        
        self.data_format = []
        self.t = []
        self.grid_size = []
        self.mblock = []
        self.ublock = []
        self.ublock_order = []
        self.buf_size_mb = []
        for i in list(range(get_num_input_operands(self.op_type))) + [-1]:
            self.data_format.append(config[VAR("df", i)])
            self.t.append(config[VAR("t", i)])
            self.grid_size.append([config[VAR("grid_size_r", i)], config[VAR("grid_size_c", i)]])
            self.mblock.append([config[VAR("mb_r", i)], config[VAR("mb_c", i)]])
            self.ublock.append([config[VAR("ub_r", i)], config[VAR("ub_c", i)]])
            self.ublock_order.append(config[VAR("ublock_order", i)])
            self.buf_size_mb.append(config[VAR("buf_size_mb", i)])

        self.target_op_name = config["target_op_name"]

    def get_attr_labels_and_alignment(self):
        label = ("math_fidelity",)
        for i in list(range(get_num_input_operands(self.op_type))) + [-1]:
            label += (
                VAR("df", i),
                VAR("t", i),
                VAR("grid_size_r", i),
                VAR("grid_size_c", i),
                VAR("mb_r", i),
                VAR("mb_c", i),
                VAR("ub_r", i),
                VAR("ub_c", i),
                VAR("ublock_order", i),
                VAR("buf_size_mb", i),
            )
        table_alignment = "%-15s " * len(label)
        return label, table_alignment
    
    def get_attr_report_single_run(self):
        attr = (self.math_fidelity,)
        for i in list(range(get_num_input_operands(self.op_type))) + [-1]:
            attr += (
                self.data_format[i],
                self.t[i],
                self.grid_size[i][0],
                self.grid_size[i][1],
                self.mblock[i][0],
                self.mblock[i][1],
                self.ublock[i][0],
                self.ublock[i][1],
                self.ublock_order[i],
                self.buf_size_mb[i],
            )
        return attr

    def get_sorted_labels(self):
        return self.get_attr_labels_and_alignment()[0][1:]
    
    # May want to exclude the first element which is the test ID
    def check_entries_match(self, attr_report_0: List[List[any]], attr_report_1: List[List[any]], exclude_first_element=True):
        # Have to also compare the str version of the attr list
        # Since when reading from csv, all attr will be read as str
        start_index = 1 if exclude_first_element else 0
        attr_0_str = [[str(attr) for attr in row[start_index:]] for row in attr_report_0]
        attr_1_str = [[str(attr) for attr in row[start_index:]] for row in attr_report_1]
        return (
            attr_report_0 == attr_report_1 or
            attr_0_str == attr_1_str
        )

class MatmulAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config, op_type=OpType.Matmul):
        super().__init__(op_type, reblock_mode, config)
        if config == "":
            return
        self.op_type = op_type
        self.mblock_k = config["mb_inner"]
        self.ublock_kt = config["ub_inner"]
        self.l1_acc = True if config["l1_acc"].lower() == "true" else False
    
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        
        label += ("mblock_k", "ublock_kt", "l1_acc")
        table_alignment += "%-15s %-15s %15s "
        
        if self.op_type == OpType.Matmul:
            label = ("Test-ID",) + label
            table_alignment = "%-10s " + table_alignment
            
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr += (self.mblock_k, self.ublock_kt, self.l1_acc)
        
        if self.op_type == OpType.Matmul:
            attr = (test_id,) + attr
            
        return attr

    def get_sorted_labels(self):
        return ["average-math-utilization-all-inputs", "total-runtime"]
    
class MatmulBiasAttr(MatmulAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, op_type=OpType.MatmulBias)
        if config == "":
            return

    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()        
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run(test_id)
        return attr

class MatmulSparse(MatmulAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, op_type=OpType.MatmulSparse)
        if config == "":
            return
        self.zero_strip_freq = config["zero_strip_freq"]
        self.zero_ublock_freq = config["zero_ublock_freq"]
        self.zero_tile_freq = config["zero_tile_freq"]
        
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("Test-ID", "zero_strp_freq", "zero_ublock_freq", "zero_tile_freq") + label
        table_alignment  = "%-10s %-10s %-10s %-10s " + table_alignment    
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run(test_id)
        attr = (test_id, self.zero_strip_freq, self.zero_ublock_freq, self.zero_tile_freq) + attr
        return attr

class MatmulSparseNzCounts(MatmulAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, op_type=OpType.MatmulSparseNzCounts)
        if config == "":
            return
        self.nz_strips = config["nz_strips"]
        self.nz_ublocks = config["nz_ublocks"]
        self.nz_tiles = config["nz_tiles"]
        
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()    
        label = ("Test-ID", "nz_strips", "nz_ublocks", "nz_tiles") + label
        table_alignment  = "%-10s %-10s %-10s %-10s " + table_alignment    
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run(test_id)
        attr = (test_id, self.nz_strips, self.nz_ublocks, self.nz_tiles) + attr
        return attr
    
class MatmulQuantAttr(MatmulAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, op_type=OpType.MatmulQuant)
        if config == "":
            return
        self.requant = True if config["requant"].lower() == "true" else False
        self.dequant = True if config["dequant"].lower() == "true" else False
        self.zero_point = config["zero_point"]
        
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("Test-ID", "requant", "dequant", "zero_point") + label
        table_alignment  = "%-10s %-10s %-10s %-10s " + table_alignment
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run(test_id)
        attr = (test_id, self.requant, self.dequant, self.zero_point) + attr
        return attr

class MatmulBiasQuantAttr(MatmulAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, op_type=OpType.MatmulBiasQuant)
        if config == "":
            return
        self.requant = True if config["requant"].lower() == "true" else False
        self.dequant = True if config["dequant"].lower() == "true" else False
        self.zero_point = config["zero_point"]
        
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("Test-ID", "requant", "dequant", "zero_point") + label
        table_alignment  = "%-10s %-10s %-10s %-10s " + table_alignment
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run(test_id)
        attr = (test_id, self.requant, self.dequant, self.zero_point) + attr
        return attr
    
class BinaryAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Binary, reblock_mode, config)
        if config == "":
            return
        self.binary_type = config["binary_type"]

    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        label =  ("Test-ID", "Binary-Type") + label
        table_alignment = "%-10s %-15s " + table_alignment
        return label, table_alignment

    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id, self.binary_type) + attr
        return attr

class UnaryAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Unary, reblock_mode, config)
        if config == "":
            return
        self.unary_type = config["unary_type"]
        self.vector_mode = config["vector_mode"]
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        label =  ("Test-ID", "Unary-Type", "Vector-Mode") + label
        table_alignment = "%-10s %-15s %-15s " + table_alignment

        return label, table_alignment
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id, self.unary_type, self.vector_mode) + attr
        return attr

class TMAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.TM, reblock_mode, config)
        if config == "":
            return
        self.unary_type = config["unary_type"]
        self.tm_factor = config["factor"]
        self.tm_type = config["tm_type"]
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        label =  ("Test-ID", "TM-Type", "TM-Factor", "Unary-Type") + label
        table_alignment = "%-10s %-15s %-15s %-15s " + table_alignment
        return label, table_alignment
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id, self.tm_type, self.tm_factor, self.unary_type) + attr
        return attr
    
    def get_sorted_labels(self):
        all_labels, _ = self.get_attr_labels_and_alignment()
        labels_to_sort = [
            "TM-Type",
            "output_t",
            "output_grid_size_r",
            "output_grid_size_c",
            "output_mb_r",
            "output_mb_c",
            "output_ub_r",
            "output_ub_c"
        ]
        for label in labels_to_sort:
            assert label in all_labels

        return labels_to_sort
    
    def check_entries_match(self, attr_report_0, attr_report_1):
        all_labels, _ = self.get_attr_labels_and_alignment()
        labels_to_check = [
            'Test-ID',
            "output_t",
            "output_grid_size_r",
            "output_grid_size_c",
            "output_mb_r",
            "output_mb_c",
            "output_ub_r",
            "output_ub_c"
        ]
        for label in labels_to_check:
            assert label in all_labels
            attr_idx = all_labels.index(label)
            for entry_idx in range(len(attr_report_0)):
                if (
                    attr_report_0[entry_idx][attr_idx] != attr_report_1[entry_idx][attr_idx] and
                    str(attr_report_0[entry_idx][attr_idx]) != str(attr_report_1[entry_idx][attr_idx])
                ):
                    return False
        return True
    
    def get_sweep_parameters(self, disable_tm=True):
        tm_factor = 1 if disable_tm else self.tm_factor
        tm_type = 1 if disable_tm else TMType[self.tm_type]
        # The new test should run with the output t of the TM test as the input t of the test without TMs
        input0_t = self.output_t if disable_tm else self.input0_t
        sweep_params = {
            'input0_t': [input0_t],

            'output_mb_r': [self.mblock[-1][0]],
            'output_mb_c': [self.mblock[-1][1]],
            'output_ub_r': [self.ublock[-1][0]],
            'output_ub_c': [self.ublock[-1][1]],
            
            'output_grid_size_r': [self.grid_size[-1][0]],
            'output_grid_size_c': [self.grid_size[-1][1]],

            'factor': [tm_factor],
            'tm_type': [tm_type],
        }
        return sweep_params

class DramAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Dram, reblock_mode, config)
        if config == "":
            return
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        label = ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id,) + attr
        return attr

class PcieAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Pcie, reblock_mode, config)
        if config == "":
            return
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        label = ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id,) + attr
        return attr
    
class FusedOp(OpBaseAttr):
    def __init__ (self, reblock_mode, config, fused_op_type):
        super().__init__(fused_op_type, reblock_mode, config)
        if config == "":
            return
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (test_id,) + attr
        return attr

class FusedOp0(FusedOp):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, OpType.Fused0)
        if config == "":
            return
class FusedOp2(FusedOp):
    def __init__ (self, reblock_mode, config):
        super().__init__(reblock_mode, config, OpType.Fused2)
        if config == "":
            return

class ReduceMaxAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.ReduceMax, reblock_mode, config)
        if config == "":
            return
        self.mblock_k = int(config["mb_inner"].split(":").pop())
        self.ublock_kt = int(config["ub_inner"].split(":").pop())
        self.reduce_dim = config["reduce_dim"]
    
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        
        label += ("mblock_k", "ublock_kt")
        table_alignment += "%-15s %-15s "

        label = ("Test-ID", "reduce-dim") + label
        table_alignment = "%-10s %-15s " + table_alignment
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr += (self.mblock_k, self.ublock_kt)
        attr = (test_id, self.reduce_dim) + attr
        return attr

class SpliceAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Splice, reblock_mode, config)
        if config == "":
            return
        self.splice_index = []
        self.splice_length = []
        self.splice_stride = []
        for i in range(get_num_input_operands(OpType.Splice)):
            self.splice_index.append(config[VAR("index", i)])
            self.splice_length.append(config[VAR("length", i)])
            self.splice_stride.append(config[VAR("stride", i)])

    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()

        for i in range(get_num_input_operands(OpType.Splice)):
            label += (VAR("index", i), VAR("length", i), VAR("stride", i),)
            table_alignment += "%-15s %-15s %-15s "

        label =  ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment

    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        for i in range(get_num_input_operands(OpType.Splice)):
            attr += (self.splice_index[i], self.splice_length[i], self.splice_stride[i],)
        attr = (test_id,) + attr
        return attr
    
class TilizerAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Tilizer, reblock_mode, config)
        if config == "":
            return
        self.parallelization_factor_r = config["parallelization_factor_r"]
        self.parallelization_factor_c = config["parallelization_factor_c"]
        self.total_num_tiles = self.grid_size[0][0] * self.grid_size[0][1] * self.mblock[0][0] * self.mblock[0][1] * self.ublock[0][0] * self.ublock[0][1]
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("total_num_tiles", "paralellization_factor_r", "parallelization_factor_c") + label
        table_alignment = "%-15s " * len(label)

        label =  ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (self.total_num_tiles, self.parallelization_factor_r, self.parallelization_factor_c) + attr
        attr = (test_id, ) + attr
        return attr
    
    def get_sorted_labels(self):
        return ["total-runtime-per-tile", VAR("ub_c", 0), "total_num_tiles"]

class QuantAttr(OpBaseAttr):
    def __init__ (self, reblock_mode, config):
        super().__init__(OpType.Quant, reblock_mode, config)
        if config == "":
            return
        self.quant_type = config["quant_type"]
        self.zero_point = config["zero_point"]
        
    def get_attr_labels_and_alignment(self):
        label, table_alignment = super().get_attr_labels_and_alignment()
        label = ("quant_type", "zero_point") + label
        table_alignment = "%-15s " * len(label)

        label =  ("Test-ID",) + label
        table_alignment = "%-10s " + table_alignment
        return label, table_alignment
    
    def get_attr_report_single_run(self, test_id):
        attr = super().get_attr_report_single_run()
        attr = (self.quant_type, self.zero_point) + attr
        attr = (test_id, ) + attr
        return attr

    def get_sorted_labels(self):
        return ["quant_type", "average-math-utilization-all-inputs", "total-runtime"]
    
def get_perf_labels_and_alignment(verbose, op_type, reblocking_mode):
    if verbose:
        label = (
            "input-idx",
            "core-label",
            "total-wait-tile", "total-wait-free-tile",
            "runtime-last-input", "math-util-last-input", "cycles/tile",
            "first-input-idx-recorded", "last-input-idx-recorded",
            "average-math-utilization-all-inputs",
            "total-runtime",
        )
        
        if op_type == OpType.Tilizer:
            label += ("total-runtime-per-tile", )

        label += (
            "input0-bw", "input1-bw", "output-bw",
            "core-label",
            "total-wait-tile", "total-wait-free-tile",
            "runtime-last-input", "math-util-last-input", "cycles/tile",
            "first-input-idx-recorded", "last-input-idx-recorded",
            "average-math-utilization-all-inputs",
            "total-runtime",
        )

        if op_type == OpType.Tilizer:
            label += ("total-runtime-per-tile", )
        
        label += (
            "input0-bw", "input1-bw", "output-bw",
            "input0-bw-across-cores-total-runtime",
            "input1-bw-across-cores-total-runtime",
            "output-bw-across-cores-total-runtime",
        )
    else:
        label = (
            "first-input-idx-recorded", "last-input-idx-recorded",
            "average-math-utilization-all-inputs",
            "total-runtime",
        )

        if op_type == OpType.Tilizer:
            label += ("total-runtime-per-tile", )
        
        label += (
            "input0-bw", "input1-bw", "output-bw",
            "input0-bw-across-cores-total-runtime",
            "input1-bw-across-cores-total-runtime",
            "output-bw-across-cores-total-runtime",
        )
    
    if reblocking_mode == ReblockTMPerfLib.Hstack or reblocking_mode == ReblockTMPerfLib.Broadcast_r:
        feeder0_label = ()
        for i in label:
            feeder0_label += (f"feeder0_{i}",)
        label += feeder0_label
    
    table_alignment = "%-25s " * len(label)
    return label, table_alignment

def get_report_label_and_alignment(attr, verbose, op_type, reblocking_mode):
    attr_label, attr_alignment = attr.get_attr_labels_and_alignment()
    perf_label, perf_alignment = get_perf_labels_and_alignment(verbose, op_type, reblocking_mode)
    return attr_label+perf_label, attr_alignment+perf_alignment

def get_perf_report_single_run(perf_results_min_runtime, perf_results_max_runtime, verbose, op_type):
    assert perf_results_min_runtime.target_input == perf_results_max_runtime.target_input
    results = ()
    if verbose:
        results += (
            perf_results_min_runtime.target_input,

            perf_results_min_runtime.target_core.replace('-', ','),
            perf_results_min_runtime.total_wait_for_tile, perf_results_min_runtime.total_wait_for_free_tile,
            perf_results_min_runtime.execution_cycles, perf_results_min_runtime.math_utilization, perf_results_min_runtime.cycles_per_tile,

            perf_results_min_runtime.first_input_recorded, perf_results_min_runtime.last_input_recorded,
            perf_results_min_runtime.average_math_utilization,
            perf_results_min_runtime.total_runtime,
        )

        if op_type == OpType.Tilizer:
            results += (perf_results_min_runtime.total_runtime_per_tile, )
        
        results += (
            perf_results_min_runtime.input0_bw, perf_results_min_runtime.input1_bw, perf_results_min_runtime.output_bw,

            perf_results_max_runtime.target_core.replace('-', ','),
            perf_results_max_runtime.total_wait_for_tile, perf_results_max_runtime.total_wait_for_free_tile,
            perf_results_max_runtime.execution_cycles, perf_results_max_runtime.math_utilization, perf_results_max_runtime.cycles_per_tile,

            perf_results_max_runtime.first_input_recorded, perf_results_max_runtime.last_input_recorded,
            perf_results_max_runtime.average_math_utilization,
            perf_results_max_runtime.total_runtime,
        )

        if op_type == OpType.Tilizer:
            results += (perf_results_max_runtime.total_runtime_per_tile, )
        
        results += (
            perf_results_max_runtime.input0_bw, perf_results_max_runtime.input1_bw, perf_results_max_runtime.output_bw,

            perf_results_max_runtime.input0_bw_across_cores_total_runtime,
            perf_results_max_runtime.input1_bw_across_cores_total_runtime,
            perf_results_max_runtime.output_bw_across_cores_total_runtime,
        )

    else:
        results += (
            perf_results_max_runtime.first_input_recorded, perf_results_max_runtime.last_input_recorded,
            perf_results_max_runtime.average_math_utilization,
            perf_results_max_runtime.total_runtime,
        )
        if op_type == OpType.Tilizer:
            results += (perf_results_max_runtime.total_runtime_per_tile, )

        results += (
            perf_results_max_runtime.input0_bw, perf_results_max_runtime.input1_bw, perf_results_max_runtime.output_bw,
            
            perf_results_max_runtime.input0_bw_across_cores_total_runtime,
            perf_results_max_runtime.input1_bw_across_cores_total_runtime,
            perf_results_max_runtime.output_bw_across_cores_total_runtime,
        )
    return results
# def get_report_single_run(op_type, attr, perf_results_min_runtime, perf_results_max_runtime, test_id, ):
#     assert perf_results_min_runtime.target_input == perf_results_max_runtime.target_input
#     attr_results = get_attr_report_single_run(op_type, attr, test_id, perf_results_min_runtime.target_input)
#     perf_results = get_perf_report_single_run(perf_results_min_runtime, perf_results_max_runtime)
#     return attr_results + perf_results

def get_attr(op_type, reblock_mode, config):
    if op_type == OpType.Matmul:
        return MatmulAttr(reblock_mode, config)
    elif op_type == OpType.MatmulBias:
        return MatmulBiasAttr(reblock_mode, config)
    elif op_type == OpType.Binary:
        return BinaryAttr(reblock_mode, config)
    elif op_type == OpType.Unary:
        return UnaryAttr(reblock_mode, config)
    elif op_type == OpType.TM:
        return TMAttr(reblock_mode, config)
    elif op_type == OpType.Dram:
        return DramAttr(reblock_mode, config)
    elif op_type == OpType.Fused0:
        return FusedOp0(reblock_mode, config)
    elif op_type == OpType.Fused2:
        return FusedOp2(reblock_mode, config)
    elif op_type == OpType.Pcie:
        return PcieAttr(reblock_mode, config)
    elif op_type == OpType.MatmulSparse:
        return MatmulSparse(reblock_mode, config)
    elif op_type == OpType.MatmulSparseNzCounts:
        return MatmulSparseNzCounts(reblock_mode, config)
    elif op_type == OpType.ReduceMax:
        return ReduceMaxAttr(reblock_mode, config)
    elif op_type == OpType.Splice:
        return SpliceAttr(reblock_mode, config)
    elif op_type == OpType.Tilizer:
        return TilizerAttr(reblock_mode, config)
    elif op_type == OpType.MatmulQuant:
        return MatmulQuantAttr(reblock_mode, config)
    elif op_type == OpType.MatmulBiasQuant:
        return MatmulBiasQuantAttr(reblock_mode, config)
    elif op_type == OpType.Quant:
        return QuantAttr(reblock_mode, config)
    else:
        raise ValueError("Invalid op type")

def get_empty_attr(op_type, reblocking_mode_perf_lib):
    return get_attr(op_type, reblocking_mode_perf_lib, "")
