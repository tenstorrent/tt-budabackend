# Installation

Debuda is a Python application and requires Python 3.9 or newer. Follow instructions at https://www.python.org/ to install Python.

### Unzip `debuda-<version>.zip` into a directory of your choice. For example:

```
mkdir debuda
cd debuda
unzip -l <path-to>/debuda-<version>.zip .
```

### Install Python dependencies

```
pip3 install -r requirements.txt
```

### Set path to Buda home directory

```
BUDA_HOME=<path-to-pybuda>/third_party/budabackend
```

<div style="page-break-after: always;"></div>




# Tutorial

This section describes how to use basic Debuda commands.

## Preparation

Enter the following code into a file called `example.py`:
```
import pybuda
import torch

# Sample PyTorch module
class PyTorchTestModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.weights1 = torch.nn.Parameter(torch.rand(32, 32), requires_grad=True)
        self.weights2 = torch.nn.Parameter(torch.rand(32, 32), requires_grad=True)
    def forward(self, act1, act2):
        m1 = torch.matmul(act1, self.weights1)
        m2 = torch.matmul(act2, self.weights2)
        return m1 + m2, m1

def test_module_direct_pytorch():
    input1 = torch.rand(4, 32, 32)
    input2 = torch.rand(4, 32, 32)
    # Run single inference pass on a PyTorch module, using a wrapper to convert to PyBUDA first
    output = pybuda.PyTorchModule("direct_pt", PyTorchTestModule()).run(input1, input2)
    print(output)


if __name__ == "__main__":
    test_module_direct_pytorch()
```

Use Pybuda to run the example:
```
TT_BACKEND_PERF_ANALYZER=1 TT_BACKEND_EPOCH_QUEUE_NUM_SLOTS=1 python example.py
```

At this point, you will have a directory called `tt_build/test_out`. This directory contains the
intermediate files generated by Pybuda during the run. Debuda will use the files located in this directory to show
information about the run and help with debugging.

## Run Debuda

To run Debuda, use:
```
<path-to>/debuda.py
```

Debuda detects the most recent subdirectory of tt_build/ as the run directory to use. If you want to specify a different 
run directory, you can enter it as an argument: `<path-to>/debuda.py tt_build/test_out`.

## Debuda command prompt

Upon startup, Debuda will show a command prompt:
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 >
```
The command prompt shows the currently selected device and location on the device. Some commands, such as `stream-summary`,
use the currently-selected device and location to show information. For example, `stream-summary` will show the stream
the streams for the currently selected device and the core coordinate (18-18 in the example prompt above).

## Show list of commands: `help`

By entering `help` at the command prompt, Debuda will show a list of available commands:
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > help
Full Name            Short    Description
-------------------  -------  --------------------------------------------------------------------------------------------------------------------------------
exit                 x        Exits the program. The optional argument represents the exit code. Defaults to 0.
help                 h        Prints documentation summary. Use -v for details. If a command name is specified, it prints documentation for that command only.
reload               rl       Reloads files in debuda_commands directory. Useful for development of commands.
eval                 ev       Evaluates a Python expression.
export               xp       The result of the *export* command is similar to a 'core dump' in a conventional program.
buffer               b        Prints details on the buffer with a given ID.
burst-read-xy        brxy     Reads data from address 'addr' at noc0 location x-y of the chip associated with the currently selected graph.
core-debug-regs      cdr      Prints the state of the debug registers for core 'x-y'. If coordinates are not supplied, it iterates through all cores.
dump-debug-buffer    ddb      Prints a debug buffer.
dump-gpr             gpr      Prints all RISC-V registers for TRISC0, TRISC1, TRISC2 and Brisc on current core.
epoch-queue-summary  eq       Prints epoch queue summary
pci-write-xy         wxy      Writes data word to address 'addr' at noc0 location x-y of the current chip.
print-stream         s        Shows stream 'stream_id' at core 'x-y' on the current device.
status-register      srs      Prints BRISC and NCRISC status registers
device               d        Shows a device summary. When no argument is supplied, it iterates through all devices used by the
dram-queue           dq       Prints DRAM queue summary
graph                g        Changes the currently active graph to the one with the given name. Some operations only
hang-analysis        ha       Traverses all devices looking for the point of failure in the netlist execution. The traversal is
host-queue           hq       Prints the summary of queues on the host
info                 i        Reads one or more values from the chip based on the <access-path>. The <access-path> is a C-style access path
op-map               om       Shows a table of all operations in the current epoch, along with their location
pipe                 p        Prints details on the pipe with a given ID.
queue-summary        q        Prints summary of all queues. If the arguments are supplied, it prints the given queue contents.
tile-summary         t        Prints tile for the current stream in the currently active phase. If --raw is specified, it prints raw bytes.
```

Debuda supports command auto-completion. For example, entering `h` and pressing TAB will show all commands starting with `h`:
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > h
                                                                                                    help
                                                                                                    hang-analysis
                                                                                                    host-queue
```

## Display help for a specific command: `help <command>`
Entering `help <command>` will show the documentation for that command. For example, `help op-map` will show:
```
Full Name      Short    Description
-------------  -------  ---------------------------------------------------------------------------------
op-map         om
                        Usage:
                          op-map

                        Description:
                          Shows a table of all operations in the current epoch, along with their location
                          in the netlist and the NOC.

                        Examples:
                          om
```

## Top level operation view

Entering `op-map` (or `om`) will show a table of all operations in the current epoch, along with their location, size, etc:
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > om
Graph/Op                                        Op type      Epoch    Device  Netlist Loc    NOC Tr    Grid Size
----------------------------------------------  ---------  -------  --------  -------------  --------  -----------
fwd_0_0_temporal_epoch_0/add_6                  add              0         0  0,2            20-18     [1, 1]
fwd_0_0_temporal_epoch_0/matmul_1               matmul           0         0  0,0            18-18     [1, 1]
fwd_0_0_temporal_epoch_0/matmul_1_output_nop_0  nop              0         0  0,3            21-18     [1, 1]
fwd_0_0_temporal_epoch_0/matmul_4               matmul           0         0  0,1            19-18     [1, 1]
```

## Top level device view

Command `device-summary` (or `d`) shows a summary of all operations running on the devices overlaid on the geometry
of the chip. Note that core epoch ID is shown in the parenthesis after the operation name. For example, `matmul_1 (0)`
means that the current epoch of the operation is 0.

```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > d
==== Device 0
    00            01            02         03                         04  05  06  07
00  matmul_1 (0)  matmul_4 (0)  add_6 (0)  matmul_1_output_nop_0 (0)
01
02
03
04
05
06
07
```

The coordinate system used is this particualr example is `netlist` (or R,C). It is also possible to specify the
coordinate system to use. For example, `d 0 nocTr` will show the same information in a NOC-translated 
coordinate system (or X-Y). It is also possible to specify the contents for the cells. For example,
`d 0 nocTr block` will show the block type for each cell.

## Examine DRAM queues

Use `dram-queues` (or `dq`) command to take a look at the DRAM queues: `dq`
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > dq
DRAM queues for graph fwd_0_0_temporal_epoch_0
   Buffer ID  Op/Queue    Input Ops    Output Ops      dram_buf_flag    dram_io_flag    Channel  Address       RD ptr    WR ptr    Occupancy    Slots    Queue Size [bytes]
------------  ----------  -----------  ------------  ---------------  --------------  ---------  ----------  --------  --------  -----------  -------  --------------------
103000000000  weights1                 matmul_1                    1               0          0  0x28e0100          0         0            0        1                  4128
101000000000  weights2                 matmul_4                    1               0          0  0x40000000         0         0            0        1                  4128
```

## Examine host queues

Use `host-queues` (or `hq`) command to take a look at the host queues: `hq`
```
Current epoch:0(fwd_0_0_temporal_epoch_0) device:0 NocTr:18-18 netlist:0,0 stream:8 op:matmul_1 > hq
HOST queues for graph fwd_0_0_temporal_epoch_0
  Buffer name  Op                          Input Ops              Output Ops      dram_buf_flag    dram_io_flag  Address      Channel    RD ptr    WR ptr    Occupancy    Q slots    Q Size [bytes]
-------------  --------------------------  ---------------------  ------------  ---------------  --------------  ---------  ---------  --------  --------  -----------  ---------  ----------------
 107000000000  direct_pt.output_add_6      add_6                                              0               1  0x102a0            0         4         4            0          8             32768
 111000000000  act1                                               matmul_1                    0               1  0x20               0         4         4            0          8             33024
 105000000000  direct_pt.output_reshape_2  matmul_1_output_nop_0                              0               1  0x182e0            0         4         4            0          8             32768
 109000000000  act2                                               matmul_4                    0               1  0x8160             0         4         4            0          8             33024
```

Refer to command help for more examples on how to use commands.


# Scripting and Development

### Simple scripting with --commands CLI argument

When starting Debuda, you can specify a list of commands to run. For example:
```
dbd/debuda.py --commands "om-map; d; exit"
```
The above command will run `op-map`, followed by `d` and then exit. The output can then be redirected to a file for further processing.


### Developing new commands (debuda_commands/ folder)

This folder contains python files that define Debuda commands. To create a new command, create a new file in this folder.
The file name will be the command name. For example, `op-map.py` defines the `op-map` command. The file must contain
a function called `run()` which represents the main entry point for the command. The command_metadata dictionary
must also be defined. The metadata is used to define the command name, short name, description, etc. For example:

```
from tabulate import tabulate

command_metadata = {
        "short" : "om",
        "type" : "high-level",
        "description" : "Draws a mini map of the current epoch"
    }
```

<div style="page-break-after: always;"></div>
