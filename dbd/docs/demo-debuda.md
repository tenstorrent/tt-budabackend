# Debuda.py Tutorial

This short tutorial shows basics of Debuda.py debugging tool. You will introduce a hang by modifying a kernel
source code, and then use Debuda.py to determine the location of the hang. You will also examine some commonly-used
commands of Debuda.py.

## Compile test_op
```
make -j32 && make -j32 verif/op_tests/test_op
device/bin/silicon/reset.sh # If reset is available and you want to reset the device
./build/test/verif/op_tests/test_op --netlist dbd/test/netlists/netlist_multi_matmul_perf.yaml --seed 0 --silicon --timeout 60
```

Insert a hang into the __recip__ node (Grayskull only):
```
git apply dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-grayskull.patch
```
Similarly, for Wormhole:
```
git apply dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-wormhole.patch
```

Rerun the test_op, and you should see a hang:
```
                Runtime | INFO     | Running program 'program0'
                Runtime | INFO     |    Running graph 'test_op', epoch_id = 0, input_count = 1, pc = 4, device_id = 0, queue_settings.size() = 2
                Runtime | INFO     |    Wait for idle complete on devices {0}, caller = sync-on-execute
^C
```
Use ctrl-c to exit (or ctrl-\ if ctrl-c does not work).

## Debugging

Start Debuda:
```
dbd/debuda.py
```
Debuda detect the most recent subdirectory of tt_build as the run directory to use. If you do not want the most recent build, add `--netlist` argument followed by the path to the netlist file, and a 'positional' argument that points to the output directory. For example:
```
dbd/debuda.py --netlist my-netlist.yaml tt-build/my-run-dir/
```

Show command docs: `help`
```
Long Form          Short    Arg count    Arguments                        Description
-----------------  -------  -----------  -------------------------------  ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
exit               x        0/1          exit_code                        Exits the program. The optional argument represents the exit code. Defaults to 0.
help               h        0                                             Prints documentation.
burst-read-xy      brxy     4/5          x y addr burst_type format       Reads data from address 'addr' at noc0 location x-y of the chip associated with currently selected graph. Available formats: i8, i16, i32, hex8, hex16, hex32.
core-debug-regs    cdr      0/2          x y                              Shows debug registers for core 'x-y'. If coordinates are not supplied, it iterates through all cores.
dump-debug-buffer  ddb      2/3          id, num_words, format, x, y, c   Prints a debug buffer. 'id' - trisc 0|1|2 at current x-y. 'num_words' - number of words to dump. 'format' - i8, i16, i32, hex8, hex16, hex32. optional noc0 location x-y. optional c - chip_id
dump-gpr           gpr      0                                             Prints general purpose registers at Risc cores.
full-dump          fd       0                                             Performs a full stream dump at current x-y location.
pci-raw-read       pcir     1            addr                             Reads data from PCI BAR at address 'addr'
pci-raw-write      pciw     2            addr data                        Writes 'data' word to PCI BAR at address 'addr'
pci-read-xy        rxy      3            x y addr                         Reads data word from address 'addr' at noc0 location x-y of the current chip.
pci-write-xy       wxy      4            x y addr value                   Writes word 'value' to address 'addr' at noc0 location x-y of the current chip.
stream             s        3            x y stream_id                    Shows stream 'stream_id' at core 'x-y'
status-register    srs      1            verbosity                        Prints brisc and ncrisc status registers. Verbosity can be 0, 1 or 2.
buffer             b        1            buffer_id_or_op_name             Prints details on the buffer with a given id, or buffer(s) mapped to a given operation.
core               c        2            x y                              Shows summary for core 'x-y'.
device             d        0/1          device_id                        Shows a device summary. When no argument is supplied, it iterates through all devices.
dram-queue         dq       0                                             Prints DRAM queue summary
dump-tile          t        2            tile_id raw                      Prints tile for the current stream in the currently active phase. If raw=1, it prints raw bytes.
graph              g        1            graph_name                       Changes the current active graph.
hang-analysis      ha       0                                             Prints a summary of all operations in the netlist.
host-queue         hq       0                                             Prints host queue summary.
op-map             om       0                                             Draws a mini map of the current epoch.
pipe               p        1            pipe_id                          Prints details on the pipe with ID 'pipe_id'.
queue-summary      q        0/1/3        queue_name start_addr num_bytes  Prints summary of all queues. If the arguments are supplied, it prints given queue contents.
```

Debuda will show a command prompt showing the currently selected epoch, graph, device, etc. Some commands (such as stream-summary) will use the currently selected state to narrow down what to show.

## Top level view

Show cores with configured streams: `d`
```
==== Device 0 
Loading '/home/ihamer/work/budabackend/device/grayskull_10x12.yaml'
00  +   +   +   +   .   .   .   .   .   .   .   .   Coordinate system: RC
01  .   .   .   .   .   .   .   .   .   .   .   .   . - Functional worker
02  .   .   +   .   +   .   +   .   .   .   .   .   + - Functional worker with configured stream(s)
03  .   .   .   .   .   .   .   .   .   .   .   .
04  .   .   .   .   .   .   .   .   .   .   .   .
05  .   .   .   .   .   .   .   .   .   .   .   .
06  .   .   .   .   .   .   .   .   .   .   .   .
07  .   .   .   .   .   .   .   .   .   .   .   .
08  .   .   .   .   .   .   .   .   .   .   .   .
09  .   .   .   .   .   .   .   .   .   .   .   .
    00  01  02  03  04  05  06  07  08  09  10  11
```
The coordinate system used is 'row, column' (`R,C`). When NOC0 coordinates are used, they are separated by a hyphen instead (e.g. `X-Y`).

Operation table can be shown with: `op-map`
```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > op-map
Graph/Op         Op type       Epoch    Device  Grid Loc    NOC0 Loc    Grid Size
---------------  ----------  -------  --------  ----------  ----------  -----------
test_op/add1     add               0         0  [2, 6]      7-3         [1, 1]
test_op/d_op3    datacopy          0         0  [0, 3]      4-1         [1, 1]
test_op/f_op0    datacopy          0         0  [0, 0]      1-1         [1, 1]
test_op/f_op1    datacopy          0         0  [0, 1]      2-1         [1, 1]
test_op/matmul1  matmul            0         0  [0, 2]      3-1         [1, 1]
test_op/matmul2  matmul            0         0  [2, 4]      5-3         [1, 1]
test_op/recip    reciprocal        0         0  [2, 2]      3-3         [1, 1]
```


## Examine DRAM queues
- Use `dram-queues` (or `dq`) command to take a look at the DRAM queues: `dq`

```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > dq
DRAM queues for graph test_op
  Buffer ID  Op      Input Ops    Output Ops      dram_buf_flag    dram_io_flag    Channel  Address       RD ptr    WR ptr    Occupancy    Slots    Size [bytes]
-----------  ------  -----------  ------------  ---------------  --------------  ---------  ----------  --------  --------  -----------  -------  --------------
10000010000  output  d_op3                                    0               1          0  0x32000000         0         0            0        1          131072
10000030000  input1               f_op1                       1               0          0  0x31000000         0         1            1        1           66560
10000050000  input0               f_op0                       1               0          0  0x30000000         0         1            1        1           66560
```

## Hang analysis

- Use `hang-analysis` (or `ha`) to run the analysis of stuck streams:
```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > ha
Analyzing device 0
  Reading epochs for locations
    Analyzing graph test_op ( epoch 0 )
  Device ID    Epoch ID  Graph Name    Source    Destination    Hang Reason        Stream     Additional Stream Info
-----------  ----------  ------------  --------  -------------  -----------------  ---------  ------------------------------------------------------------------
          0           0  test_op       op2:Op    d_op3:Op       Data not received  (4, 1, 8)  {'phase': 1, 'msg_received': 0, 'msg_remaining': 64, 'dest': True}
Speed dial:
  #  Description             Command
---  ----------------------  ---------
  0  Go to stream (4, 1, 8)  s 4 1 8
```

- Navigate to stream 4-1-8 by using speed-dial: `0`
```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > 0
Non-idle streams               Registers                                                     Stream (blob.yaml)                                       Buffer 10000060000                                       Pipe 10000150000
------------------  ---------  ------------------------------------------------  ----------  ----------------------------  -------------------------  ----------------------------  -------------------------  --------------------  -------------------------
8                   RS-7-3-24  STREAM_ID                                         8           buf_addr                      241664 (0x3b000)           md_op_name                    d_op3                      id                    10000150000 (0x2540e2df0)
24                             PHASE_AUTO_CFG_PTR (word addr)                    0x1b414     buf_id                        10000060000 (0x2540cce60)  id                            0                          input_list            [10000130000]
                               CURR_PHASE                                        1           buf_size                      33280 (0x8200)             uniqid                        10000060000 (0x2540cce60)  pipe_periodic_repeat  0
                               CURR_PHASE_NUM_MSGS_REMAINING                     64          buf_space_available_ack_thr   0                          epoch_tiles                   64 (0x40)                  pipe_consumer_repeat  1
                               NUM_MSGS_RECEIVED                                 0           data_auto_send                True                       chip_id                       [0]                        output_list           [10000060000]
                               NEXT_MSG_ADDR                                     0x3b00      dest                          []                         core_coordinates              (0, 3)                     incoming_noc_id       0
                               NEXT_MSG_SIZE                                     0x0         fork_stream_ids               []                         size_tiles                    16 (0x10)                  outgoing_noc_id       0
                               OUTGOING_DATA_NOC                                 0           input_index                   0                          scatter_gather_num_tiles      64 (0x40)                  mcast_core_rc         [0, 0, 2]
                               LOCAL_SOURCES_CONNECTED                           0           intermediate                  False                      is_scatter                    0
                               SOURCE_ENDPOINT                                   0           local_receiver                True                       replicate                     0
                               REMOTE_SOURCE                                     1           local_receiver_tile_clearing  True                       tile_size                     2080 (0x820)
...
Speed dial:
  #  Description                    Command
---  -----------------------------  ---------
  0  Go to source (No op at [2,6])  s 7 3 24
```

- Show NCRISC/BRISC registers `srs 0`:
```
Current epoch:0(test_op) device:0 core:4-1 rc:0,3 stream:8 > srs 0
Reading status registers on device 0...
NCRISC status summary:
X-Y    Status    Status Description
-----  --------  -----------------------------
1-1    11111111  Main loop begin
2-1    11111111  Main loop begin
3-1    c0000000  Load queue pointers
3-3    11111111  Main loop begin
4-1    f0000000  Which stream will write queue
5-3    11111111  Main loop begin
7-3    c0000000  Load queue pointers
BRISC status summary:
X-Y    Status    Status Description
-----  --------  ------------------------------------------------------
1-1    b0000000  Stream restart check
2-1    b0000000  Stream restart check
3-1    b0000000  Stream restart check
3-3    c0000000  Check whether unpack stream has data
4-1    c0000000  Check whether unpack stream has data
5-3    c0000000  Check whether unpack stream has data
7-3    e0000000  Check and push pack stream that has data (TM ops only)
```
Since this is live data, re-running the command might show different values.

Use `srs 1` and `srs 2` for more verbose output.

Show core debug registers: `cdr 3 3`

```
Current epoch:0(test_op) device:0 core:3-3 rc:2,2 stream:8 > cdr 3 3
=== Debug registers for core 3-3 ===
T0               T1               T2               FW
-------  ------  -------  ------  -------  ------  -------  ------
DBG[0]   0x0000  DBG[0]   0x0000  DBG[0]   0x0000  DBG[0]   0x0000
DBG[1]   0x0000  DBG[1]   0x0000  DBG[1]   0x0000  DBG[1]   0x0000
DBG[2]   0x0000  DBG[2]   0x0000  DBG[2]   0x0000  DBG[2]   0x0000
DBG[3]   0x0000  DBG[3]   0x0000  DBG[3]   0x0000  DBG[3]   0x0000
DBG[4]   0x0000  DBG[4]   0x0000  DBG[4]   0x0000  DBG[4]   0x0000
DBG[5]   0x0000  DBG[5]   0x0000  DBG[5]   0x0000  DBG[5]   0x0000
DBG[6]   0x0000  DBG[6]   0x0000  DBG[6]   0x0000  DBG[6]   0x0000
...
```


## Scripting

### Simple scripting with --commands argument

When debuda encounters `--command` argument it will treat it as a semicolon-separated list of commands to run.
If the last command is `exit`, Debuda will terminate. For example:
```
dbd/debuda.py --commands "cdr 3 3; exit"
```


### Commands/ folder

A python file placed into commands/ folder becomes a command available to Debuda.

Define the metadata for the command to be available in the help menu. For example, this is the metadata for `op-map` command:
```c++
from tabulate import tabulate

command_metadata = {
        "short" : "om",
        "type" : "high-level",
        "description" : "Draws a mini map of the current epoch"
    }
```
Then define function run(). This example shows the `op-map` command:
```python
def run(args, context, ui_state = None):
    navigation_suggestions = []

    rows = []
    for graph_name, graph in context.netlist.graphs.items():
        epoch_id = context.netlist.graph_name_to_epoch_id (graph_name)
        device_id = context.netlist.graph_name_to_device_id (graph_name)
        device = context.devices[device_id]
        for op_name in graph.op_names():
            op = graph.root[op_name]
            grid_loc = op['grid_loc']
            noc0_x, noc0_y = device.tensix_to_noc0 (grid_loc[0], grid_loc[1])
            row = [ f"{graph_name}/{op_name}", op['type'], epoch_id, f"{graph.root['target_device']}", f"{grid_loc}", f"{noc0_x}-{noc0_y}", f"{op['grid_size']}"]
            rows.append (row)

    print (tabulate(rows, headers = [ "Op", "Op type", "Epoch", "Device", "Grid Loc", "NOC0 Loc", "Grid Size" ]))

    return navigation_suggestions
```

## Analyze blocked streams (Obsolete)

- Condensed version: `abs`
```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > abs
Analyzing device 1/1
Loading device.read_all_stream_registers () cache from file device0-stream-cache.pkl
X-Y    Op                          Stream  Type      Epoch    Phase    MSGS_REMAINING    MSGS_RECEIVED  Depends on    State    Flag
-----  ------------------------  --------  ------  -------  -------  ----------------  ---------------  ------------  -------  ------
3-3    test_op/recip:reciprocal         8  input         0        1                32                2  2-1           Active   All core inputs ready, but no output generated
Speed dial:
  #  Description    Command
---  -------------  ---------
  0  Go to stream   s 3 3 8
  ```

- Detailed version: `abs 1`

```
X-Y    Op                          Stream  Type            Epoch    Phase    MSGS_REMAINING    MSGS_RECEIVED  Depends on       State    Flag
-----  ------------------------  --------  ------------  -------  -------  ----------------  ---------------  ---------------  -------  ------
2-1    test_op/f_op1:datacopy           8  input               0        1                 0                0  2-1
                                       24  output              0        1                 0                0
                                       40  op-relay            0        1                 0                0
                                       41  op-relay            0        1                24                2                   Active
3-1    test_op/matmul1:matmul           8  input               0        1                 0                0  2-1
                                        9  input               0        1                 0                0
                                       24  output              0        1                56                2                   Active
                                       32  intermediate        0        1                64                0
3-3    test_op/recip:reciprocal         8  input               0        1                32                2  2-1              Active   All core inputs ready, but no output generated
                                       24  output              0        1                32                0
5-3    test_op/matmul2:matmul           8  input               0        1                32                2  2-1 3-3          Active
                                        9  input               0        1                32                0
                                       24  output              0        1                64                0
                                       32  intermediate        0        1                64                0
7-3    test_op/add1:add                 8  input               0        1                64                2  2-1 5-3 3-1 3-3  Active
                                        9  input               0        1                64                0
                                       24  output              0        1                64                0
Speed dial:
  #  Description    Command
---  -------------  ---------
  0  Go to stream   s 2 1 41
  1  Go to stream   s 3 1 24
  2  Go to stream   s 3 3 8
  3  Go to stream   s 5 3 8
  4  Go to stream   s 7 3 8
  ```

## For reference: How to render a graph in ASCII

Graph-Easy is a Perl-based program that renders .dot graphs in ASCII. In this tutorial, we only use it to show
a graphical representation of the netlist graph.

```
sudo apt install cpanminus
sudo cpanm Graph::Easy
```
Then add this alias to render the graph to Left->Right:
```
alias graph='sed '\''s/digraph G {/digraph G {\nrankdir=LR/g'\'' | graph-easy --as boxart 2>/dev/null'
```
Now, we can do something like this to render as simple graph
```
cat graph_test_op.netlist.dump | graph
```
You should see something like this:
```
     ┌────────┐  0   ┌───────┐  0   ┌─────────┐  0   ┌──────┐  0   ┌───────┐  0   ┌────────┐
     │ input0 │ ───▶ │ f_op0 │ ───▶ │ matmul1 │ ───▶ │ add1 │ ───▶ │ d_op3 │ ───▶ │ output │
     └────────┘      └───────┘      └─────────┘      └──────┘      └───────┘      └────────┘
                       │              ▲                ▲
  ┌────────────────────┘              │                │
  │                                   │                │
  │  ┌────────┐  0   ┌───────┐  1     │                │
  │  │ input1 │ ───▶ │ f_op1 │ ───────┘                │
  │  └────────┘      └───────┘                         │
  │                    │                               │
  │                    │ 0                             │
  │                    ▼                               │
  │                  ┌───────┐  1   ┌─────────┐  1     │
  │                  │ recip │ ───▶ │ matmul2 │ ───────┘
  │                  └───────┘      └─────────┘
  │   0                               ▲
  └───────────────────────────────────┘
  ```

As of June 15 2023, to generate the graphs, one must manually modify struct tt_backend_config::dump_graphs to true. 
Then, when running the test, watch from message `Dumping graphviz: ...` to see the localtion of the files being dumped.