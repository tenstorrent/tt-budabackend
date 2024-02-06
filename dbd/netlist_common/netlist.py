from copy import deepcopy

def get_dict(obj):
    if isinstance(obj,dict):
        return obj
    if isinstance(obj, Dict):
        return obj.get_raw()
    raise Exception(f"Object {obj} is {type(obj)}. It should be Dict or dict.")

def get_list(obj):
    if isinstance(obj,list):
        return obj
    if isinstance(obj, List):
        return obj.get_raw()
    raise Exception(f"Object {obj} is {type(obj)}. It should be List or list.")

class NetlistConsts:
    DEVICES = "devices"
    QUEUES = "queues"
    GRAPHS = "graphs"
    PROGRAMS = "programs"
    DEVICES_COUNT = "count"
    DEVICES_ARCH = "arch"
    DEVICES_ARCH_GRAYSKULL = "grayskull"
    DEVICES_ARCH_WORMHOLE = "wormhole"
    QUEUE_INPUT = "input"
    QUEUE_INPUT_HOST = "HOST"
    QUEUE_TYPE = "type"
    QUEUE_TYPE_QUEUE = "queue"
    QUEUE_ENTRIES = "entries"
    QUEUE_LOCATION = "loc"
    QUEUE_LOCATION_DRAM = "dram"
    QUEUE_DRAM = "dram"
    QUEUE_TARGET_DEVICE = "target_device"
    QUEUE_DATA_FORMAT = "df"
    QUEUE_GRID_SIZE = "grid_size"
    QUEUE_MBLOCK = "mblock"
    QUEUE_UBLOCK = "ublock"
    QUEUE_T = "t"
    GRAPH_TARGET_DEVICE = "target_device"
    GRAPH_INPUT_COUNT = "input_count"
    OPERATION_OUT_DF = "out_df"
    OPERATION_IN_DF = "in_df"
    OPERATION_GRID_LOC = "grid_loc"
    OPERATION_GRID_SIZE = "grid_size"
    OPERATION_MBLOCK = "mblock"
    OPERATION_UBLOCK = "ublock"
    OPERATION_T = "t"
    OPERATION_INPUT_NAMES = "inputs"
    OPERATION_UBLOCK_ORDER = "ublock_order"
    OPERATION_UBLOCK_ORDER_ROW = "r"
    OPERATION_TYPE = "type"
    OPERATION_TYPE_DATACOPY = "datacopy"
    OPERATION_ACC_DF = "acc_df"
    OPERATION_INTERMED_DF=  "intermed_df"
    OPERATION_BUF_SIZE_MB = "buf_size_mb"
    OPERATION_MATH_FIDELITY=  "math_fidelity"
    OPERATION_UNTILIZE_OUTPUT= "untilize_output"
    INSTRUCTION_NAME_EXECUTE = "execute"
    INSTRUCTION_NAME_LOOP = "loop"
    INSTRUCTION_NAME_ENDLOOP = "endloop"
    INSTRUCTION_EXECUTE_GRAPH_NAME = "graph_name"
    INSTRUCTION_EXECUTE_QUEUE_SETTINGS = "queue_settings"
    INSTRUCTION_EXECUTE_QUEUE_SETTINGS_WR_PTR_GLOBAL ="wr_ptr_global"
    INSTRUCTION_EXECUTE_QUEUE_SETTINGS_PROLOGUE = "prologue"
class Var:
    def __init__(self, var) -> None:
        self.__var = var

    def get_raw(self):
        return self.__var

    def clone(self):
        return deepcopy(self)

class List(Var):
    def __init__(self, l) -> None:
        super().__init__(get_list(l))

    def len(self):
        return len(self.get_raw())

    def index(self, value):
        return self.get_raw().index(value)

    def get_item(self, index):
        return self.get_raw()[index]

    def set_item(self, index, value):
        self.get_raw()[index] = value

    def set(self, value):
        self.get_raw().clear()
        self.get_raw().extend(get_list(value))

    def clear(self):
        self.get_raw().clear()

    def remove(self, index):
        del self.get_raw()[index]

class Dict(Var):
    def __init__(self, d) -> None:
        super().__init__(get_dict(d))

    def get_keys(self):
        return list(self.get_raw().keys())

    def get_values(self):
        return self.get_raw().values()

    def get_value(self, key):
        assert(key in self.get_raw())
        return self.get_raw()[key]

    def add(self, key, value):
        assert(key not in self.get_raw())
        self.get_raw()[key] = value

    def set(self, key, value):
        assert(key in self.get_raw())
        self.get_raw()[key] = value

    def has(self, key):
        return key in self.get_keys()

    def remove(self, key):
        del self.get_raw()[key]

    def clear(self):
        self.get_raw().clear()

class Operation(Dict):
    def __init__(self, nl_op) -> None:
        super().__init__(nl_op)
    def set_in_df(self, in_formats):
        self.set(NetlistConsts.OPERATION_IN_DF, in_formats)
    def set_out_df(self, value):
        return self.set(NetlistConsts.OPERATION_OUT_DF, value)
    def get_out_df(self):
        return self.get_value(NetlistConsts.OPERATION_OUT_DF)
    def get_grid_loc(self):
        return GridLoc(self.get_value(NetlistConsts.OPERATION_GRID_LOC))
    def set_grid_loc(self, grid_loc):
        self.set(NetlistConsts.OPERATION_GRID_LOC, get_list(grid_loc))
    def get_grid_size(self):
        return GridSize(self.get_value(NetlistConsts.OPERATION_GRID_SIZE))
    def set_grid_size(self, gs):
        self.set(NetlistConsts.OPERATION_GRID_SIZE, get_list(gs))
    def get_mblock(self):
        return self.get_value(NetlistConsts.OPERATION_MBLOCK)
    def set_mblock(self, mblock):
        self.set(NetlistConsts.OPERATION_MBLOCK, mblock)
    def get_ublock(self):
        return self.get_value(NetlistConsts.OPERATION_UBLOCK)
    def set_ublock(self, ublock):
        return self.set(NetlistConsts.OPERATION_UBLOCK, ublock)
    def get_t(self):
        return self.get_value(NetlistConsts.OPERATION_T)
    def set_t(self, t):
        self.set(NetlistConsts.OPERATION_T, t)
    def get_input_names(self):
        return self.get_value(NetlistConsts.OPERATION_INPUT_NAMES)
    def set_input_names(self, inputs):
        self.set(NetlistConsts.OPERATION_INPUT_NAMES, inputs)
    def replace_input_name(self, old_input_name, new_input_name):
        for i in range(len(self.get_input_names())):
            if self.get_input_names()[i] == old_input_name:
                self.get_input_names()[i] = new_input_name
    def get_ublock_order(self):
        return self.get_value(NetlistConsts.OPERATION_UBLOCK_ORDER)
    def set_ublock_order(self, ublock_order):
        self.set(NetlistConsts.OPERATION_UBLOCK_ORDER, ublock_order)
    def set_type(self, type):
        self.set(NetlistConsts.OPERATION_TYPE, type)

    def get_acc_df(self):
        return self.get_value(NetlistConsts.OPERATION_ACC_DF)
    def set_acc_df(self, acc_df):
        self.set(NetlistConsts.OPERATION_ACC_DF, acc_df)

    def get_intermed_df(self):
        return self.get_value(NetlistConsts.OPERATION_INTERMED_DF)
    def set_intermed_df(self, inetrmed_df):
        self.set(NetlistConsts.OPERATION_INTERMED_DF, inetrmed_df)

    def get_buf_size_mb(self):
        return self.get_value(NetlistConsts.OPERATION_BUF_SIZE_MB)
    def set_buf_size_mb(self, buf_size_mb):
        self.set(NetlistConsts.OPERATION_BUF_SIZE_MB, buf_size_mb)

    def get_math_fidelity(self):
        return self.get_value(NetlistConsts.OPERATION_MATH_FIDELITY)
    def set_math_fidelity(self, math_fidelity):
        self.set(NetlistConsts.OPERATION_MATH_FIDELITY, math_fidelity)

    def get_untilize_output(self):
        return self.get_value(NetlistConsts.OPERATION_UNTILIZE_OUTPUT)
    def set_set_untilize_output(self, untilize_output):
        self.set(NetlistConsts.OPERATION_UNTILIZE_OUTPUT, untilize_output)
class Graph(Dict):
    No_Op = set([NetlistConsts.GRAPH_TARGET_DEVICE, NetlistConsts.GRAPH_INPUT_COUNT])

    def __init__(self, nl_graph) -> None:
        super().__init__(nl_graph)

    def create_graph(target_device, input_count) -> None:
        return Graph({NetlistConsts.GRAPH_TARGET_DEVICE:target_device, NetlistConsts.GRAPH_INPUT_COUNT:input_count})

    def get_target_device(self):
        return self.get_value(NetlistConsts.GRAPH_TARGET_DEVICE)

    def get_input_count(self):
        return self.get_value(NetlistConsts.GRAPH_INPUT_COUNT)

    def get_operation_names(self):
        return set(self.get_keys()) - Graph.No_Op

    def get_operation(self, operation_name):
        return Operation(self.get_value(operation_name))

    def add_operation(self, operation_name, operation):
        self.add(operation_name, get_dict(operation))

    def get_operation_names_for_input(self, input_name):
        operation_names = []
        for operation_name in self.get_operation_names():
            if input_name in self.get_operation(operation_name).get_input_names():
                operation_names.append(operation_name)
        return operation_names

class Graphs(Dict):
    def __init__(self, nl_graphs) -> None:
        super().__init__(nl_graphs)

    def get_graph_names(self):
        return self.get_keys()

    def get_graph(self, graph_name):
        return Graph(self.get_value(graph_name))

    def add_graph(self, graph_name, graph):
        self.add(graph_name, get_dict(graph))

    def get_graph_name(self, operation_name):
        for graph_name in self.get_graph_names():
            if operation_name in self.get_graph(graph_name).get_operation_names():
                return graph_name
        raise Exception(f"Cannot find operation [{operation_name}] in netlist.")

class GridLoc(List):
    X = 0
    Y = 1
    def __init__(self, nl_grid) -> None:
        assert(len(nl_grid) == 2)
        super().__init__(nl_grid)

    def get_x(self):
        return self.get_item(GridLoc.X)

    def get_y(self):
        return self.get_item(GridLoc.Y)

class GridSize(List):
    X = 0
    Y = 1
    def __init__(self, nl_grid) -> None:
        assert(len(nl_grid) == 2)
        super().__init__(nl_grid)

    def get_x(self):
        return self.get_raw()[GridSize.X]

    def get_y(self):
        return self.get_raw()[GridSize.Y]

    def get_core_count(self):
        return self.get_x() * self.get_y()

class QueueDramMemorySlot(List):
    CHANNEL_IDX = 0
    ADDRESS_IDX = 1
    def __init__(self, nl_slot):
        assert(len(nl_slot) == 2)
        super().__init__(nl_slot)

    def get_address(self):
        return self.get_item(QueueDramMemorySlot.ADDRESS_IDX)

    def get_channel(self):
        return self.get_item(QueueDramMemorySlot.CHANNEL_IDX)

class QueueDramMemory(List):
    def __init__(self, nl_memory) -> None:
        super().__init__(nl_memory)

    def get_slot_count(self):
        return self.len()

    def get_slot(self, slot_id):
        assert(slot_id < self.get_slot_count())
        return QueueDramMemorySlot(self.get_item(slot_id))

class Queue(Dict):
    def __init__(self, nl_queue) -> None:
        super().__init__(nl_queue)

    def get_input(self):
        return self.get_value(NetlistConsts.QUEUE_INPUT)

    def set_input(self, input_name):
        self.set(NetlistConsts.QUEUE_INPUT, input_name)

    def get_type(self):
        return self.get_value(NetlistConsts.QUEUE_TYPE)

    def get_entries(self):
        return self.get_value(NetlistConsts.QUEUE_ENTRIES)

    def get_location(self):
        return self.get_value(NetlistConsts.QUEUE_LOCATION)

    def is_dram(self):
        return self.get_location() == NetlistConsts.QUEUE_LOCATION_DRAM

    def get_target_device(self):
        return self.get_value(NetlistConsts.QUEUE_TARGET_DEVICE)

    def get_memory(self):
        if (self.is_dram()):
            return QueueDramMemory(self.get_value(NetlistConsts.QUEUE_DRAM))

        return List(self.get_value(self.get_location()))

    def set_memory(self, memory):
        assert(self.is_dram())
        self.set(NetlistConsts.QUEUE_DRAM, memory)

    def get_df(self):
        return self.get_value(NetlistConsts.QUEUE_DATA_FORMAT)

    def get_grid_size(self):
        return GridSize(self.get_value(NetlistConsts.QUEUE_GRID_SIZE))

    def get_mblock(self):
        return self.get_value(NetlistConsts.QUEUE_MBLOCK)

    def get_ublock(self):
        return self.get_value(NetlistConsts.QUEUE_UBLOCK)

    def get_t(self):
        return self.get_value(NetlistConsts.QUEUE_T)

class Queues(Dict):
    def __init__(self, nl_queues) -> None:
        super().__init__(nl_queues)

    def get_queue_names(self):
        return self.get_keys()

    def get_queue(self, queue_name):
        return Queue(self.get_value(queue_name))

    def add_queue(self, queue_name, queue):
        self.add(queue_name, get_dict(queue))

    def get_max_entries(self):
        return max([Queue(value).get_entries() for value in self.get_values()])

    def get_queue_input_names(self):
        input_names = set()
        for queue_name in self.get_queue_names():
            input_names.add(self.get_queue(queue_name).get_input())
        return input_names

    def get_inputs_and_queue_names(self):
        d = dict()
        for queue_name in self.get_queue_names():
            input = self.get_queue(queue_name).get_input()
            if input in d:
                d[input].add(queue_name)
            else:
                d[input] = set()
                d[input].add(queue_name)
        return d

class Devices(Dict):
    def __init__(self, nl_devices):
        super().__init__(nl_devices)

    def get_count(self):
        return self.get_value(NetlistConsts.DEVICES_COUNT)

    def get_arch(self):
        arch = self.get_value(NetlistConsts.DEVICES_ARCH)
        if isinstance(arch, list):
            return arch
        return [arch]

    def is_arch_supported(self, arch_name):
        return arch_name in self.get_arch()

    def is_grayskull_supported(self):
        return self.is_arch_supported(NetlistConsts.DEVICES_ARCH_GRAYSKULL)

    def is_wormhole_supported(self):
        return self.is_arch_supported(NetlistConsts.DEVICES_ARCH_WORMHOLE)

class Instruction:
    def __init__(self, name, instruction) -> None:
        self.__name = name
        self.__instruction = instruction

    def get_name(self):
        return self.__name

    def get_instruction(self):
        return self.__instruction

class ExecuteInstruction(Dict):
    def __init__(self, name, d) -> None:
        assert(name == NetlistConsts.INSTRUCTION_NAME_EXECUTE)
        super(Dict, self).__init__(d)
        self.__name = name

    def get_name(self):
        return self.__name

    def get_graph_name(self):
        return self.get_value(NetlistConsts.INSTRUCTION_EXECUTE_GRAPH_NAME)

    def get_queue_settings(self):
        return QueueSettings(self.get_value(NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS))

class QueueSetting(Dict):
    def __init__(self, d) -> None:
        super().__init__(d)

class QueueSettings(Dict):
    def __init__(self, d) -> None:
        super().__init__(d)

    def get_queue_names(self):
        return list(self.get_keys())

    def get_queue_setting(self, name):
        return QueueSetting(self.get_value(name))

    def add_queue_setting(self, name, setting):
        self.add(name, get_dict(setting))

class Program(List):
    def __init__(self, l) -> None:
        super().__init__(l)

    def get_instruction(self, index):
        instruction = self.get_item(index)
        if isinstance(instruction, str):
            return Instruction(instruction, None)
        else:
            name = list(instruction.keys())[0]
            if (name == NetlistConsts.INSTRUCTION_NAME_EXECUTE):
                return ExecuteInstruction(name, instruction[name])
            else:
                return Instruction(name, instruction[name])

    def get_instruction_names(self):
        return [self.get_instruction(i).get_name() for i in range(self.len())]

    def get_execute_instuctions(self):
        return [self.get_instruction(i) for i in range(self.len()) if self.get_instruction(i).get_name() == NetlistConsts.INSTRUCTION_NAME_EXECUTE]

class ProgramsDict(Dict):
    def __init__(self, d) -> None:
        super().__init__(d)

    def get_program_names(self):
        return list(self.get_keys())

    def get_program(self, key):
        return Program(self.get_value(key))

class ProgramsList(List):
    def __init__(self, nl_programs) -> None:
        super().__init__(nl_programs)

    def get_program_names(self):
        names = []
        for i in range(self.len()):
            names.extend(self.get_programs_dict(i).get_program_names())
        return names

    def get_programs_dict(self, index):
        return ProgramsDict(self.get_item(index))

    def remove_program(self, program_name):
        i = self.get_index(program_name)
        self.remove(i)

    def get_index(self, program_name):
        for i in range(self.len()):
            if self.get_programs_dict(i).has(program_name):
                return i
        raise Exception(f"Program with name {program_name} does not exist")

    def get_program(self, program_name):
        return self.get_programs_dict(self.get_index(program_name)).get_program(program_name)

class Netlist(Dict):
    def __init__(self, nl) -> None:
        super().__init__(nl)

    def get_devices(self):
        return Devices(self.get_value(NetlistConsts.DEVICES))

    def get_queues(self):
        return Queues(self.get_value(NetlistConsts.QUEUES))

    def get_queue_names(self):
        return self.get_queues().get_queue_names()

    def get_queue(self, queue_name):
        return self.get_queues().get_queue(queue_name)

    def add_queue(self, queue_name, queue):
        return self.get_queues().add_queue(queue_name, queue)

    def get_graphs(self):
        return Graphs(self.get_value(NetlistConsts.GRAPHS))

    def get_graph(self, graph_name):
        return self.get_graphs().get_graph(graph_name)

    def add_graph(self, graph_name, graph):
        self.get_graphs().add_graph(graph_name, graph)

    def get_operation(self, graph_name, operation_name):
        return self.get_graph(graph_name).get_operation(operation_name)

    def get_operation_names(self, graph_name):
        return self.get_graph(graph_name).get_operation_names()

    def add_operation(self, graph_name, operation_name, operation):
        self.get_graph(graph_name).add_operation(operation_name, operation)

    def get_graph_output_queue_names(self, graph_name):
        result = []
        d = self.get_queues().get_inputs_and_queue_names()
        for operation_name in self.get_graph(graph_name).get_operation_names():
            if operation_name in d:
                result.extend(list(d[operation_name]))
        return result

    def get_operation_inputs(self, graph_name, operation_name):
        graph = self.get_graph(graph_name)
        op = graph.get_operation(operation_name)
        inputs = {}
        for input_name in op.get_input_names():
            if graph.has(input_name):
                inputs[input_name] = graph.get_operation(input_name)
            else:
                inputs[input_name] = self.get_queues().get_queue(input_name)
        return inputs

    def get_graph_input_queue_names(self, graph_name):
        g = self.get_graph(graph_name)
        inputs = {}
        for op_name in g.get_operation_names():
            inputs.update(self.get_operation_inputs(graph_name, op_name))
        return [input_name for input_name in inputs if isinstance(inputs[input_name], Queue)]

    def get_programs(self):
        return ProgramsList(self.get_value(NetlistConsts.PROGRAMS))

    def set_programs(self, programs):
        self.set(NetlistConsts.PROGRAMS, get_list(programs))

    def get_section(self, section_name):
        if section_name == NetlistConsts.DEVICES:
            return self.get_devices()
        elif section_name == NetlistConsts.QUEUES:
            return self.get_queues()
        elif section_name == NetlistConsts.GRAPHS:
            return self.get_graphs()
        elif section_name == NetlistConsts.PROGRAMS:
            return self.get_programs()
        else:
            return Dict(self.get_value(section_name))

    def get_section_names(self):
        return self.get_keys()

    def __str__(self):
        return NetlistWriter(self).to_string()

class NetlistWriter:
    SINGLE_SPACE = "  "

    def __init__(self, nl) -> None:
        self.__mynl = Netlist(nl)

    def list_to_str_inline(l):
        return "["+", ".join(NetlistWriter.to_str_inline(e) for e in l) + "]"

    def dict_to_str_inline(d):
        if len(d) == 1 and d[list(d.keys())[0]] is None:
            return "{" +str(list(d.keys())[0]) + "}"

        return "{" + ", ".join(str(e) + ": " + NetlistWriter.to_str_inline(d[e]) for e in d) + "}"

    def to_str_inline(e):
        if e is None:
            return ""

        if isinstance(e,bool):
            return "true" if e else "false"

        if isinstance(e,dict):
            return NetlistWriter.dict_to_str_inline(e)

        if isinstance(e, list) and not isinstance(e, str):
            return NetlistWriter.list_to_str_inline(e)

        return f"{e}"

    def dict_as_inline_list(d, prefix):
        return "\n".join([f"{prefix}{element}: {NetlistWriter.to_str_inline(d[element])}" for element in d]) + "\n"

    def default_section_to_str(section, space=""):
        return NetlistWriter.dict_as_inline_list(get_dict(section), space + NetlistWriter.SINGLE_SPACE)

    def queues_to_str(self):
        myQueues = self.__mynl.get_queues().clone()
        for q_name in myQueues.get_queue_names():
            memory_list = myQueues.get_queue(q_name).get_memory().get_raw()
            for cnt in range(0, len(memory_list)):
                if isinstance(memory_list[cnt], list):
                    memory_list[cnt][1] = f"0x{memory_list[cnt][1]:08x}"
                else:
                    memory_list[cnt] = f"0x{memory_list[cnt]:08x}"
        return NetlistWriter.default_section_to_str(myQueues.get_raw())

    def graphs_to_str(self):
        g = self.__mynl.get_graphs()
        return "\n".join([f"{NetlistWriter.SINGLE_SPACE}{graph_name}:\n{NetlistWriter.default_section_to_str(g.get_graph(graph_name).get_raw(), NetlistWriter.SINGLE_SPACE)}" for graph_name in g.get_graph_names()]) + "\n"

    def programs_to_str(self):
        program_list = self.__mynl.get_programs()
        s = ""
        for program_name in program_list.get_program_names():
            program = program_list.get_program(program_name)
            s = s + NetlistWriter.SINGLE_SPACE + "- " + program_name + ":\n"
            for i in range(program.len()):
                instruction = program.get_instruction(i)
                s = s + "      - " + instruction.get_name()
                if instruction.get_name() == NetlistConsts.INSTRUCTION_NAME_EXECUTE:
                    s = s+ ": {" + NetlistConsts.INSTRUCTION_EXECUTE_GRAPH_NAME+ ": " + instruction.get_graph_name() + ", " + NetlistConsts.INSTRUCTION_EXECUTE_QUEUE_SETTINGS + ": {\n"
                    for queue_name in instruction.get_queue_settings().get_queue_names():
                        s = s +"        "+queue_name+": " + NetlistWriter.to_str_inline(instruction.get_queue_settings().get_queue_setting(queue_name).get_raw()) + ",\n"
                    s = s[:-2] + "}}"
                else:
                    s = s + ("" if instruction.get_instruction() is None else ": " + NetlistWriter.to_str_inline(instruction.get_instruction()))
                s = s + "\n"
            s = s + "\n"
        return s

    def section_to_str(self, section_name):
        if section_name == NetlistConsts.QUEUES:
            return self.queues_to_str()
        elif section_name == NetlistConsts.GRAPHS:
            return self.graphs_to_str()
        elif section_name == NetlistConsts.PROGRAMS:
            return self.programs_to_str()
        else:
            return NetlistWriter.default_section_to_str(self.__mynl.get_section(section_name))

    def to_string(self):
        return "\n\n".join([f"{section_name}:\n{self.section_to_str(section_name)}" for section_name in self.__mynl.get_section_names()])