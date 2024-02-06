from copy import deepcopy

import yaml
from .dramallocation import DramFactory, get_dram_queue_buffer_size
from .netlist import Netlist, Graph, NetlistConsts, Operation, Queue, QueueSetting

class Chip:
    def __init__(self, rows, columns) -> None:
        self.__rows = rows
        self.__columns = columns
        self.__cores = [[False for i in range(columns)] for j in range(rows)]

    def set_row_column(self, row, column):
        if not self.is_free(row, column):
            raise Exception(f"Location [{row},{column}] is occupied")
        self.__cores[row][column] = True

    def set_rows_columns(self, row, column, rows, columns):
        assert (row >= 0 and column >=0 and row + rows <=self.__rows and column + columns <= self.__columns)
        for r in range(row, row + rows):
            for c in range(column, column + columns):
                self.set_row_column(r, c)

    def is_free(self, row, column):
        return self.__cores[row][column] == False

    def can_place(self, row, column, rows, columns):
        if row + rows >= self.__rows or column + columns >= self.__columns:
            return False
        for r in range(row, row + rows):
            for c in range(column, column + columns):
                if not self.is_free(r, c):
                    return False
        return True

    def get_free_location(self, rows, columns):
        for r in range(self.__rows):
            for c in range(self.__columns):
                if self.can_place(r, c, rows, columns):
                    self.set_rows_columns(r,c, rows, columns)
                    return [r,c]
        return None

class NetlistBuilderHelper:
    def build_empty_netlist_from_template(mynl):
        nl = Netlist(mynl).clone()
        nl.get_queues().clear()
        nl.get_graphs().clear()
        nl.get_programs().clear()
        return nl

    def build_netlist_from_file(filename):
        return Netlist(yaml.safe_load(open(filename)))

    def save_netlist_to_file(filename, nl):
        mynl = Netlist(nl)
        with open(filename, "w") as f:
            f.write(str((mynl)))
        f.close()

    def build_empty_graph(mygraph):
        return Graph.create_graph(mygraph.get_target_device(), mygraph.get_input_count())

    def build_runtime_ram_settings(zero, prolog, epilog, wr_ptr_global, rd_ptr_global):
        return {
            "zero":zero,
            "prologue":prolog,
            "epilogue":epilog,
            "wr_ptr_global":wr_ptr_global,
            "rd_ptr_global":rd_ptr_global
        }

    def build_runtime_queue_settings(zero, prolog, epilog, rd_ptr_global, rd_ptr_local, rd_ptr_autoinc):
        return {
            "zero":zero,
            "prologue":prolog,
            "epilogue":epilog,
            "rd_ptr_global":rd_ptr_global,
            "rd_ptr_local":rd_ptr_local,
            "global_rdptr_autoinc":rd_ptr_autoinc
        }

    def get_default_queue_setting(qs):
        result = {}
        for name in qs.get_queue_names():
            setting = qs.get_queue_setting(name)
            if not setting.has(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS_WR_PTR_GLOBAL):
                result = setting.clone()
                result.set(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS_PROLOGUE, True)
                return result

        d = qs.get_queue_setting(qs.get_queue_names()[0]).clone()
        if d.has(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS_WR_PTR_GLOBAL):
            d.remove(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS_WR_PTR_GLOBAL)
        d.set(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS_PROLOGUE, True)
        return d

    def build_dram_queue(input, entries, target_device, memory, df, grid_size, mblock, ublock, t):
        return Queue({
            NetlistConsts.QUEUE_INPUT : input,
            NetlistConsts.QUEUE_TYPE : "queue",
            NetlistConsts.QUEUE_ENTRIES : entries,
            NetlistConsts.QUEUE_GRID_SIZE : grid_size,
            NetlistConsts.QUEUE_T :t,
            NetlistConsts.QUEUE_MBLOCK : mblock,
            NetlistConsts.QUEUE_UBLOCK : ublock,
            NetlistConsts.QUEUE_DATA_FORMAT : df,
            NetlistConsts.QUEUE_TARGET_DEVICE : target_device,
            NetlistConsts.QUEUE_LOCATION : NetlistConsts.QUEUE_LOCATION_DRAM,
            NetlistConsts.QUEUE_DRAM : memory
        })

    def build_dram_queue_from_operation(nl, graph_name, operation_name, input):
        mynl = Netlist(nl)
        mygraph = mynl.get_graphs().get_graph(graph_name)
        myop = mygraph.get_operation(operation_name)
        return NetlistBuilderHelper.build_dram_queue(
            input,
            mynl.get_queues().get_max_entries(),
            mygraph.get_target_device(),
            None,
            myop.get_out_df(),
            myop.get_grid_size().get_raw(),
            myop.get_mblock(),
            myop.get_ublock(),
            myop.get_t()
            )

    def create_datacopy_operation_from_op(nl, graph_name, operation_name, input_name, grid_loc):
        datacopy_op = Operation(
            {
                NetlistConsts.OPERATION_TYPE: NetlistConsts.OPERATION_TYPE_DATACOPY,
                NetlistConsts.OPERATION_GRID_LOC: None, 
                NetlistConsts.OPERATION_GRID_SIZE: None, 
                NetlistConsts.OPERATION_INPUT_NAMES: None,
                NetlistConsts.OPERATION_ACC_DF: None, 
                NetlistConsts.OPERATION_IN_DF: None,
                NetlistConsts.OPERATION_OUT_DF: None,
                NetlistConsts.OPERATION_INTERMED_DF: None,
                NetlistConsts.OPERATION_UBLOCK_ORDER: NetlistConsts.OPERATION_UBLOCK_ORDER_ROW,
                NetlistConsts.OPERATION_BUF_SIZE_MB: None,
                NetlistConsts.OPERATION_MATH_FIDELITY: None,
                NetlistConsts.OPERATION_UNTILIZE_OUTPUT: False,
                NetlistConsts.OPERATION_T: None,
                NetlistConsts.QUEUE_MBLOCK: None, 
                NetlistConsts.QUEUE_UBLOCK: None})

        op = Netlist(nl).get_graphs().get_graph(graph_name).get_operation(operation_name)
        datacopy_op.set_grid_loc(grid_loc)
        datacopy_op.set_grid_size(op.get_grid_size())
        datacopy_op.set_input_names([input_name])
        datacopy_op.set_acc_df(op.get_out_df())
        datacopy_op.set_in_df([op.get_out_df()])
        datacopy_op.set_intermed_df(op.get_out_df())
        datacopy_op.set_buf_size_mb(op.get_buf_size_mb())
        datacopy_op.set_math_fidelity(op.get_math_fidelity())
        datacopy_op.set_t(op.get_t())
        datacopy_op.set_mblock(op.get_mblock())
        datacopy_op.set_ublock(op.get_ublock())
        datacopy_op.set_out_df(op.get_out_df())
        return datacopy_op

    def get_queue_name_from_op_name(operation_name):
        return "DBG_" + operation_name

    def get_input_op_name_from_op_name(operation_name):
        return "DBG_DATACOPY_IN_" + operation_name

    def get_output_op_name_from_op_name(operation_name):
        return "DBG_DATACOPY_OUT_" + operation_name

    def calc_grid_location(nl, graph_name, grid_size):
        X_CORES = 10
        Y_CORES = 12
        chip = Chip(X_CORES,Y_CORES)
        mynl = Netlist(nl)

        for op_name in mynl.get_graph(graph_name).get_operation_names():
            pos = mynl.get_operation(graph_name, op_name).get_grid_loc()
            size = mynl.get_operation(graph_name, op_name).get_grid_size()
            chip.set_rows_columns(pos.get_x(), pos.get_y(), size.get_x(), size.get_y())

        return chip.get_free_location(grid_size.get_x(), grid_size.get_y())

    def get_programs_for_graph(nl, graph_name):
        programs = Netlist(nl).get_programs().clone()
        for program_name in programs.get_program_names():
            program = programs.get_program(program_name)
            program_instructions = program.get_instruction_names()
            index = 0
            run = True
            while (run):
                try:
                    next_index = program_instructions.index(NetlistConsts.INSTRUCTION_NAME_EXECUTE, index)
                    if program.get_instruction(next_index).get_graph_name() != graph_name:
                        program.remove(next_index)
                        program_instructions.remove(next_index)
                    else:
                        index = next_index + 1
                except:
                    run = False
            if index == 0:
                programs.remove_program(program_name)
        return programs

class BuildNetlist:
    def __init__(self, nl) -> None:
        self.__nl = Netlist(nl)
        self.__new_nl = self.__nl.clone()
        self.__created_input_queues = []
        pass

    def build_empty(self):
        self.__new_nl = NetlistBuilderHelper.build_empty_netlist_from_template(self.__nl)
        self.__created_input_queues = []

    def build_single_graph(self, graph_name):
        self.__new_nl = self.__nl.clone()
        self.__created_input_queues = []
        graph_names = self.__new_nl.get_graphs().get_graph_names()
        if graph_name not in graph_names:
            raise Exception(f"Graph [{graph_name}] is not in provided netlist.")

        #  delete graph names
        for name in graph_names:
            if name != graph_name:
                self.__new_nl.get_graphs().remove(name)

        # delete queues
        input_queues = self.__new_nl.get_graph_input_queue_names(graph_name)
        output_queues = self.__new_nl.get_graph_output_queue_names(graph_name)
        queue_names = self.__new_nl.get_queue_names()
        for q_name in queue_names:
            if q_name not in input_queues and q_name not in output_queues:
                self.__new_nl.get_queues().remove(q_name)

        # keep only programs for graph_name
        self.__new_nl.set_programs(NetlistBuilderHelper.get_programs_for_graph(self.__nl, graph_name))
        return self

    def remove_output_queues_for_graph(self, graph_name):
        out_queues = self.__new_nl.get_graph_output_queue_names(graph_name)
        for q_name in out_queues:
            self.__new_nl.get_queues().remove(q_name)
        return self

    def add_empty_graph(self, graph_name):
        g = self.__nl.get_graph(graph_name)
        empty_graph = NetlistBuilderHelper.build_empty_graph(g)
        self.__new_nl.add_graph(graph_name, empty_graph)

    def add_operation(self, graph_name, operation_name):
        self.__new_nl.add_operation(graph_name, operation_name, self.__nl.get_operation(graph_name, operation_name).clone())

    def replace_input(self, graph_name, operation_name, input_name, new_input_name):
        self.__new_nl.get_operation(graph_name, operation_name).replace_input_name(input_name, new_input_name)

    def add_inputs_to_operation(self, graph_name, operation_name):
        inputs = self.__nl.get_operation_inputs(graph_name, operation_name)
        for input_name in inputs:
            if isinstance(inputs[input_name], Queue):
                self.clone_input_queue(input_name)
            else:
                input_operation = Operation(inputs[input_name])
                queue_name = self.add_input_queue_for_operation(graph_name, input_name)
                new_input_op_name = self.add_input_datacopy_operation(graph_name, input_name, queue_name)
                self.replace_input(graph_name, operation_name, input_name, new_input_op_name)

    def clone_input_queue(self, queue_name):
        queue = self.__nl.get_queue(queue_name).clone()
        queue.set_input(NetlistConsts.QUEUE_INPUT_HOST)
        self.__new_nl.add_queue(queue_name, queue)

    def add_input_queue_for_operation(self, graph_name, operation_name):
        new_input_queue_name = NetlistBuilderHelper.get_queue_name_from_op_name(operation_name)
        new_input_queue = NetlistBuilderHelper.build_dram_queue_from_operation(
            self.__nl,
            graph_name,
            operation_name,
            NetlistConsts.QUEUE_INPUT_HOST)
        self.__new_nl.add_queue(new_input_queue_name, new_input_queue)
        self.__created_input_queues.append(new_input_queue_name)
        return new_input_queue_name

    def add_output_queue_for_operation(self, graph_name, operation_name, input_name):
        new_output_queue_name = NetlistBuilderHelper.get_queue_name_from_op_name(operation_name)
        new_output_queue = NetlistBuilderHelper.build_dram_queue_from_operation(
                    self.__nl,
                    graph_name,
                    operation_name,
                    input_name)
        self.__new_nl.add_queue(new_output_queue_name, new_output_queue)
        return new_output_queue_name

    def add_output_datacopy_for_operation(self, graph_name, operation_name):
        new_out_op_name = NetlistBuilderHelper.get_output_op_name_from_op_name(operation_name)

        grid_loc = NetlistBuilderHelper.calc_grid_location(
            self.__new_nl,
            graph_name,
            self.__new_nl.get_operation(graph_name, operation_name).get_grid_size())

        new_out_datacopy_op = NetlistBuilderHelper.create_datacopy_operation_from_op(
            self.__nl,
            graph_name,
            operation_name,
            operation_name,
            grid_loc)

        self.__new_nl.add_operation(graph_name, new_out_op_name, new_out_datacopy_op)
        return new_out_op_name

    def add_input_datacopy_operation(self, graph_name, operation_name, input_name):
        new_input_op_name = NetlistBuilderHelper.get_input_op_name_from_op_name(operation_name)
        new_input_datacopy_op = NetlistBuilderHelper.create_datacopy_operation_from_op(
            self.__nl,
            graph_name,
            operation_name,
            input_name,
            self.__nl.get_operation(graph_name, operation_name).get_grid_loc())
        self.__new_nl.add_operation(graph_name, new_input_op_name, new_input_datacopy_op)
        return new_input_op_name

    def add_outputs_to_operation(self, graph_name, operation_name):
        out_queue_input = operation_name

        shouldAddOutputDataCopyOperation = self.__nl.get_operation(graph_name, operation_name).get_ublock_order() != NetlistConsts.OPERATION_UBLOCK_ORDER_ROW
        if shouldAddOutputDataCopyOperation:
            data_copy_op_name = self.add_output_datacopy_for_operation(graph_name, operation_name)
            self.__new_nl.get_operation(graph_name, data_copy_op_name).set_ublock_order(NetlistConsts.OPERATION_UBLOCK_ORDER_ROW)
            out_queue_input = data_copy_op_name

        self.add_output_queue_for_operation(graph_name, operation_name, out_queue_input)

    def add_programs(self, graph_name):
        programs = NetlistBuilderHelper.get_programs_for_graph(self.__nl, graph_name)

        input_queue_names = self.__new_nl.get_graph_input_queue_names(graph_name)
        for program_name in programs.get_program_names():
            for execute_instruction in programs.get_program(program_name).get_execute_instuctions():
                queue_settings = execute_instruction.get_queue_settings()
                for input_queue_name in input_queue_names:
                    if not queue_settings.has(input_queue_name):
                        q_type = self.__new_nl.get_queue(input_queue_name).get_type()
                        ## TODO: Is this bug in netlist or functionality
                        if input_queue_name not in self.__created_input_queues:
                            continue
                        if (q_type != NetlistConsts.QUEUE_TYPE_QUEUE):
                            raise Exception(f"Missing functionality for queue {input_queue_name} type {q_type}")
                        queue_settings.add_queue_setting(input_queue_name, NetlistBuilderHelper.get_default_queue_setting(queue_settings))
                for q_name in queue_settings.get_queue_names():
                    if q_name not in input_queue_names:
                        queue_settings.remove(q_name)
        self.__new_nl.set_programs(programs)

    def allocate_dram(self):
        dram = DramFactory.create_grayskull_dram()
        queues = self.__new_nl.get_queues()
        queues_to_allocate = []
        for queue_name in queues.get_queue_names():
            try:
                queue = queues.get_queue(queue_name)
                if (queue.is_dram()):
                    memory = queue.get_memory()
                    for i in range(memory.get_slot_count()):
                        buffer_size = get_dram_queue_buffer_size(queue.get_mblock(), queue.get_ublock(), queue.get_t(), queue.get_df(), queue.get_entries())
                        dram.set_allocation(memory.get_slot(i).get_channel(), memory.get_slot(i).get_address(), buffer_size)
            except:
                queues_to_allocate.append(queue_name)

        for queue_name in queues_to_allocate:
            queue = queues.get_queue(queue_name)
            assert(queue.is_dram())
            buffer_size = get_dram_queue_buffer_size(queue.get_mblock(), queue.get_ublock(), queue.get_t(), queue.get_df(), queue.get_entries())
            memory = []
            for i in range(queue.get_grid_size().get_core_count()):
                channel, address = dram.allocate(buffer_size)
                memory.append([channel, address])
            queue.set_memory(memory)

    def replace_input_names(self, graph_name, op_name, new_op_name):
        for operation_name in self.__new_nl.get_operation_names(graph_name):
            if op_name in self.__new_nl.get_operation_inputs(graph_name, operation_name):
                self.__new_nl.get_operation(graph_name, operation_name).replace_input_name(op_name, new_op_name)
        return self

    def remove_operation(self, graph_name, operation_name):
        # first
        inputs_and_queue_names = self.__new_nl.get_queues().get_inputs_and_queue_names()
        operation_inputs = self.__new_nl.get_operation_inputs(graph_name, operation_name)
        for input_operation in operation_inputs:
            if type(operation_inputs[input_operation]) is Operation:
                fan_out_ops = self.__new_nl.get_graph(graph_name).get_operation_names_for_input(input_operation)
                if operation_name not in fan_out_ops:
                    raise Exception(f"Op that is removing {operation_name}")
                if len(fan_out_ops) == 1 and input_operation not in inputs_and_queue_names: # input is dependent only on this op
                    # we can remove child op safely
                    self.remove_operation(graph_name, input_operation)
        for input_operation in operation_inputs:
            if type(operation_inputs[input_operation]) is Queue:
                fan_out_ops = self.__new_nl.get_graph(graph_name).get_operation_names_for_input(input_operation)
                assert(operation_name in fan_out_ops)
                if len(fan_out_ops) == 1: # only one op depends on this queue
                    self.__new_nl.get_queues().remove(input_operation)

        self.__new_nl.get_graph(graph_name).remove(operation_name)

    def remove_operations_without_output(self, graph_name):
        operations_to_remove = []
        inputs_and_queue_names = self.__new_nl.get_queues().get_inputs_and_queue_names()
        for operation_name in self.__new_nl.get_operation_names(graph_name):
            if not self.__new_nl.get_graph(graph_name).get_operation_names_for_input(operation_name):
                if operation_name not in inputs_and_queue_names:
                    operations_to_remove.append(operation_name)

        for operation_name in operations_to_remove:
            self.remove_operation(graph_name, operation_name)

        return self

    def get_netlist(self):
        return self.__new_nl

    ### Static functions 
    def build_netlist_single_op(netlist, graph_name, operation_name):
        builder = BuildNetlist(netlist)
        builder.build_empty()
        builder.add_empty_graph(graph_name)
        builder.add_operation(graph_name, operation_name)
        builder.add_inputs_to_operation(graph_name, operation_name)
        builder.add_outputs_to_operation(graph_name, operation_name)
        builder.allocate_dram()
        builder.add_programs(graph_name)
        return builder.get_netlist()

    def build_netlist_tapout_all_operations(netlist):
        builder = BuildNetlist(netlist)
        graphs = builder.get_netlist().get_graphs()
        queue_inputs = builder.get_netlist().get_queues().get_queue_input_names()

        for graph_name in graphs.get_graph_names():
            for op_name in graphs.get_graph(graph_name).get_operation_names():
                if op_name not in queue_inputs:
                    builder.add_outputs_to_operation(graph_name, op_name)

        builder.allocate_dram()

        return builder.get_netlist()

    def build_netlist_tapout_single_op(netlist, graph_name, op_name):
        builder = BuildNetlist(netlist)
        builder.add_outputs_to_operation(graph_name, op_name)
        builder.allocate_dram()
        return builder.get_netlist()