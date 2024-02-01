\page backend_overview Backend Runtime Overview

# Backend Runtime Overview

- [**Loader Overview**](#loader-overview)
- [**Netlist**](#netlist)
  - [**Tensor**](#tensor)
  - [**Queue**](#queue)
  - [**Graph**](#graph)
  - [**Ops**](#ops)
  - [**Program**](#program)
- [**Device**](#device)

## Loader Overview

The block diagram below illustrates how pybuda dispatches and runs a model on devices.

- Pybuda has two modes of operation
  - Concurrent (default): Launches three threads: (This mode is necessary for performance).
    1. Runtime thread: Generates the program binaries and dispatches epochs on device.
    2. Push Input thread: Tilizes and pushes the input tensors to device.
    3. Pop Output thread: Collects output tensors from device.
  - Sequential: Only a single thread pushes the inputs, dispatches the epochs on device and pops the output.

![21](images/21.png)

Loader (the `Dispatch Program` section) for each program, executes the following:

- Compiles and generates the binaries for all kernels and overlay blob.
- For each `epoch` (each `run graph` instruction in the figure), it assembles the binaries and the epoch command.
- Waits for space in the epoch command queue and binary cache on device.
- If there was space, it pushes a new command a set of new binaries.

![23](images/23.png)

## Netlist

Netlist is the interface between pybuda and budabackend. This is considered the input to backend. Loader uses the netlist, to generate the commands/binaries and dispatch them to device.

Netlist generally contains the following sections:

| Netlist sections | Subsections | Description |
|----------------------|-----------------|-----------------|
| devices |  |  |
|  | arch | Specifies the architecture of the target device |
| queues |  | Lists all the queues that will be used by any of the graphs in the netlist. Queue is a collection of buffers in DRAM or on host system memory |
|  | List of queues | For the attributes for each queue [See](#queue) |
| graphs |  | List of all the graphs. Each graph in this context refers to a set of ops that can fit on a single chip with the architecture specified under `arch` |
|  | target_device | Target device ID that any instantiation of this graph will be executed on |
|  | input_count | Number of input tensors for this graph. The dimensions of each input tensor are determined by t \* mblock \* ublock tiles |
| program |  | List of all programs. Each program will specify which graphs in what order must be executed. It also configures the queue settings that are used by each epoch. |

### Tensor

The dimension of each tensor can be derived by the following parameters: t, mblock, ublock. The figure below illustrates how the tensor is constructed from these parameters:

![18](images/18.png)

- tile_size = TILE_HEADER_SIZE + PADDING + SHARED_EXPONENT_SIZE + 32 \* 32 \* DATUM_SIZE
  - TILE_HEADER_SIZE = 16B
  - PADDING = 16B
  - DATUM_SIZE can be derived from the data-format.
    - For formats without shared exponent, SHARED_EXPONENT_SIZE = 0
    - For formats with shared exponent, each 16 datums share an exponent byte, hence SHARED_EXPONENT_SIZE = 64B for the entire tile.
- `input_count` determines how many of these tensors are streamed through the graph.

### Queue

Each queue is a collection of buffers in DRAM or on host system memory.

Queues under netlist have the following attributes:

| Queue attributes | Description |
|----------------------|-----------------|
| type | Type of the queues. Currently, we have the following types:<br>- queue: Acts as a queue, where the wr pointer increments after writing an entry, and the rd pointer increments after reading an entry.<br>- ram: Random access memory, with rd/wr based on the current local rd/wr pointers. The pointers do not automatically increment after every rd/wr |
| input | The input source to the queue. It can be set to `Host` or any `<OP_NAME>`. |
| queue dimensions | Same as op tensor dimensions. [See](#tensor). Includes the following fields:<br>- entries<br>- t<br>- mblock<br>- ublock |
| df | Data format of the values in the queue |
| target_device | Device ID that the queue resides |
| loc | - dram: Queue resides on DRAM on the `target_device` device<br>- host: Queue resides in system memory |
| dram/host | 2D list of the location of all buffers. Each inner list consists of `\[dram_channel_idx, dram_addr\]`. If `loc` is set to `host`, it will be `\[0, host_memory_mapped_addr\]` |

### Graph

List of all the graphs. Each graph in this context refers to a set of ops that can fit on a single chip with the architecture specified under `arch`.

- An epoch is one instantiation of this graph. Which consists of all ops in the graph to execute `input_count` number of tensors. | Graph attributes | Description | |----------------------|-----------------| | target_device | Target device ID that any instantiation of this graph will be executed on | | input_count | Number of input tensors for this graph. The dimensions of each input tensor are determined by t \* mblock \* ublock tiles | | List of ops | List of all the ops that will be placed on the `target_device` in every epoch that runs this graph. [See](#ops) |

### Ops

| Op attributes | Description |
|-------------------|-----------------|
| type | Type of the op. e.g. `matmul` or `gelu` or `nop` |
| grid_loc | logical `\[row, column\]` or equivalently `\[y, x\]` location on the grid of worker cores. The logical row/col index start from 0 and increment.<br>To convert to physical coord use soc_descriptor (e.g. `device/wormhole_b0_80_arch.yaml` for b0).<br>e.g. logical `\[7, 6\]` translates to physical `\[8, 9\]`.<br>Note that physical worker coords start from \[1, 1\] and one row and one col are skipped for dram and eth |
| grid_size | number of cores in each row and column `\[row, column\]` |
| inputs | Inputs to the op. It can either be another op or a queue |
| in_df | Input data format for every operand. This should match the output data format of the producer node |
| acc_df |  |
| intermed_df | The data format for the intermediate buffers |
| output_df | The data format for the output buffers. This should match the input data format of the consumer node. |
| ublock_order | ublock scan order. `r` for row major. `c` for column major. |
| buf_size_mb | Used to determine the size of the output buffer. `output_buffer_size = buf_size_mb \* mblock_size`<br>`mblock_size = mblock_r \* mblock_c \* ublock_r \* ublock_c \* 32 \* 32 \* tile_size` |
| math_fidelity |  |
| untilize_output | If set to true, the op will untilize the output tensor and push to the host queue. This usually is only used for the last op in the graph that writes the output back to host |
| t, mblock, ublock | Dimensions of the output tensor |
| input\_#\_tms | The type of TM applied to operand # |
| input_buf_min_size_tiles | The minimum input buffer size for each operand (in tiles). If it is set to 0, the default buffer size is used |
| attributes | List of additional attributes |
| attributes-mk | For Matmul ops, specifies inner macro block dim. |
| attributes-uk | For Matmul ops, specifies inner micro block dim. |

Given the following ops/queues in the netlist, it will be placed and run on the device as depicted below:

```yaml
queues:
  q0: {type: queue, input: HOST    , entries: 1, grid_size: [1, 2], t: 1, mblock: [2, 2], ublock: [1, 1], df: Float16, target_device: 0, loc: dram, dram: [[0, 0x10000000], [2, 0x11000000]]}
  q2: {type: queue, input: unary2 , entries: 10240, grid_size: [1, 2], t: 1, mblock: [2, 2], ublock: [1, 1], df: Float16, target_device: 0, loc: dram, dram: [[1, 0x10000000], [3, 0x10000000]]}

graphs:
  test_binary:
    target_device: 0
    input_count: 128
    unary0: {type: nop, grid_loc: [0, 0], grid_size: [1, 2], inputs: [q0], in_df: [Float16], acc_df: Float16, out_df: Float16,  intermed_df: Float16, ublock_order: r, buf_size_mb: 2, math_fidelity: HiFi3, untilize_output: false, t: 1, mblock: [2, 2], ublock: [2, 4] }
    unary1: {type: nop, grid_loc: [1, 0], grid_size: [2, 2], inputs: [unary0], in_df: [Float16], acc_df: Float16, out_df: Float16,  intermed_df: Float16, ublock_order: r, buf_size_mb: 2, math_fidelity: HiFi3, untilize_output: false, t: 1, mblock: [1, 2], ublock: [2, 4] }
    unary2: {type: nop, grid_loc: [4, 0], grid_size: [1, 2], inputs: [unary1], in_df: [Float16], acc_df: Float16, out_df: Float16,  intermed_df: Float16, ublock_order: r, buf_size_mb: 2, math_fidelity: HiFi3, untilize_output: false, t: 1, mblock: [2, 2], ublock: [2, 4] }
```

![17](images/17.png)

### Program

Each program describes:

- What graphs must be executed in what order. Each instantiation of a graph is called an `epoch`
- Queue settings update. Where the queue header including rd/wr pointers can be updated.

## Device

Each worker core has 5 risc processors:

- Brisc: Brisc fw runs core startup and stream reconfiguration.
- Ncrisc: Runs Fw for reading and writing to dram. Dispatching commands to Triscs.
- Triscs:
  - Unpacker: Runs kernel for configuring and issuing unpacker instructions (reading data from l1).
  - Math: Runs kernel for configuring and issuing math instructions.
  - Pack: Runs kernel for configuring and issuing pack instructions (writing data back to l1).
- There is one epoch command queue for each tensix core. This can be either placed on l1 of the corresponding core, or queues for all cores, can be distributed across all dram channels.

Epoch command queue num slots by default = 32.

- There is also one binaries cache for each core. Where the kernel binaries and overlay blob (will be referred to as Binaries from now on) is stored. This cache for all cores, is distributed across all dram channels.

Binaries Cache num slots by default = 32.

- For each epoch, host pushes the Binaries and then the epoch command to every Tensix core of every active chip.
  - Host can populate the Binaries cache when program is initialized. In this case, loader fills the entire cache (Binaries for 32 graphs) and at runtime, it will only update the cache, if the next epoch command runs a graph which its binaries do not exist in the cache.
- On the device side, Ncrisc of each core polls its corresponding epoch command queue, and it starts launching the epoch when it receives a new epoch command.
  - The address to the corresponding binaries is populated in the epoch command. Ncrisc will read the binaries after it sees a command.
  - Ncrisc, at a high level, will go through the following stages for every epoch:

![20](images/20.png)

- During each epoch, for each core, unpack, math, and pack will operate as depicted below:

![unpack_math_pack](images/unpack_math_pack.png)