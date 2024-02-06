#!/usr/bin/env python3
"""
tapout.py parses netlist and generates new queues.
"""
import argparse, subprocess, os
from collections import defaultdict

from netlist_common.netlistbuilder import BuildNetlist, NetlistBuilderHelper

class TopologicalSort:
    def __init__(self):
        self.graph = defaultdict(list)

    def add_edge(self,u,v):
        self.graph[u].append(v)
        self.graph[v]

    def __sort(self,v,visited,sorted_vertices):
        visited[v] = True

        for i in self.graph[v]:
            if visited[i] == False:
                self.__sort(i,visited,sorted_vertices)

        sorted_vertices.insert(0,v)

    def sort(self):
        visited = {}
        for k in self.graph:
            visited[k]=False

        sorted_vertices =[]
        for i in self.graph:
            if visited[i] == False:
                self.__sort(i,visited,sorted_vertices)

        return sorted_vertices
class TestCommand:
    def __init__(self, command) -> None:
        self.__command = command.split()
        self.__out_dir = ""
        self.__log_filename = ""

    # netlist filename is after --netlist string
    def __get_netlist_index(self):
        return self.__command.index("--netlist") + 1

    def get_netlist(self):
        return self.__command[self.__get_netlist_index()]

    def set_netlist(self, netlist):
        self.__command[self.__get_netlist_index()] = netlist

    def set_command(self, command):
        self.__command = command.split()

    def get_command(self):
        return self.__command

    def get_command_as_string(self):
        return " ".join(self.__command)

    def set_out_dir(self, out_dir):
        self.__out_dir = out_dir

    def get_out_dir(self):
        return self.__out_dir

    def set_log_filename(self, log_filename):
        self.__log_filename = log_filename

    def get_log_filename(self):
        return self.__log_filename

class CommandExecutor:
    def __init__(self, log_to_console = True) -> None:
        self.__log_to_console = log_to_console
        self.__line_handler = None
        self.__exception_handler = None
        self.__log_filename = None
        self.__log_file = None

    def set_logfile(self, log_filename):
        self.__log_filename = log_filename
        self.__log_file = open(log_filename, "w")

    def get_logfilename(self):
        return self.__log_filename

    def set_line_handler(self, line_handler):
        self.__line_handler = line_handler
        return self

    def set_exception_handler(self, exception_handler):
        self.__exception_handler = exception_handler
        return self

    def __log(self, line):
        if self.__log_to_console:
            print(line, end ="")
        if self.__log_file is not None:
            self.__log_file.write(line)

    def __handle_line(self, line):
        self.__log(line)
        if self.__line_handler is not None:
            self.__line_handler(line)

    def __handle_exception(self, e):
        self.__log(repr(e))
        if self.__line_handler is not None:
            self.__exception_handler(e)

    def execute(self, command):
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
            try:
                while True:
                    line = proc.stdout.readline()
                    if not line:
                        break

                    ln = line.decode("utf-8")

                    self.__handle_line(ln)
            except Exception as e:
                proc.terminate()
                self.__handle_exception(e)
            return proc.returncode
class CommandHandlerList:
    def __init__(self) -> None:
        self.__exception_handlers = []
        self.__line_handlers = []

    def add_line_handler(self, handler):
        self.__line_handlers.append(handler)
        return self

    def add_exception_handler(self, handler):
        self.__exception_handlers.append(handler)
        return self

    def handle_line(self, line):
        for handler in self.__line_handlers:
            handler(line)

    def handle_exception(self, e):
        for handler in self.__exception_handlers:
            handler(e)

class TileMismatchParser:
    def __init__(self) -> None:
        self.clear()
        self.__MAX_LINES = 100

    def __is_mismatch_start_detected(self, line):
        return "First Mismatched Tile for Tensor" in line

    def __is_mismatch_end_detected(self, line):
        matches = ["Error", "Queue:"]
        return all(x in line for x in matches)

    def clear(self):
        self.__in_processing = False
        self.__mismatch_found = False
        self.__lines = []

    def is_mismatch_found(self):
        return self.__mismatch_found

    def get_lines(self):
        return self.__lines

    def process_line(self, line):
        if self.__is_mismatch_start_detected(line):
            self.__in_processing = True
            self.__mismatch_found = False
            self.__lines = []

        if self.__in_processing:
            self.__lines.append(line)
            if len(self.__lines) > self.__MAX_LINES:
                # Exception("Parsing mismatched tiles has more lines than expected")
                self.__in_processing = False
                self.__mismatch_found = False

        if self.__is_mismatch_end_detected(line):
            if not self.__in_processing:
                raise Exception("Mismatch start has not been detected")
            self.__in_processing = False
            self.__mismatch_found = True

class TapoutOperation:
    def __init__(self, graph_name, operation_name) -> None:
        self.__graph_name = graph_name
        self.__operation_name = operation_name
        self.__mismatch = []
        self.__mismatch_parser = TileMismatchParser()
        self.__operation_failed = False
        self.__operation_finished = False # if true tapout operation is detected in log
        self.__test_failed = False
        self.__test_execution_failed = False
        pass

    def get_graph_name(self):
        return self.__graph_name

    def get_operation_name(self):
        return self.__operation_name

    def get_tapout_queue_name(self):
        return NetlistBuilderHelper.get_queue_name_from_op_name(self.__operation_name)

    def process_line_from_log(self, line):
        if self.get_tapout_queue_name() in line:
            self.__operation_finished = True

        self.__mismatch_parser.process_line(line)

        if self.__mismatch_parser.is_mismatch_found():
            if self.get_tapout_queue_name() in line:
                self.__operation_failed = True
                self.__mismatch.append(self.get_tapout_queue_name() + "\n")
                self.__mismatch.append("".join(self.__mismatch_parser.get_lines()))
            self.__mismatch_parser.clear()

    def set_failed(self):
        self.__test_failed = True

    def process_exception(self, e):
        if "what():  Test Failed" in repr(e):
            self.__test_failed = True
        else:
            self.__test_execution_failed = True

    def get_mismatch(self):
        return self.__mismatch

    def get_operation_result(self):
        if self.__operation_finished:
                return "FAILED" if self.__operation_failed else "PASSED"
        else:
            if self.__test_execution_failed:
                return "COMMAND FAILURE (For more information look at log file)"
            else:
                return "TAPOUT OUTPUT NOT PROCESSED"

class TapoutCommandExecutor:
    def __init__(self, command, out_dir) -> None:
        self.__out_dir = out_dir
        if not os.path.exists(self.__out_dir):
            os.makedirs(self.__out_dir)
        self.__test_command = TestCommand(command)

        self.__ferrors = open(self.get_op_errors_log(), "w")
        self.__result_file = open(self.get_run_result_log(), "w")
        self.__netlist_filename = self.__test_command.get_netlist()
        self.__netlist = NetlistBuilderHelper.build_netlist_from_file(self.__netlist_filename)


    def get_run_result_log(self):
        return f"{self.__out_dir}/tapout_result.log"

    def get_op_errors_log(self):
        return f"{self.__out_dir}/op_errors.log"

    def reset():
        CommandExecutor().execute(["device/bin/silicon/reset.sh"])

    def get_tapout_operations(self):
        queue_input_names = self.__netlist.get_queues().get_queue_input_names()
        tapout_operations = []
        for graph_name in self.__netlist.get_graphs().get_graph_names():
            topo_sort = TopologicalSort()
            for operation_name in self.__netlist.get_graphs().get_graph(graph_name).get_operation_names():
                inputs = self.__netlist.get_operation_inputs(graph_name, operation_name)
                for input_name in inputs:
                    topo_sort.add_edge(input_name, operation_name)
            sorted_inputs = topo_sort.sort()
            for input_name in sorted_inputs:
                if input_name not in queue_input_names and input_name not in self.__netlist.get_queues().get_queue_names():
                    tapout_operations.append(TapoutOperation(graph_name, input_name))
        return tapout_operations

    def log(self, tapout_operation):
        self.__ferrors.write(tapout_operation.get_tapout_queue_name() + "\n")
        self.__ferrors.writelines("".join(tapout_operation.get_mismatch()))
        self.__ferrors.flush()

        self.__result_file.write(f'op_name: {tapout_operation.get_tapout_queue_name()}\n\tCommand: {self.__test_command.get_command_as_string()}\n\tLog: {self.__test_command.get_log_filename()}\n\t')
        self.__result_file.write(f'Result: {tapout_operation.get_operation_result()}\n')
        self.__result_file.flush()

    def build_single_op_netlist(self, operation, filename):
        nl = BuildNetlist.build_netlist_single_op(self.__netlist, operation.get_graph_name(), operation.get_operation_name())
        self.__test_command.set_netlist(filename)
        NetlistBuilderHelper.save_netlist_to_file(filename, nl)

    def build_netlist(self, tapout_operations, filename):
        builder = BuildNetlist(self.__netlist)
        for tapout_operation in tapout_operations:
            builder.add_outputs_to_operation(tapout_operation.get_graph_name(), tapout_operation.get_operation_name())
        builder.allocate_dram()

        self.__test_command.set_netlist(filename)
        NetlistBuilderHelper.save_netlist_to_file(filename, builder.get_netlist())

    def build_command_executor(self, tapout_operations, log_filename):
        command_executor = CommandExecutor()
        command_executor.set_logfile(log_filename)

        handlers = CommandHandlerList()
        for operation in tapout_operations:
            handlers.add_line_handler(operation.process_line_from_log)
            handlers.add_exception_handler(operation.process_exception)

        command_executor.set_line_handler(handlers.handle_line)
        command_executor.set_exception_handler(handlers.handle_exception)
        return command_executor

    def run_tapout_operations(self, tapout_operations, log_filename):
        self.__test_command.set_log_filename(log_filename)
        command_executor = self.build_command_executor(tapout_operations, log_filename)
        result = command_executor.execute(self.__test_command.get_command())

        for tapout_operation in tapout_operations:
            if result != 0:
                tapout_operation.set_failed()

            self.log(tapout_operation)

    def run(self, one_run = False, step_by_step = False):
        # commands
        tapout_operations = self.get_tapout_operations()

        if not one_run:
            for operation in tapout_operations:
                new_netlist_filename = f"{self.__out_dir}/netlist_{operation.get_tapout_queue_name()}.yaml"
                log_filename = f"{self.__out_dir}/{operation.get_tapout_queue_name()}.console.log"
                if step_by_step:
                    self.build_single_op_netlist(operation, new_netlist_filename)
                else:
                    self.build_netlist([operation], new_netlist_filename)

                self.run_tapout_operations([operation], log_filename)
        else:
            new_netlist_filename = f"{self.__out_dir}/modified.netlist.yaml"
            log_filename = f"{self.__out_dir}/out.log"
            self.build_netlist([operation], new_netlist_filename)
            self.run_tapout_operations(tapout_operations, log_filename)

        self.__ferrors.close()
        self.__result_file.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test_command', type=str, required=True, help='Command that will be executed with modified netlist')
    parser.add_argument('--out_dir', type=str, required=True, help='Output directory')
    parser.add_argument('--one_run', action='store_true', default=False, help=f'Tapout all operations in one run.')
    parser.add_argument('--step_by_step', action='store_true', default=False, help=f'Modified netlist will have only one operation from graph')
    args = parser.parse_args()

    command = args.test_command
    TapoutCommandExecutor(command, args.out_dir).run(args.one_run, args.step_by_step)

if __name__ == "__main__":
    main()