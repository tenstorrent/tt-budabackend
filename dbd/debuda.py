#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  script_name [options] [<output_dir>]

Options:
  -h --help                       Show this help message and exit.
  --netlist=<file>                Netlist file to import. If not supplied, the most recent subdirectory of tt_build/ will be used.
  --commands=<cmds>               Execute a list of semicolon-separated commands.
  --server-cache=<mode>           Specifies the method of communication with the Debuda Server. [default: off]
  --verbose                       Print verbose output.
  --test                          Exits with non-zero exit code on any exception.
  --debuda-server-address=<addr>  IP address of debuda server. [default: localhost:5555]
  --local                         Use pybind library instead of debuda server. This argument overwrites --debuda-server-address with localhost:0. Note that this option does not support dumping tiles and always opens ALL available devices.

Description:
    Debuda parses the build output files and reads the device state to provide a debugging interface for the user.

Arguments:
  output_dir                     Output directory of a buda run. If left blank, the most recent subdirectory of tt_build/ will be used.
"""
try:
    from functools import cached_property
    import sys, os, argparse, traceback, fnmatch, importlib
    from tabulate import tabulate
    from prompt_toolkit import PromptSession
    from prompt_toolkit.completion import Completer, Completion
    from prompt_toolkit.formatted_text import HTML, fragment_list_to_text, to_formatted_text
    from prompt_toolkit.history import InMemoryHistory
    from docopt import DocoptExit, docopt
    from fastnumbers import try_int
except ModuleNotFoundError as e:
    traceback.print_exc()
    print(f"Try:\033[31m pip install -r dbd/requirements.txt; make dbd \033[0m")
    exit(1)

# Add the current directory to the path. This is so that dynamically-loaded debuda_commands can import the files in
# the application directory.
def application_path():
    if getattr(sys, "frozen", False):
        application_path = os.path.dirname(sys.executable)
    elif __file__:
        application_path = os.path.dirname(__file__)
    return application_path


sys.path.append(application_path())

from tt_commands import find_command
from tt_debuda_server import debuda_server_not_supported
from tt_coordinate import OnChipCoordinate
import tt_util as util, tt_device, tt_netlist
from tt_firmware import ELF, BUDA_FW_VARS

class DebudaCompleter(Completer):
    def __init__(self, commands, context):
        self.commands = [cmd["long"] for cmd in commands] + [
            cmd["short"] for cmd in commands
        ]
        self.context = context

    # Given a piece of a command, find all possible completions
    def lookup_commands(self, cmd):
        completions = []
        for command in self.commands:
            if command.startswith(cmd):
                completions.append(command)
        return completions

    def fuzzy_lookup_addresses(self, addr):
        completions = self.context.elf.fuzzy_find_multiple(addr, limit=30)
        return completions

    def get_completions(self, document, complete_event):
        if complete_event.completion_requested:
            prompt_current_word = document.get_word_before_cursor(
                pattern=self.context.elf.name_word_pattern
            )
            prompt_text = document.text_before_cursor
            # 1. If it is the first word, complete with the list of commands (lookup_commands)
            if " " not in prompt_text:
                for command in self.lookup_commands(prompt_current_word):
                    yield Completion(command, start_position=-len(prompt_current_word))
            # 2. If the currently-edited word starts with @, complete with address lookup from FW (fuzzy_lookup_addresses)
            elif prompt_current_word.startswith("@"):
                addr_part = prompt_current_word[1:]
                for address in self.fuzzy_lookup_addresses(addr_part):
                    yield Completion(
                        f"@{address}", start_position=-len(prompt_current_word)
                    )


# Creates rows for tabulate for all commands of a given type
def format_commands(commands, type, specific_cmd=None, verbose=False):
    rows = []
    for c in commands:
        if c["type"] == type and (
            specific_cmd is None
            or c["long"] == specific_cmd
            or c["short"] == specific_cmd
        ):
            description = c["description"]
            if verbose:
                row = [f"{util.CLR_INFO}{c['long']}{util.CLR_END}", f"{c['short']}", ""]
                rows.append(row)
                row2 = [f"", f"", f"{description}"]
                rows.append(row2)
                rows.append(["<--MIDRULE-->", "", ""])
            else:
                description = description.split("\n")
                # Iterate to find the line containing "Description:". Then take the following line.
                # If there is no such line, take the first line.
                found_description = False
                for line in description:
                    if found_description:
                        description = line
                        break
                    if "Description:" in line:
                        found_description = True
                if not found_description:
                    description = description[0]
                description = description.strip()
                row = [
                    f"{util.CLR_INFO}{c['long']}{util.CLR_END}",
                    f"{c['short']}",
                    f"{description}",
                ]
                rows.append(row)
    return rows

# Print all commands (help)
def print_help(commands, cmd):
    help_command_description = find_command(commands, "help")["description"]
    args = docopt(help_command_description, argv=" ".join(cmd[1:]))

    specific_cmd = args["<command>"] if "<command>" in args else None
    verbose = ("-v" in args and args["-v"]) or specific_cmd is not None

    rows = []
    rows += format_commands(commands, "housekeeping", specific_cmd, verbose)
    rows += format_commands(commands, "low-level", specific_cmd, verbose)
    rows += format_commands(commands, "high-level", specific_cmd, verbose)
    # rows += format_commands (commands, 'dev', "Development")

    if not rows:
        util.WARN(f"Command '{specific_cmd}' not found")
        return

    # Replace each line starting with <--MIDRULE-->, with a ruler line to separate the commands visually
    table_str = tabulate(rows, headers=["Full Name", "Short", "Description"])
    lines = table_str.split("\n")
    midrule = lines[1]
    for i in range(len(lines)):
        if lines[i].startswith("<--MIDRULE-->"):
            lines[i] = midrule
    new_table_str = "\n".join(lines)
    print(new_table_str)
    if not verbose:
        print("Use '-v' for more details.")


# Certain commands give suggestions for next step. This function formats and prints those suggestions.
def print_navigation_suggestions(navigation_suggestions):
    if navigation_suggestions:
        print("Speed dial:")
        rows = []
        for i in range(len(navigation_suggestions)):
            rows.append(
                [
                    f"{i}",
                    f"{navigation_suggestions[i]['description']}",
                    f"{navigation_suggestions[i]['cmd']}",
                ]
            )
        print(tabulate(rows, headers=["#", "Description", "Command"]))


# Imports 'plugin' commands from debuda_commands/ directory
# With 'reload' argument set to True, the debuda_commands can be live-reloaded (using importlib.reload)
def import_commands(reload=False):
    # Built-in commands
    commands = [
        {
            "long": "exit",
            "short": "x",
            "type": "housekeeping",
            "description": "Description:\n  Exits the program. The optional argument represents the exit code. Defaults to 0.",
        },
        {
            "long": "help",
            "short": "h",
            "type": "housekeeping",
            "description": "Usage:\n  help [-v] [<command>]\n\n"
            + "Description:\n  Prints documentation summary. Use -v for details. If a command name is specified, it prints documentation for that command only.\n\n"
            + "Options:\n  -v   If specified, prints verbose documentation.",
        },
        {
            "long": "reload",
            "short": "rl",
            "type": "housekeeping",
            "description": "Description:\n  Reloads files in debuda_commands directory. Useful for development of commands.",
        },
        {
            "long": "eval",
            "short": "ev",
            "type": "housekeeping",
            "description": "Description:\n  Evaluates a Python expression.\n\nExamples:\n  eval 3+5\n  eval hex(@brisc.EPOCH_INFO_PTR.epoch_id)",
        },
    ]

    cmd_files = []
    for root, dirnames, filenames in os.walk(
        util.application_path() + "/debuda_commands"
    ):
        for filename in fnmatch.filter(filenames, "*.py"):
            cmd_files.append(os.path.join(root, filename))

    sys.path.append(util.application_path() + "/debuda_commands")

    cmd_files.sort()
    for cmdfile in cmd_files:
        module_path = os.path.splitext(os.path.basename(cmdfile))[0]
        try:
            cmd_module = importlib.import_module(module_path)
        except Exception as e:
            # Print call stack
            util.notify_exception(type(e), e, e.__traceback__)
            continue
        command_metadata = cmd_module.command_metadata
        command_metadata["module"] = cmd_module

        # Make the module name the default 'long' invocation string
        if "long" not in command_metadata:
            command_metadata["long"] = cmd_module.__name__
        util.VERBOSE(
            f"Importing command {command_metadata['long']} from '{cmd_module.__name__}'"
        )

        if reload:
            importlib.reload(cmd_module)

        # Check command names/shortcut overlap (only when not reloading)
        for cmd in commands:
            if cmd["long"] == command_metadata["long"]:
                util.FATAL(f"Command {cmd['long']} already exists")
            if cmd["short"] == command_metadata["short"]:
                util.FATAL(
                    f"Commands {cmd['long']} and {command_metadata['long']} use the same shortcut: {cmd['short']}"
                )
        commands.append(command_metadata)
    return commands


# Finds the most recent build directory
def locate_most_recent_build_output_dir():
    # Try to find a default output directory
    most_recent_modification_time = None
    try:
        for tt_build_subfile in os.listdir("tt_build"):
            subdir = f"tt_build/{tt_build_subfile}"
            if os.path.isdir(subdir):
                if (
                    most_recent_modification_time is None
                    or os.path.getmtime(subdir) > most_recent_modification_time
                ):
                    most_recent_modification_time = os.path.getmtime(subdir)
                    most_recent_subdir = subdir
        return most_recent_subdir
    except:
        pass
    return None


# All-encompassing structure representing a Debuda context
class Context:
    def __init__(self, netlist_filepath, run_dirpath, runtime_data_yaml, cluster_desc_path):
        self._netlist_filepath = netlist_filepath
        self._run_dirpath = run_dirpath
        self._runtime_data_yaml = runtime_data_yaml
        self._cluster_desc_path = cluster_desc_path

    @cached_property
    def netlist(self):
        if self._run_dirpath is None or self._runtime_data_yaml is None:
            raise util.TTException(f"We are running with limited functionality, elf files are not available.")
        return tt_netlist.Netlist(self._netlist_filepath, self._run_dirpath, self._runtime_data_yaml)

    @cached_property
    def devices(self):
        device_ids = self.device_ids
        devices = dict()
        for device_id in device_ids:
            try:
                device_desc_path = self.server_ifc.get_device_soc_description(device_id)
            except:
                device_desc_path = tt_device.get_soc_desc_path(device_id, self._run_dirpath)
            # util.INFO(f"Loading device {device_id} from {device_desc_path}")
            devices[device_id] = tt_device.Device.create(
                self.arch,
                device_id=device_id,
                cluster_desc=self.cluster_desc.root,
                device_desc_path=device_desc_path,
                context=self
            )
        return devices

    @cached_property
    def cluster_desc(self):
        if self._cluster_desc_path is None:
            raise util.TTException(f"We are running with limited functionality, cluster description is not available.")
        return util.YamlFile(self._cluster_desc_path)

    @cached_property
    def is_buda(self):
        return not self._runtime_data_yaml is None

    @cached_property
    def device_ids(self):
        try:
            device_ids = self.server_ifc.get_device_ids()
            return util.set(d for d in device_ids)
        except:
            return self.netlist.get_device_ids()

    @cached_property
    def arch(self):
        try:
            return self.server_ifc.get_device_arch(min(self.device_ids))
        except:
            return self.netlist.get_arch()

    @cached_property
    def elf(self):
        if self._run_dirpath is None:
            raise util.TTException(f"We are running with limited functionality, elf files are not available.")

        elf_files_to_load = {
            "brisc": f"{self._run_dirpath}/brisc/brisc.elf",
            "ncrisc": f"{self._run_dirpath}/ncrisc/ncrisc.elf",
        }
        if self.arch.lower() != "grayskull":
            elf_files_to_load["erisc_app"] = f"{self._run_dirpath}/erisc/erisc_app.elf"

        extra_vars = BUDA_FW_VARS if self.is_buda else None
        return ELF(elf_files_to_load, extra_vars=extra_vars)

    @cached_property
    def epoch_id_address(self):
        address, _ = self.elf.parse_addr_size("brisc.EPOCH_INFO_PTR.epoch_id")
        if address is None:
            raise util.TTException(f"Could not find address of epoch_id field in ELF file.")
        return address

    @cached_property
    def eth_epoch_id_address(self):
        if self.arch.lower() == "grayskull":
            raise util.TTException(f"There are no eth cores on grayskull.")
        address, _ = self.elf.parse_addr_size("erisc_app.EPOCH_INFO_PTR.epoch_id")
        # address = 0x20080 + context.elf.parse_address("@epoch_t.epoch_id")
        if address is None:
            raise util.TTException(f"Could not ind address of epoch_id field in erisc ELF file.")
        return address

    def __repr__(self):
        return f"context"

# Loads all files necessary to debug a single buda run
# Returns a debug 'context' that contains the loaded information
def load_context(netlist_filepath, run_dirpath, runtime_data_yaml, cluster_desc_path):
    context = Context(netlist_filepath, run_dirpath, runtime_data_yaml, cluster_desc_path)

    # TODO: Make this lazy load for buda
    if context.is_buda:
        # Assign a device to each graph.
        for _, graph in context.netlist.graphs.items():
            graph.device = context.devices[graph.device_id()]
        context.netlist.devices = context.devices

    return context

class UIState:
    def __init__(self, context: Context) -> None:
        self.context = context
        self.current_device_id = 0  # Currently selected device id
        self.current_location = OnChipCoordinate(0, 0, "netlist", self.current_device)  # Currently selected core
        self.current_stream_id = 8  # Currently selected stream_id
        self.current_prompt = ""  # Based on the current x,y,stream_id tuple
        try:
            self.current_graph_name = context.netlist.graphs.first().id()  # Currently selected graph name
        except:
            self.current_graph_name = None

    @property
    def current_device(self):
        return self.context.devices[self.current_device_id] if self.current_device_id is not None else None

class SimplePromptSession:
    def __init__(self):
        self.history = InMemoryHistory()
    def prompt(self, message):
        print(fragment_list_to_text(to_formatted_text(message)))
        s = input()
        self.history.append_string(s)
        return s

def main_loop(args, context):
    """
    Main loop: read-eval-print
    """
    cmd_raw = ""

    commands = import_commands()
    context.commands = commands # Set the commands in the context so we can call commands from other commands

    # Create prompt object.
    context.prompt_session = PromptSession(completer=DebudaCompleter(commands, context)) if sys.stdin.isatty() else SimplePromptSession()

    # Initialize current UI state
    ui_state = UIState(context)

    navigation_suggestions = None

    # These commands will be executed right away (before allowing user input)
    non_interactive_commands = (
        args["--commands"].split(";") if args["--commands"] else []
    )

    # Main command loop
    while True:
        have_non_interactive_commands = len(non_interactive_commands) > 0
        current_loc = ui_state.current_location

        if (
            ui_state.current_location is not None
            and ui_state.current_graph_name is not None
            and ui_state.current_device is not None
        ):
            ui_state.current_prompt = (
                f"NocTr:{util.CLR_PROMPT}{current_loc.to_str()}{util.CLR_PROMPT_END} "
            )
            ui_state.current_prompt += f"netlist:{util.CLR_PROMPT}{current_loc.to_str('netlist')}{util.CLR_PROMPT_END} "
            ui_state.current_prompt += f"stream:{util.CLR_PROMPT}{ui_state.current_stream_id}{util.CLR_PROMPT_END} "
            graph = context.netlist.graph(ui_state.current_graph_name)
            op_name = graph.location_to_op_name(current_loc)
            ui_state.current_prompt += f"op:{util.CLR_PROMPT}{op_name}{util.CLR_PROMPT_END} "

        try:
            if not ui_state.current_graph_name is None:
                ui_state.current_device_id = context.netlist.graph_name_to_device_id(ui_state.current_graph_name)

            print_navigation_suggestions(navigation_suggestions)

            if have_non_interactive_commands:
                cmd_raw = non_interactive_commands[0].strip()
                non_interactive_commands = non_interactive_commands[1:]
                if len(cmd_raw) > 0:
                    print(
                        f"{util.CLR_INFO}Executing command: %s{util.CLR_END}" % cmd_raw
                    )
            else:
                if context.is_buda:
                    epoch_id = context.netlist.graph_name_to_epoch_id(ui_state.current_graph_name)
                else:
                    epoch_id = None
                my_prompt = f"Current epoch:{util.CLR_PROMPT}{epoch_id}{util.CLR_PROMPT_END}({ui_state.current_graph_name}) "
                my_prompt += f"device:{util.CLR_PROMPT}{ui_state.current_device_id}{util.CLR_PROMPT_END} "
                my_prompt += f"loc:{util.CLR_PROMPT}{current_loc.to_str()}{util.CLR_PROMPT_END} "
                my_prompt += f"{ui_state.current_prompt}> "
                cmd_raw = context.prompt_session.prompt(HTML(my_prompt))

            cmd_int = try_int(cmd_raw)
            if type(cmd_int) == int:
                if (
                    navigation_suggestions
                    and cmd_int >= 0
                    and cmd_int < len(navigation_suggestions)
                ):
                    cmd_raw = navigation_suggestions[cmd_int]["cmd"]
                else:
                    raise util.TTException(f"Invalid speed dial number: {cmd_int}")

            cmd = cmd_raw.split()
            if len(cmd) > 0:
                cmd_string = cmd[0]
                found_command = None

                # Look for command to execute
                for c in commands:
                    if c["short"] == cmd_string or c["long"] == cmd_string:
                        found_command = c

                if found_command == None:
                    # Print help on invalid commands
                    print_help(commands, cmd)
                    raise util.TTException(f"Invalid command '{cmd_string}'")
                else:
                    if found_command["long"] == "exit":
                        exit_code = int(cmd[1]) if len(cmd) > 1 else 0
                        return exit_code
                    elif found_command["long"] == "help":
                        print_help(commands, cmd)
                    elif found_command["long"] == "reload":
                        import_commands(reload=True)
                    elif found_command["long"] == "eval":
                        eval_str = " ".join(cmd[1:])
                        eval_str = context.elf.substitute_names_with_values(eval_str)
                        print(f"{eval_str} = {eval(eval_str)}")
                    else:
                        new_navigation_suggestions = found_command["module"].run(
                            cmd_raw, context, ui_state
                        )
                        navigation_suggestions = new_navigation_suggestions

        except Exception as e:
            if args["--test"]:  # Always raise in test mode
                util.ERROR("CLI option --test is set. Raising exception to exit.")
                raise
            else:
                util.notify_exception(type(e), e, e.__traceback__)
            if have_non_interactive_commands or type(e) == util.TTFatalException:
                # In non-interactive mode and on fatal excepions, we re-raise to exit the program
                raise
        except DocoptExit as e:
            util.ERROR(e)
            if args["--test"]:  # Always raise in test mode
                raise

def main():
    args = docopt(__doc__)

    if not args["--verbose"]:
        util.VERBOSE = util.NULL_PRINT

    # Try to determine the output directory
    runtime_data_yaml = None
    runtime_data_yaml_filename = None
    if args["<output_dir>"] is None:  # Then try to find the most recent tt_build subdir
        most_recent_build_output_dir = locate_most_recent_build_output_dir()
        if most_recent_build_output_dir:
            util.INFO(
                f"Output directory not specified. Using most recently changed subdirectory of tt_build: {os.getcwd()}/{most_recent_build_output_dir}"
            )
            args["<output_dir>"] = most_recent_build_output_dir
        else:
            util.WARN(
                f"Output directory (output_dir) was not supplied and cannot be determined automatically. Continuing with limited functionality..."
            )

    # Try to load the runtime data from the output directory
    if not args['<output_dir>'] is None:
        runtime_data_yaml_filename = f"{(args['<output_dir>'])}/runtime_data.yaml"
        if os.path.exists(runtime_data_yaml_filename):
            runtime_data_yaml = util.YamlFile(runtime_data_yaml_filename)
        else:
            runtime_data_yaml_filename = None
            util.WARN(f"Output directory (output_dir) does not represent buda run output directory. Continuing with limited functionality...")

    # Try to connect to the server. If it is not already running, it will be started.
    if args["--local"]:
        print(f"Using pybin library instead of debuda server.")
        args["--debuda-server-address"] = "localhost:0"
    else:
        print(f"Connecting to Debuda server at {args['--debuda-server-address']}")
    server_ifc = tt_device.init_server_communication(
        args["--server-cache"],
        args["--debuda-server-address"],
        runtime_data_yaml_filename,
    )

    # We did not find the runtime_data.yaml file, so we need to get the runtime data from the server
    if runtime_data_yaml is None:
        try:
            runtime_data_yaml = server_ifc.get_runtime_data()
        except debuda_server_not_supported as e:
            # It is ok to continue with limited functionality
            pass

    try:
        cluster_desc_path = os.path.abspath(server_ifc.get_cluster_desc_path())
    except debuda_server_not_supported as e:
        # It is ok to continue with limited functionality
        cluster_desc_path = None

    # Create the context
    context = load_context(
        netlist_filepath=args["--netlist"],
        run_dirpath=args["<output_dir>"],
        runtime_data_yaml=runtime_data_yaml,
        cluster_desc_path=cluster_desc_path,
    )
    context.server_ifc = server_ifc
    context.args = args  # Used by 'export' command
    context.debuda_path = __file__  # Used by 'export' command

    # If we spawned the Debuda server, the runtime_data provided by debuda server is not valid, and we use the runtime_data.yaml file saved by the run
    # As the server_ifc might get probed for the data, we set the get_runtime_data function to return the runtime_data_yaml
    if server_ifc.spawning_debuda_stub:
        server_ifc.get_runtime_data = lambda: context.netlist.runtime_data_yaml

    # Main function
    exit_code = main_loop(args, context)

    util.VERBOSE(f"Exiting with code {exit_code} ")
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
