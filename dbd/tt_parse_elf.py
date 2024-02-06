#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Description:

    This program parses an ELF file and extracts the information from the DWARF
    section. By default, it prints all the information in a table.

    If access-path is specified, it prints the memory access path to read the
    variable pointed to by the access-path. For example, if the access-path is
    "s_ptr->an_int", it prints the memory access path to read the variable
    pointed to by s_ptr->an_int. In this case, it will print two memory accesses
    (one for reading s_ptr and one for reading s_ptr->an_int).

    Access-path supports structures, pointer dereferences, references, and arrays.
    For arrays, the indices must be integers. For example, this is allowed:
    "s_global_var.my_coordinate_matrix_ptr->matrix[2][3].x".

    Dereferending a pointer with * is supported only at the top level. For example,
    this is allowed: "*s_global_var.my_member".

Usage:
  tt_parse_elf.py <elf-file> [ <access-path> ] [ -d | --debug ]

Options:
  -d --debug             Enable debug messages

Arguments:
  elf-file               ELF file to parse
  access-path            Access path to a variable in the ELF file

Examples:
  tt_parse_elf.py ./debuda_test/brisc/brisc.elf
  tt_parse_elf.py ./debuda_test/brisc/brisc.elf EPOCH_ID_PTR->epoch_id

  Options:
  -h --help      Show this screen.
"""
import re
try:
  from elftools.elf.elffile import ELFFile
  from elftools.dwarf.die import DIE as DWARF_DIE
  from elftools.elf.enums import ENUM_ST_INFO_TYPE
  from docopt import docopt
  from tabulate import tabulate
except:
  print ("ERROR: Please install dependencies with: pip install pyelftools docopt fuzzywuzzy python-Levenshtein tabulate")
  exit(1)

CLR_RED = '\033[31m'
CLR_GREEN = '\033[32m'
CLR_BLUE = '\033[34m'
CLR_GREY = '\033[37m'
CLR_ORANGE = '\033[38:2:205:106:0m'
CLR_WHITE = '\033[38:2:255:255:255m'
CLR_END = '\033[0m'

# Helpers
debug_enabled = False

def debug (msg):
    if debug_enabled:
        print (msg)

def strip_DW_(s):
    """
    Removes DW_AT_, DW_TAG_ and other DW_* prefixes from the string
    """
    return re.sub(r'^DW_[^_]*_', '', s)

def find_DIE_at_local_offset(CU, local_offset):
    """
    Given a local offset, find the DIE that has that offset
    """
    for DIE in CU.iter_DIEs():
        if DIE.offset == local_offset + CU.cu_offset:
            return MY_DIE(DIE)
    return None

def find_DIE_that_specifies(CU, die):
    """
    Given a DIE, find another DIE that specifies it. For example, if the DIE is a
    variable, find the DIE that defines the variable.

    IMPROVE: What if there are multiple dies to return?
    """
    for DIE in CU.iter_DIEs():
        if 'DW_AT_specification' in DIE.attributes:
            if DIE.attributes['DW_AT_specification'].value == die.offset:
                return MY_DIE(DIE)
    return None

class MY_DIE(DWARF_DIE):
    """
    A wrapper around DIE class from pyelftools that adds some helper functions.
    """
    def __init__(self, other=None, *args, **kwargs):
        if other is not None:
            # Copy all attributes from the other DIE object.
            self.__dict__.update(other.__dict__)
        else:
            # Initialize as usual if 'other' is not provided.
            super().__init__(*args, **kwargs)

    # We only care about the stuff we can use for probing the memory
    IGNORE_TAGS = set([ 'DW_TAG_compile_unit', 'DW_TAG_subprogram', 'DW_TAG_formal_parameter', 'DW_TAG_unspecified_parameters' ])

    def get_category(self):
        """
        We lump all the DIEs into the following categories
        """
        if self.tag.endswith('_type') or self.tag == 'DW_TAG_typedef':
            return 'type'
        elif self.tag == 'DW_TAG_enumerator':
            return 'enumerator'
        elif self.tag == 'DW_TAG_variable':
            return 'variable'
        elif self.tag == 'DW_TAG_member':
            return 'member'
        elif self.tag in self.IGNORE_TAGS:
            pass # Just skip these tags
        elif self.tag == 'DW_TAG_namespace':
            return 'type'
        elif self.tag == 'DW_TAG_imported_declaration' or self.tag == 'DW_TAG_imported_module' or self.tag == 'DW_TAG_template_type_param' or self.tag == 'DW_TAG_template_value_param':
            return None
        else:
            print (f"{CLR_RED}Don't know how to categorize tag: {self.tag}{CLR_END}")
            return None

    def get_path(self):
        """
        Returns full path of the DIE, including the parent DIEs
            e.g. <parent.get_path()...>::<self.get_name()>
        """
        parent = self.get_parent()
        name = self.get_name()

        if parent and parent.tag != 'DW_TAG_compile_unit':
            parent_path = parent.get_path()
            return f"{parent_path}::{name}"

        return self.get_name()

    def get_resolved_type(self):
        """
        Resolve to underlying type
        TODO: test typedefs, this looks overly complicated
        """
        if self.tag == 'DW_TAG_typedef':
            local_offset = self.attributes['DW_AT_type'].value
            typedef_DIE = find_DIE_at_local_offset(self.cu, local_offset)
            if typedef_DIE: # If typedef, recursivelly do it
                return typedef_DIE.get_resolved_type()
        elif 'DW_AT_type' in self.attributes:
            local_offset = self.attributes['DW_AT_type'].value
            my_type_die = find_DIE_at_local_offset(self.cu, local_offset)
            if my_type_die.tag == 'DW_TAG_typedef':
                return my_type_die.get_resolved_type()
            return my_type_die
        return self

    def dereference_type(self):
        """
        Dereference a pointer type to get the type of what it points to
        """
        if self.tag == 'DW_TAG_pointer_type' or self.tag == 'DW_TAG_reference_type':
            if 'DW_AT_type' not in self.attributes:
                return None
            local_offset = self.attributes['DW_AT_type'].value
            return find_DIE_at_local_offset(self.cu, local_offset)
        return None

    def get_array_element_type(self):
        """
        Get the type of the elements of an array
        """
        if self.tag == 'DW_TAG_array_type':
            local_offset = self.attributes['DW_AT_type'].value
            return find_DIE_at_local_offset(self.cu, local_offset)
        return None

    def get_size(self):
        """
        Return the size in bytes of the DIE
        """
        if 'DW_AT_byte_size' in self.attributes:
          return self.attributes['DW_AT_byte_size'].value

        if self.tag == 'DW_TAG_pointer_type':
            return 4  # Assuming 32-bit pointer

        if self.tag == 'DW_TAG_array_type':
            array_size = 1
            for child in self.iter_children():
                if 'DW_AT_upper_bound' in child.attributes:
                    upper_bound = child.attributes['DW_AT_upper_bound'].value
                    array_size *= (upper_bound + 1)
            elem_die = find_DIE_at_local_offset(self.cu, self.attributes['DW_AT_type'].value)
            elem_size = elem_die.get_size()
            return array_size * elem_size

        if 'DW_AT_type' in self.attributes:
            type_die = find_DIE_at_local_offset(self.cu, self.attributes['DW_AT_type'].value)
            return type_die.get_size()

        return None

    def get_addr(self):
        """
        Return the address of the DIE within the parent type
        """
        addr = None
        if 'DW_AT_data_member_location' in self.attributes:
            addr = self.attributes['DW_AT_data_member_location'].value
        else:
            location = self.attributes.get('DW_AT_location')
            if location:
                # Assuming the location's form is a block
                block = location.value
                # Check if the first opcode is DW_OP_addr (0x03)
                if block[0] == 0x03:
                    addr = int.from_bytes(block[1:], byteorder='little')
            else:
                # Try to find another DIE that defines this variable
                other_die = find_DIE_that_specifies(self.cu, self)
                if other_die:
                    addr = other_die.get_addr()

        if addr is None:
            if self.tag_is('enumerator') or self.tag_is('namespace') or self.tag.endswith('_type') or self.tag_is('typedef'):
                # Then we are not expecting an address
                pass
            elif self.get_parent().tag == 'DW_TAG_union_type':
                return 0 # All members of a union start at the same address
            else:
                if self.attributes.get ("DW_AT_const_value"):
                    pass
                else:
                    print (f"{CLR_RED}ERROR: Cannot find address for {self}{CLR_END}")
        return addr

    def get_value(self):
        """
        Return the value of the DIE
        """
        if 'DW_AT_const_value' in self.attributes:
            return self.attributes['DW_AT_const_value'].value
        return None

    def get_name(self):
        """
        Return the name of the DIE
        """
        if 'DW_AT_name' in self.attributes:
            name = self.attributes['DW_AT_name'].value.decode('utf-8')
        elif 'DW_AT_specification' in self.attributes:
            # This is a variable that is defined elsewhere. We'll skip it.
            # IMPROVE: We should probably find the DIE that defines it and use its name.
            name = None
        elif self.tag_is('pointer_type'):
            if self.dereference_type() is None:
                name = "?"
            else:
                name = f"{self.dereference_type().get_name()}*"
        elif self.tag_is('reference_type'):
            name = f"{self.dereference_type().get_name()}&"
        else:
            # We can't figure out the name of this variable. Just give it a name based on the ELF offset.
            name = f"{self.tag}-{hex(self.offset)}"
        return name

    def iter_children(self):
        """
        Iterate over all children of this DIE
        """
        for child in super().iter_children():
            yield MY_DIE(child)

    def get_parent(self):
        """
        A parent of a variable is the struct it is defined in. It is a type.
        """
        if super().get_parent():
            return MY_DIE(super().get_parent())
        return None

    def __repr__(self):
        """
        Return a string representation of the DIE for debugging
        """
        attrs = []
        for attr_name in self.attributes.keys():
            attr_value = self.attributes[attr_name].value
            if isinstance(attr_value, bytes):
                attr_value = attr_value.decode('utf-8')
            attrs.append(f"{strip_DW_(attr_name)}={attr_value}")
        attrs = ", ".join(attrs)
        return f"{strip_DW_(self.tag)}({attrs}) offset={hex(self.offset)}"

    def tag_is(self, tag):
        return self.tag == f"DW_TAG_{tag}"
# end class MY_DIE


def process_DIE(die, recurse_dict, r_depth):
    """
    Processes a DIE, adds it to the recurse_dict if needed, and returns True if we
    should recurse into its children
    """
    def log(str):
        debug (f"{'  ' * (r_depth+1)}{str}")

    category = die.get_category()
    path = die.get_path()
    resolved_type_path = die.get_resolved_type().get_path()

    if path == "S_TYPE::0x80::an_unnamed_int":
        print("here")
    log (f"{CLR_BLUE}{path}{CLR_END} {category} {CLR_GREEN}{resolved_type_path}{CLR_END} {die.offset}/{hex(die.offset)} {die}")

    if category:
        if category not in recurse_dict:
            recurse_dict[category] = dict()
        recurse_dict[category][path] = die

    recurse_down = category is not None
    return recurse_down

def recurse_DIE(DIE, recurse_dict, r_depth=0):
    """
    This function visits all children recursively and calls process_DIE() on each
    """
    for child in DIE.iter_children():
        recurse_down = process_DIE(child, recurse_dict, r_depth)
        if recurse_down:
            recurse_DIE(child, recurse_dict, r_depth+1)

def parse_dwarf(dwarf):
    """
    Itaretes recursively over all the DIEs in the DWARF info and returns a dictionary
    with the following keys:
        'variable' - all the variables (top level names)
        'type' - all the types
        'member' - all the members of structures etc
        'enumerator' - all the enumerators in the DWARF info
    """
    recurse_dict = dict()

    for CU in dwarf.iter_CUs():
        top_DIE = MY_DIE(CU.get_top_DIE())
        cu_name = 'N/A'
        if 'DW_AT_name' in top_DIE.attributes:
            cu_name = top_DIE.attributes['DW_AT_name'].value.decode('utf-8')
        debug (f"CU: {cu_name}")

        recurse_DIE(top_DIE, recurse_dict)

    return recurse_dict

def read_elf (elf_file_path):
    """
    Reads the ELF file and returns the symbol and type tables
    """
    with open(elf_file_path, 'rb') as f:
        elf = ELFFile(f)

        if not elf.has_dwarf_info():
            print(f"ERROR: {elf_file_path} does not have DWARF info. Source file must be compiled with -g")
            return
        dwarf = elf.get_dwarf_info()

        recurse_dict = parse_dwarf(dwarf)
    return recurse_dict

#
# Access path parsing / processing
#
def split_access_path(access_path):
    """
    Splits a C language access path into three parts:
    1. The first element of the path.
    2. The dividing element (one of '.', '->', '[').
    3. The rest of the path.
    """
    # Regex pattern to capture the first element, the dividing element, and the rest of the path
    # pattern = r'^([\*]*\w+)(\.|->|\[|\])(.*)$'
    pattern = r'^([\*]*[\w:]+)(\.|->|\[)(.*)$'

    match = re.match(pattern, access_path)

    if match:
        return match.group(1), match.group(2), match.group(3)
    else:
        return access_path, None, None

def get_ptr_dereference_count(name):
    """
    Given a name, count the number of leading '*'s. Return the name without the leading '*'s, and the count.
    """
    ptr_dereference_count = 0
    while name.startswith("*"):
        name = name[1:]
        ptr_dereference_count += 1
    return name, ptr_dereference_count

def get_array_indices (rest_of_path):
    """
    Given a string that starts with '[', parse the array indices and return them as a list.
    Supports integer indices only. Supports multidimensional arrays (e.g. [1][2] in which
    case it returns [1, 2]).
    """
    array_indices = []
    while rest_of_path.startswith("["):
        closing_bracket_pos = rest_of_path.find("]")
        if closing_bracket_pos == -1:
            raise Exception(f"ERROR: Expected ] in {rest_of_path}")
        array_index = rest_of_path[1:closing_bracket_pos]
        array_indices.append(int(array_index))
        rest_of_path = rest_of_path[closing_bracket_pos+1:]
    return array_indices, rest_of_path

def resolve_unnamed_union_member(type_die, member_name):
    """
    Given a die that contains an unnamed union of type type_die, and a member path
    represening a member of the unnamed union, return the die of the unnamed union.
    """
    for child in type_die.iter_children():
        if 'DW_AT_name' not in child.attributes and child.tag == 'DW_TAG_member':
            union_type = child.get_resolved_type()
            for union_member_child in union_type.iter_children():
                if union_member_child.get_name() == member_name:
                    return child
    return None


def mem_access (name_dict, access_path, mem_access_function):
    """
    Given an access path such as "s_ptr->an_int", "s_ptr->an_int[2]", or "s_ptr->an_int[2][3]",
    calls the mem_access_function to read the memory, and returns the value array.
    """
    debug (f"Accessing {CLR_GREEN}{access_path}{CLR_END}")

    # At the top level, the next name should be found in the 'variable'
    # section of the name dict: name_dict["variable"]
    # We also check for pointer dereferences here
    access_path, ptr_dereference_count = get_ptr_dereference_count(access_path)
    name, path_divider, rest_of_path = split_access_path(access_path)
    die = name_dict["variable"][name]
    current_address = die.get_addr()
    type_die = die.get_resolved_type()

    num_members_to_read = 1
    while True:
        if path_divider is None:
            # We reached the end of the path. Call the mem_access_functions, and return the value array.

            # If we have leading *s, dereference the pointer
            while ptr_dereference_count > 0:
                ptr_dereference_count -= 1
                type_die = type_die.dereference_type()
                current_address = mem_access_function(current_address, 4)[0] # Assuming 4 byte pointers

            # Check if it is a reference
            if type_die.tag_is("reference_type"):
                type_die = type_die.dereference_type()
                current_address = mem_access_function(current_address, 4)[0] # Dereference the reference

            bytes_to_read = type_die.get_size() * num_members_to_read
            return mem_access_function(current_address, bytes_to_read), current_address, bytes_to_read, type_die
        elif path_divider == ".":
            if num_members_to_read > 1:
                raise Exception(f"ERROR: Cannot access {name} as a single value")
            member_name, path_divider, rest_of_path = split_access_path(rest_of_path)
            member_path = type_die.get_path() + "::" + member_name
            die = name_dict["member"].get(member_path, resolve_unnamed_union_member(type_die, member_name))
            if not die:
                raise Exception(f"ERROR: Cannot find {member_path}")
            type_die = die.get_resolved_type()
            current_address += die.get_addr()

        elif path_divider == "->":
            if num_members_to_read > 1:
                raise Exception(f"ERROR: Cannot access {name} as a single value")
            member_name, path_divider, rest_of_path = split_access_path(rest_of_path)
            if not type_die.tag_is("pointer_type"):
                raise Exception(f"ERROR: {type_die.get_path()} is not a pointer")
            type_die = type_die.dereference_type().get_resolved_type()
            member_path = type_die.get_path() + "::" + member_name
            die = name_dict["member"].get(member_path, resolve_unnamed_union_member(type_die, member_name))
            if not die:
                raise Exception(f"ERROR: Cannot find {member_path}")
            type_die = die.get_resolved_type()
            current_address = mem_access_function(current_address, 4)[0] + die.get_addr() # Assuming 4 byte pointers

        elif path_divider == "[":
            if num_members_to_read > 1:
                raise Exception(f"INTERNAL ERROR: An array of arrays should be processed in a single call")
            array_indices, rest_of_path = get_array_indices ("[" + rest_of_path)
            element_type_die, array_member_offset, num_members_to_read = get_array_member_offset (type_die, array_indices)
            element_size = element_type_die.get_size()
            current_address += element_size * array_member_offset
            rest_of_path = "ARRAY" + rest_of_path
            member_name, path_divider, rest_of_path = split_access_path(rest_of_path)
            type_die = element_type_die
        else:
            raise Exception(f"ERROR: Unknown divider {path_divider}")

def get_array_member_offset (array_type, array_indices):
    """
    Given a list of array_indices of a multidimensional array:
     - Return element type with the offset in bytes.
     - Also, return the number of elements to read to get to the all the subarray elements, in
       case of multidimensional arrays with only a portion of the indices specified.

    For example, for int A[2][3]:
    - if array_indices is [0][0], we return (int, 0, 1): a single element of at offset 0
    - if array_indices is [0][1], we return (int, 1, 1): a single element of at offset 1
    - if array_indices is [1][0], we return (int, 3, 1)): a single element of at offset 3
    - if array_indices is [1],    we return (int, 3, 3): 3 elements at offset 3
    """
    if not array_type.tag_is("pointer_type") and not array_type.tag_is("array_type"):
        raise Exception(f"ERROR: {array_type.get_name()} is not a pointer or an array")
    else:
        if array_type.tag_is("pointer_type"):
            array_element_type = array_type.dereference_type()
        else:
            array_element_type = array_type.get_array_element_type()

        # 1. Find array dimensions
        array_dimensions = []
        for child in array_type.iter_children():
            if 'DW_AT_upper_bound' in child.attributes:
                upper_bound = child.attributes['DW_AT_upper_bound'].value
                array_dimensions.append(upper_bound + 1)

        # 2. Compute subarray sizes in elements. Each element of subarray_sizes stores the number
        # of elements per value in array_indices for the corresponding dimension. For example,
        # if we have a 2D array of integers A[2][3], the subarray_sizes will be [3, 1] because we
        # move 3 elements for each value in array_indices[0] and 1 element for each value 
        # in array_indices[1].
        subarray_sizes = [ 1 ] # In elements
        for i in reversed(range(len(array_dimensions)-1)):
            subarray_size = array_dimensions[i+1] * subarray_sizes[0]
            subarray_sizes.insert(0, subarray_size)

        # 3. Compute offset in bytes
        offset = 0
        for i in range(len(array_indices)):
            if array_indices[i] >= array_dimensions[i]:
                raise Exception(f"ERROR: Array index {array_indices[i]} is out of bounds")
            else:
                offset += array_indices[i] * subarray_sizes[i]
        num_elements_to_read = subarray_sizes[len(array_indices)-1]
        return array_element_type, offset, num_elements_to_read

def access_logger(addr, size_bytes):
    """
    A simple memory reader emulator that prints all memory accesses
    """
    print (f"RD {hex(addr)} - {size_bytes} bytes")
    # We must return what we read to support dereferencing
    words_read = [ i for i in range((size_bytes-1)//4+1) ]
    return words_read

if __name__ == '__main__':
    args = docopt(__doc__)
    elf_file_path = args["<elf-file>"]
    access_path = args["<access-path>"]
    debug_enabled = args["--debug"]

    name_dict = read_elf (elf_file_path)
    if access_path:
        mem_access (name_dict, access_path, access_logger)
    else:
            # Debugging display
        header = ["Category", "Path", "Resolved Type Path", "Size", "Addr", "Hex Addr", "Value", "Hex Value"]
        header.append("DIE offset")
        if debug_enabled:
            header.append("DIE")

        rows = []
        for cat, cat_dict in name_dict.items():
            for key, die in cat_dict.items():
                if key != die.get_path():
                    print (f"{CLR_RED}ERROR: key {key} != die.get_path() {die.get_path()}{CLR_END}")
                resolved_type_path = die.get_resolved_type().get_path()
                if resolved_type_path: # Some DIEs are just refences to other DIEs. We skip them.
                    row = [ cat, die.get_path(), resolved_type_path, die.get_size(), die.get_addr(), hex(die.get_addr()) if die.get_addr() is not None else "", die.get_value(), hex(die.get_value()) if die.get_value() is not None else "" ]
                    row.append(hex(die.offset))
                    if debug_enabled:
                        row.append(str(die))
                    rows.append(row)

        print (tabulate(rows, headers=header, showindex=False))


# TODO:
# 2. Integration into debuda:
#   - Fuzzy search for autocomplete
#   - Real memory reader function
#   - Test
