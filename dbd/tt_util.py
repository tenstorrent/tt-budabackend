#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys, os, zipfile, pprint, time
from tabulate import tabulate
from sortedcontainers import SortedSet
import traceback, socket
import ryml, yaml


# Pretty print exceptions (traceback)
def notify_exception(exc_type, exc_value, tb):
    rows = []
    ss_list = traceback.extract_tb(tb)
    indent = 0
    fn = "-"
    line_number = "-"
    for ss in ss_list:
        file_name, line_number, func_name, text = ss
        abs_filename = os.path.abspath(file_name)

        # Exceptions thrown from importlib might not have a file in all stack frames
        if os.path.exists(abs_filename):
            fn = os.path.relpath(abs_filename)
            row = [
                f"{fn}:{line_number}",
                func_name,
                f"{CLR_BLUE}{'  '*indent}{text}{CLR_END}",
            ]
            rows.append(row)
            if indent < 10:
                indent += 1

    rows.append(
        [
            f"{CLR_RED}{fn}:{line_number}{CLR_END}",
            f"{CLR_RED}{func_name}{CLR_END}",
            f"{CLR_RED}{exc_type.__name__}: {exc_value}{CLR_END}",
        ])

    # Exceptions thrown from importlib set these
    if hasattr(exc_value, "filename") and hasattr(exc_value, "lineno"):
        rows.append(
            [
                f"{CLR_RED}{os.path.relpath(exc_value.filename)}:{exc_value.lineno}{CLR_END}",
                f"{CLR_RED}{func_name}{CLR_END}",
                f"{CLR_RED}{exc_type.__name__}: {exc_value.text}{CLR_END}",
            ]
        )

    print(tabulate(rows, disable_numparse=True))


# Replace the exception hook to print a nicer output
sys.excepthook = notify_exception


# Get path of this script. 'frozen' means packaged with pyinstaller.
def application_path():
    if getattr(sys, "frozen", False):
        application_path = os.path.dirname(sys.executable)
    elif __file__:
        application_path = os.path.dirname(__file__)
    return application_path


def to_hex_if_possible(val):
    if type(val) == int and (val > 9 or val < 0):
        return f" (0x{val:x})"
    else:
        return ""


def with_hex_if_possible(val):
    return f"{val}{to_hex_if_possible(val)}"


# Colors
CLR_RED = "\033[31m"
CLR_GREEN = "\033[32m"
CLR_BLUE = "\033[34m"
CLR_GREY = "\033[37m"
CLR_ORANGE = "\033[38:2:205:106:0m"
CLR_WHITE = "\033[38:2:255:255:255m"

CLR_END = "\033[0m"

CLR_ERR = CLR_RED
CLR_WARN = CLR_ORANGE
CLR_INFO = CLR_BLUE

CLR_PROMPT = "<style color='green'>"
CLR_PROMPT_END = "</style>"
CLR_PROMPT_BAD_VALUE = "<style color='red'>"
CLR_PROMPT_BAD_VALUE_END = "</style>"

DEC_FORMAT = 'f"{d}"'
HEX_FORMAT = 'f"0x{d:08x}"'
DEC_AND_HEX_FORMAT = 'f"{d} (0x{d:08x})"'


class TTException(Exception):
    pass


# We create a fatal exception that must terminate the program
# All other exceptions might get caught and the program might continue
class TTFatalException(Exception):
    pass


# Colorized messages
def NULL_PRINT(s):
    pass


def DEBUG(s, **kwargs):
    print(f"{CLR_WARN}{s}{CLR_END}", **kwargs)


def VERBOSE(s, **kwargs):
    print(f"{CLR_END}{s}{CLR_END}", **kwargs)


def PRINT(s, **kwargs):
    print(f"{CLR_END}{s}", **kwargs)


def INFO(s, **kwargs):
    print(f"{CLR_INFO}{s}{CLR_END}", **kwargs)


def WARN(s, **kwargs):
    print(f"{CLR_WARN}{s}{CLR_END}", **kwargs)


def ERROR(s, **kwargs):
    print(f"{CLR_ERR}{s}{CLR_END}", **kwargs)


def FATAL(s, **kwargs):
    ERROR(s, **kwargs)
    raise TTFatalException(s)


# Given a list l of possibly shuffled integers from 0 to len(l), the function returns reverse mapping
def reverse_mapping_list(l):
    ret = [0] * len(l)
    for idx, val in enumerate(l):
        ret[val] = idx
    return ret


# Converts a shallow dict to a table. A table is an array that can be consumed by tabulate.py
def dict_to_table(dct):
    if dct:
        table = [[k, with_hex_if_possible(dct[k])] for k in dct]
    else:
        table = [["", ""]]
    return table


# Given two tables 'a' and 'b' merge them into a wider table
def merge_tables_side_by_side(a, b):
    width_a = len(a[0])
    width_b = len(b[0])
    t = []
    for i in range(max(len(a), len(b))):
        row = [None] * (width_a + width_b)

        for j in range(width_a):
            row[j] = "" if i >= len(a) else a[i][j]

        for j in range(width_b):
            row[j + width_a] = "" if i >= len(b) else b[i][j]

        t.append(row)
    return t


# Given an array of dicts, and their titles. Print a flattened version of all the dicts as a big table.
def print_columnar_dicts(dict_array, title_array):
    final_table = []
    for idx, dct in enumerate(dict_array):
        assert isinstance(dct, dict) or isinstance(dct, RymlLazyDictionary)
        current_table = dict_to_table(dct)
        if idx == 0:
            final_table = current_table
        else:
            final_table = merge_tables_side_by_side(final_table, current_table)

    titles = []
    for t in title_array:
        titles += [t]
        titles += [""]

    print(tabulate(final_table, headers=titles, disable_numparse=True))


# Takes a Rapid yaml (ryml) memory object and converts it to a value. Tries to convert to int if possible.
def ryml_memory_to_value(mem):
    if mem is None:
        return None
    v = bytes(mem).decode("utf-8")
    # Try to convert v to int allowing for hex string (0x...)
    try:
        v = int(v, 0)
    except ValueError:
        pass
    return v


# Takes a Rapid yaml (ryml) tree and converts it to a Python dict
def ryml_to_dict(tree, i):
    is_seq = tree.is_seq(i)
    is_map = tree.is_map(i)
    if i == ryml.NONE:
        return None

    if is_seq:
        d = []
        fc = tree.first_child(i)
        while fc != ryml.NONE:
            d.append(ryml_to_dict(tree, fc))
            fc = tree.next_sibling(fc)
            if fc == ryml.NONE:
                break
        return d

    elif is_map:
        d = dict()
        fc = tree.first_child(i)
        while fc != ryml.NONE:
            key = ryml_memory_to_value(tree.key(fc))
            d[key] = ryml_to_dict(tree, fc)
            fc = tree.next_sibling(fc)
            if fc == ryml.NONE:
                break
        return d
    else:
        v = None
        if tree.has_val(i):
            v = ryml_memory_to_value(tree.val(i))
        return v


from collections.abc import Sequence
from functools import cached_property
from typing import Mapping, TypeVar
from fastnumbers import try_int


def ryml_to_lazy(tree, id):
    if id == ryml.NONE:
        return None
    if tree.is_seq(id):
        return RymlLazyList(tree, id)
    if tree.is_map(id):
        return RymlLazyDictionary(tree, id)
    if tree.has_val(id):
        value = tree.val(id)
        if value is not None:
            return try_int(str(value, "utf8"), base=0)
    return None


class RymlLazyList(Sequence):
    def __init__(self, tree, node):
        self.tree = tree
        self.node = node
        self.length = self.tree.num_children(self.node)
        self.items = [None] * self.length
        self.item_initialized = [False] * self.length

    def __getitem__(self, i):
        if not self.item_initialized[i]:
            self.items[i] = ryml_to_lazy(self.tree, self.tree.child(self.node, i))
            self.item_initialized[i] = True
        return self.items[i]

    def __len__(self):
        return self.length

    def __repr__(self):
        return repr(self.items)

    def __iter__(self):
        i = 0
        while i < self.length:
            v = self[i]
            yield v
            i += 1


KeyType = TypeVar("KeyType")
ValueType = TypeVar("ValueType")


class RymlLazyDictionaryIterator:
    def __init__(self, dictionary: "RymlLazyDictionary"):
        self.dictionary = dictionary
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index < self.dictionary.length:
            child_node = self.dictionary.tree.child(self.dictionary.node, self.index)
            key = self.dictionary.get_key(child_node)
            self.index += 1
            return key
        else:
            raise StopIteration


class RymlLazyDictionary(Mapping[KeyType, ValueType]):
    def __init__(self, tree, node):
        self.tree = tree
        self.node = node
        self.length = self.tree.num_children(self.node)
        self._items = dict()

    @cached_property
    def child_nodes(self):
        child_nodes = dict()
        for child_node in ryml.children(self.tree, self.node):
            child_nodes[self.get_key(child_node)] = child_node
        return child_nodes

    def get_key(self, child_node):
        return try_int(str(self.tree.key(child_node), "utf8"), base=0)

    def __contains__(self, key):
        return key in self.child_nodes

    def __getitem__(self, key: KeyType) -> ValueType:
        item = self._items.get(key)
        if item == None:
            child_node = self.child_nodes[key]
            item = ryml_to_lazy(self.tree, child_node)
            self._items[key] = item
        return item

    def __setitem__(self, key, value):
        self._items[key] = value

    def __iter__(self):
        return RymlLazyDictionaryIterator(self)

    def __len__(self):
        return self.length

    def keys(self):
        return self.child_nodes.keys()

    def items(self):
        for key in self.keys():
            yield (key, self[key])

    def __str__(self):
        return str(dict(self.items()))


class RymlLazy(RymlLazyDictionary):
    def __init__(self, buffer):
        if type(buffer) == str:
            buffer = buffer.encode()
        self.buffer = buffer
        self.tree = ryml.parse_in_arena(self.buffer)
        super().__init__(self.tree, self.tree.root_id())


USE_LAZY_RYML = True


# Given a string with multiple yaml documents, parse them all and return a list of dicts
def ryml_load_all(yaml_string):
    documents = yaml_string.split("---")
    parsed_documents = []
    for d in documents:
        global USE_LAZY_RYML
        if USE_LAZY_RYML:
            parsed_documents.append(RymlLazy(d))
        else:
            tree = ryml.parse_in_arena(d)
            parsed_documents.append(ryml_to_dict(tree, tree.root_id()))
    return parsed_documents


# Container for YAML
class YamlContainer:
    def __init__(self, yaml_string, source="N/A"):
        self.root = dict()
        parsed_documents = ryml_load_all(yaml_string)
        for d in parsed_documents:
            # Merge the documents
            self.root = {**self.root, **d}
        self.source = source

    def __str__(self):
        return f"{type(self).__name__}: {self.source}"

    def __repr__(self):
        return self.__str__()


# A wrapper/container of a YAML file. Loads the file on demand.
# Includes a cache in case a file is loaded multiple times
class YamlFile:
    # Cache
    file_cache = {}

    def __init__(self, yamlname, post_process_yaml=None, file_content=None ):
        self.filepath = yamlname
        self.content = file_content
        # Some files (such as pipegen.yaml) contain multiple documents (separated by ---). We post-process them
        self.post_process_yaml = post_process_yaml
        YamlFile.file_cache[self.filepath] = None

    def load(self):
        if self.filepath in YamlFile.file_cache and YamlFile.file_cache[self.filepath]:
            self.root = YamlFile.file_cache[self.filepath]
        else:
            current_time = time.time()
            INFO(f"Loading yaml file: '{os.path.abspath(self.filepath)}'", end="")
            self.root = dict()

            # load self.filepath into string
            if self.content is None:
                with open(self.filepath, "r") as stream:
                    yaml_string = stream.read()
            else:
                yaml_string = self.content
                
                # Clear the content to save memory
                self.content = None

            if self.post_process_yaml is not None:
                self.root = self.post_process_yaml(ryml_load_all(yaml_string))
            else:
                for i in ryml_load_all(yaml_string):
                    self.root = {**self.root, **i}
            YamlFile.file_cache[self.filepath] = self.root
            INFO(
                f" ({len(yaml_string)} bytes loaded in {time.time() - current_time:.2f}s)"
            )

    def __str__(self):
        return f"{type(self).__name__}: {self.filepath}"

    def __repr__(self):
        return self.__str__()

    def items(self):
        return self.root.items()

    def id(self):
        return self.filepath

    def __getattr__(self, name):
        if name == "root":
            self.load()
        return object.__getattribute__(self, name)


DEFAULT_EXPORT_FILENAME = "debuda-export.zip"


# Removes a prefix from a string
def remove_prefix(text, prefix):
    if prefix and text.startswith(prefix):
        return text[len(prefix) :]
    return text


def generate_unique_filename(filename):
    file_id = 0
    unique_filename = filename
    e_filename, e_file_extension = os.path.splitext(unique_filename)
    while os.path.exists(unique_filename):
        # Split the filename into name and extension
        unique_filename = f"{e_filename}.{file_id}{e_file_extension}"
        file_id += 1
    return unique_filename


# Exports filelist to a zip file
def export_to_zip(filelist, out_file=DEFAULT_EXPORT_FILENAME, prefix_to_remove=None):
    if out_file is None:
        out_file = DEFAULT_EXPORT_FILENAME

    unique_out_file = generate_unique_filename(out_file)

    zf = zipfile.ZipFile(unique_out_file, "w", zipfile.ZIP_DEFLATED)

    for filepath in filelist:
        zf.write(filepath, arcname=remove_prefix(filepath, prefix_to_remove))

    return unique_out_file


def write_to_yaml_file(data, filename):
    with open(filename, "w") as output_yaml_file:
        # Improve: This could also be done with ryml
        yaml.dump(data, output_yaml_file)


# Takes in data in row/column format and returns string with aligned columns
# Usage:
# column_format = [
#     { 'key_name' : None,          'title': 'Name',   'formatter': None },
#     { 'key_name' : 'target_device', 'title': 'Device', 'formatter': lambda x: f"{util.CLR_RED}{x}{util.CLR_END}" },
# ]
# table=util.TabulateTable(column_format)
# ... table.add_row (row_key_name, row_data_dict)
# print (table)
class TabulateTable:

    # How to format across columns.
    # if 'key_name' is None, the 'key' argument to add_row is used for that column
    def __init__(self, column_format, sort_col=None):
        self.headers = [col["title"] for col in column_format]
        self.rows = []
        self.column_format = column_format
        self.sort_col = sort_col

    def add_row(self, key, data):
        row = []

        for col_data in self.column_format:
            if col_data["key_name"] is None:
                element_data = key
            else:
                element_data = data[col_data["key_name"]]

            if "formatter" in col_data and col_data["formatter"] is not None:
                element_data = col_data["formatter"](element_data)
            row.append(element_data)
        self.rows.append(row)

    def __str__(self):
        if self.sort_col is not None:
            self.rows.sort(key=lambda x: x[self.sort_col])

        return tabulate(self.rows, headers=self.headers, disable_numparse=True)


def is_iterable(obj):
    try:
        iter(obj)
        return True
    except TypeError:
        return False


def set(*args, **kwargs):
    return SortedSet(*args, **kwargs)


class CELLFMT:
    def passthrough(r, c, i, val):
        return val

    def odd_even(r, c, i, val):
        if r % 2 == 0:
            return val
        else:
            return f"{CLR_GREY}{val}{CLR_END}"

    def hex(bytes_per_entry, prefix="0x", postfix=""):
        def hex_formatter(r, c, i, val):
            return (
                prefix
                + "{vrednost:{fmt}}".format(
                    fmt="0" + str(bytes_per_entry * 2) + "x", vrednost=val
                )
                + postfix
            )

        return hex_formatter

    def composite(fmt_function_array):
        def cell_fmt(r, c, i, val):
            for f in fmt_function_array:
                val = f(r, c, i, val)
            return val

        return cell_fmt

    def dec_and_hex(r, c, i, val):
        return f"{CLR_BLUE}{i:4d}{CLR_END} 0x{i:08x}"


def array_to_str(
    A,
    num_cols=8,
    start_row=None,
    end_row=None,
    condense=False,
    show_row_index=True,
    show_col_index=True,
    cell_formatter=CELLFMT.passthrough,
    row_index_formatter=CELLFMT.passthrough,
    include_header=False,
):
    """
    Converts a 1D number array into a formatted string

    Parameters:
        A (list)                                : 1D array to be converted.
        num_cols (int, optional)                : Number of columns in the 2D array representation. Default is 8.
        start_row (int, optional)               : Starting row index for slicing the array. Default is 0.
        end_row (int, optional)                 : Ending row index for slicing the array. Default is the calculated max row index based on `num_cols`.
        condense (bool, optional)               : If True, omits central rows for long arrays, showing only the beginning and end. Default is False.
        show_row_index (bool, optional)         : If True, includes row indices in the output. Default is True.
        show_col_index (bool, optional)         : If True, includes column indices in the output. Default is True.
        cell_formatter (function, optional)     : Function to format each cell. Default is `CELLFMT.passthrough`.
        row_index_formatter (function, optional): Function to format row indices. Default is `CELLFMT.passthrough`.
        include_header (bool, optional)         : If True, includes column headers in the output. Default is False.

    Returns:
    str: A string representation of the 2D array.

    Notes:
        - Returns None for empty input array.
        - `cell_formatter` and `row_index_formatter` are functions taking parameters (row, col, index, value) and (row, start_index, end_index, row_number) respectively.
    """

    lena = len(A)
    if lena == 0:
        return
    if not start_row:
        start_row = 0
    if not end_row:
        end_row = (lena - 1) // num_cols + 1

    skip_border_rows = 4 if condense else 0
    skip_rows_rendered = False

    header = list(range(num_cols))
    rows = []
    for r in range(start_row, end_row):
        if skip_border_rows and (
            r >= skip_border_rows and r < end_row - skip_border_rows
        ):
            if not skip_rows_rendered:
                rows.append(["..."] * num_cols)
                skip_rows_rendered = True
        else:
            row = (
                []
                if not show_row_index
                else [row_index_formatter(r, r * num_cols, r * num_cols, r)]
            )
            for c in range(num_cols):
                i = r * num_cols + c
                if i < lena:
                    row.append(cell_formatter(r, c, i, A[i]))
                else:
                    row.append("")

            rows.append(row)
    return tabulate(rows, headers=header if show_col_index else [], tablefmt="plain", disable_numparse=True)


def dump_memory(addr, array, bytes_per_entry, bytes_per_row, in_hex):
    num_cols = bytes_per_row // bytes_per_entry
    if in_hex:
        cell_formatter = CELLFMT.hex(bytes_per_entry, "")
    else:
        cell_formatter = CELLFMT.passthrough

    def fmt_row_index(addr, bytes_per_entry):
        def hex_formatter(r, c, i, val):
            return "0x{val:08x}".format(val=addr + i * bytes_per_entry) + ":"

        return hex_formatter

    row_index_formatter = fmt_row_index(addr, bytes_per_entry)
    return array_to_str(
        array,
        num_cols,
        show_col_index=False,
        cell_formatter=cell_formatter,
        row_index_formatter=row_index_formatter,
    )


# A helper function to parse print_format
PRINT_FORMATS = {
    "i32": {"is_hex": False, "bytes": 4},
    "i16": {"is_hex": False, "bytes": 2},
    "i8": {"is_hex": False, "bytes": 1},
    "hex32": {"is_hex": True, "bytes": 4},
    "hex16": {"is_hex": True, "bytes": 2},
    "hex8": {"is_hex": True, "bytes": 1},
}


def word_to_byte_array(A):
    byte_array = []
    for i in A:
        byte_array.append(i & 0xFF)
        byte_array.append((i >> 8) & 0xFF)
        byte_array.append((i >> 16) & 0xFF)
        byte_array.append((i >> 32) & 0xFF)
    return byte_array


# Returns True if port available
def is_port_available(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    result = False
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", port))
        result = True
    except:
        pass
    sock.close()
    return result


# Returns a dictionary pretty printed into a string
def pretty(dct):
    pp = pprint.PrettyPrinter(indent=2)
    return pp.pformat(dct)


# Helpers to create comma and space separated strings
def comma_join(l):
    return ", ".join([str(i) for i in l])


def space_join(l):
    return " ".join([str(i) for i in l])


#
# The following helpers are used to allow tracing of function calls
#
import functools, types

# Global variable to keep track of the nesting level of the trace. Used to indent the printout.
TRACE_NESTING_LEVEL = 2
TRACER_FUNCTION = INFO  # By default we show the trace with INFO level


def trace(func):
    """Decorator that prints the comment of the function when it's called.
    The function must have a docstring to be traced."""

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        global TRACE_NESTING_LEVEL
        doc = func.__doc__

        # Collecting arguments and their values for the printout
        args_list = [repr(a) for a in args]
        args_list.extend([f"{k}={v!r}" for k, v in kwargs.items()])
        arguments = ", ".join(args_list)

        # Print with indentation
        if doc:
            TRACER_FUNCTION(
                f"{' ' * TRACE_NESTING_LEVEL}{func.__name__}({arguments}): {doc.strip().splitlines()[0]}"
            )
            # Increase nesting level for nested calls
            TRACE_NESTING_LEVEL += 2

        result = func(*args, **kwargs)

        if doc:
            TRACE_NESTING_LEVEL -= 2

        return result

    return wrapper


def decorate_all_module_functions_for_tracing(mod):
    """Decorates all functions in a given module 'mod' with @trace decorator."""
    global TRACE_NESTING_LEVEL
    TRACE_NESTING_LEVEL = 2
    for name, obj in list(vars(mod).items()):
        if isinstance(obj, types.FunctionType) and obj.__module__ == mod.__name__:
            setattr(mod, name, trace(obj))


def get_indent():
    return " " * TRACE_NESTING_LEVEL


class LOG_INDENT:
    def __enter__(self):
        global TRACE_NESTING_LEVEL
        TRACE_NESTING_LEVEL += 2

    def __exit__(self, exc_type, exc_val, exc_tb):
        global TRACE_NESTING_LEVEL
        TRACE_NESTING_LEVEL -= 2


# Return an ansi color code for a given index. Useful for coloring a list of items.
def clr_by_index(idx):
    return f"\033[{31 + idx % 7}m"
