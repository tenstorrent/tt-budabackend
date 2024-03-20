# Netlist Specification {#netlist}

Netlist is Tenstorrent(TT) BUDA software stack's intermediate representation (IR) formatted as a YAML file. It is used as the workload description to BUDA backend.

Post-optimization and post-placement graphs are found in the netlist, represented as nodes (`queues` and `ops`) or edges (connections between nodes). 

In addition to graph level information, netlists also contain runtime information in `programs` section, with a custom netlist specfic instruction set (ISA).

It is expected that BUDA backend will not make any graph level optimizations, and can only make runtime level optimizations (modifications to `programs` section).

# Specification

Netlist is a YAML file with the a few standard sections - `devices`, `queues`, `graphs`, `fused_ops`, and `programs`, as well as several optional sections found in `Optional Sections` below.

## Devices

### arch

The target device arch that the netlist is compiled for, one or more can be specified. Currently `grayskull`, `wormhole`, `wormhole_b` are supported.

## Queues

Queues is a misnomer since they are generic IO nodes that are passive data nodes in the graph with globally unique names. Each IO node can be either a `ram` or a `queue`, with different semantics and properties. The following are fields specific to these IO nodes:

| IO Field  | Description                                                                                                                                       |
| ------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| input   | The producer of the queue, must be an `op` node or `HOST` which represents the host CPU.                                                           |
| type   | The type of the queue, currently supported types are `ram`, which represents random access memory without flow control, and `queue`, which represents a FIFO queue with wr_ptr and rd_ptr control. |
| entries| The number of entries, typically a multiple of `input_count` found in the producer or consumer graphs for activation `queue`, and 1 for parameter `ram` or constant `queue`. |
|grid_size| A grid of [rows, cols] of data buffers, with each buffer having `entries` number of entries. The total number of buffers found in the grid is `grid_size[0] * grid_size[1]`, matching the number of allocated buffers found in `dram` or `host`. |
| t      | The "time" dimension of the data, used for temporal streaming of data to the device. At the tensor level this is implemented as the depth dimension, and also the outermost dimension of each entry. Each `t` slice is a macroblock. |
| mblock | A grid of [rows, cols] of microblocks found within each macroblock. |
| ublock | A grid of [rows, cols] of tiles found within each microblock. |
| ublock_order | The memory layout's order of microblocks within each microblock, currently supported orders are `r` or row-major and `c` or column-major, if unspecified the default is `r`. |
| layout | Describes the data layout in the underlying memory. By default `tilized` layout is used, and optionally user can set `flat` or other custom layouts. |
| alias  | Only used in `dual-view` IO, this aliases the current IO as an alternative view of the specified IO. Both views may have different blocking schemes but must have the same entry size and the same underlying memory (`loc` and `dram`/`host` addresses). |
| df     | The data format of the tensors in the queue, currently supported formats are `Float32`, `Float16`/`Float16_b`(bfloat16), `RawUInt32`/`RawUInt16`/`RawUInt8`, `Bfp8`/`Bfp8_b`, `Bfp4`/`Bfp4_b`, `Bfp2`/`Bfp2_b`. |
| target_device | The target device id that the queue is allocated on, the type of device memory is specified by `loc`. |
| loc    | The location of the queue, currently supported locations are `dram` and `host`. |
|dram   | Only valid if `loc` is `dram`, specifies the dram bank and the bank's local address that the `queue`/`ram` is allocated on. The number and order of allocations matches the number and order of buffers found in `grid_size` (row-major order). |
| host   | Only valid if `loc` is `host`, specifies the host address that the `queue`/`ram` is allocated on. The number and order of allocations matches the number and order of buffers found in `grid_size` (row-major order). |

## Graphs

Graphs are compute graphs with globally unique names, where each graph may be either an independent graph or a sub-graph mapped to a spatial epoch. 

Graphs are represented as a map of `ops` with connections between them either directly or indirectly via queues. 

OPs are active compute nodes in the graph with globally unique names. They share many of the same attributes as queues, but with additional attributes specific to compute nodes.

### target_device

The target device id that the graph is mapped to.

### input_count

The number of input/output activations to the graph. This is used to determine the number of entries input/output activations read/written by the graph, and also the number of activations to be computed by the ops.


### OPs

OPs are represented as a map of nodes with globally unique names. Both graphs and op nodes can be connected via either queues or op nodes. The following are the attributes of an op node.

| OP Field     | Description | 
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| type         | The compute type the current op, this is used to determine the compute kernel to be used for the op. Each compute type has a different set of supported attributes, and different numbers of inputs and outputs. |
| grid_loc     | A coordinate location that marks the top-left corner of the grid in the spatial epoch. The grid size is determined by `grid_size`. |
| grid_size    | A grid of [rows, cols] of Tensix cores assigned to the current op. The total number of cores found in the grid is `grid_size[0] * grid_size[1]`. No two ops within the same graph may share the same cores, ie. no overlapping grids. |
| inputs       | Input operands to the current op, must be either a `queue`/`ram` or another `op`. If the input is another `op`, then the current op is a consumer of the input op's output. If the input is a `queue`/`ram`, then the current op is an active reader. |
| input_x_tms  | Input operand x's tensor manipulation (TM) spec. The TM operation is implemented on the data transfer path between nodes via pipes. Pipes and the implementation details are outside the scope of the netlist and are handled by the BUDA backend. |
| input_buf_min_size_tiles | The minimum size of the input operand buffer in tiles, if unspecified it's up to the BUDA backend to determine the size. |
| t            | The "time" dimension of the data, used for temporal streaming of data to the op. At the tensor level this is implemented as the depth dimension, and also the outermost dimension of each entry. Each `t` slice is a macroblock. |
| mblock       | A grid of [rows, cols] of microblocks found within each macroblock. |
| ublock       | A grid of [rows, cols] of tiles found within each microblock. |
| buf_size_mb  | The size of the output buffer in macroblocks, if unspecified the default is 1. |
| ublock_order | The memory layout's order of microblocks within each microblock, currently supported orders are `r` or row-major and `c` or column-major, if unspecified the default is `r`. |
| in_df        | The data format of the input operands to the op. The number of elements matches the number of input operands specified in `input`. The data format of each element should match its corresponding `input` operand's data format. |
| out_df       | The data format of the output operand to the op. The data format of each element should match the output `queue`/`ram`'s or the consumer operand's data format. |
| intermed_df  | The data format of the intermediate operands to the op. |
| acc_df       | The data format used by the Tensix core math unit's destination register accumulator. |
| math_fidelity | The math fidelity of the op, higher the fidelity the more accurate the result, but also the more compute cycles required. Currently supported fidelity levels from lowest to highest are `LoFi`, `HiFi2`, `HiFi3`, `HiFi4`. |
| attributes   | A map of attributes specific to the op's compute `type`. |
| untilize_output | A boolean flag that specifies whether the output of the op should be untilized. If set to `true`, then all device specific tiling and blocking are removed, and the final output is in the same layout found in a torch tensor. This is typically used for final output back to the host python/pybuda runtime. If unspecified the default is `false`. |
| grid_transpose | A boolean flag that specifies whether the op grid should be transposed. If set to `true`, the op grid is transposed, ie. the rows and columns are swapped. If unspecified the default is `false`. |
| gradient_op  | A boolean flag that specifies whether the op is a gradient accumulation op. If unspecified the default is `false`. |

#### Attributes -- WIP

| OP Attribute Field | Description | 
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| m_k | Specify how many mblocks of accumulation is needed for the blocked matrix multiply -- Affects how often we spill and reload from L1 in grayskull |
| u_kt | Specify how many ublocks of accumulation is done first before we move to the next mblock |

## Fused OPs

```yaml
fused_ops:
  0: 
    inputs: 3
    intermediates: 0
    schedules: 
      -
        - multiply_16: { type: multiply, inputs: [input0, input1], mblock: [2, 1], ublock: [2, 4], output: dest}
        - add_17: { type: add, inputs: [dest, input2], input_1_tms: [tile_broadcast: r], mblock: [2, 1], ublock: [2, 4], output: dest}
        - softmax_18.dc.exp.0: { type: exp, inputs: [dest], mblock: [2, 1], ublock: [2, 4], output: output}
```

Fused OPs are a special type of OP that is used to fuse multiple ops into a single op. This is used to reduce the number of ops in the graph, and also to reduce the core-to-core data transfers between ops. 

Each fused OP has a unique id with its own set of schedule. To use a fused OP, it must be referred to with `type` set to `fused_op` and `attributes` map containing `fused_op_id: <fused_op_id>`.

### inputs

Inputs are references to the list of inputs found in the original OP in the graph. Each distinct input can be referred to as `input` followed by an integer id. 

### intermediates

Intermediates are local buffers in allocated in Tensix core's private L1 memory. The number of intermediates is determined by the number of distinct buffers found in the schedule, and each distinct buffer can be referred to as `interm` followed by an integer id. They are sized as 2x producer op's `ublock` size in bytes.

### schedules

A subset of the OPs syntax is used to specify the schedule of the fused op. With notable differences:

| Fused OP Field | Description | 
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| inputs       | Input operands to the current op, keywords `input` and `interm` followed by an integer id are used (`inputs` and `intermediates` rules above apply). |
| output       | Output operands to the current op, keywords `interm`, `dest` and `output` are used. The output node `output` is mapped to the original output associated with the original OP in the graph. Fast access destination registers local to the math engine can be referred to as `dest`. (`inputs` and `intermediates` rules above apply)|

## Programs

Programs are a collection of programs without an assumption on the user's specific execution order or sequential/parallel execution model. However it is common for PyBUDA to generate a program for each phase of training, ie. forward, backward, and optimizer programs.

Each program is a list of interpretable code sequences that include which graphs to execute when and program variable updates. It runs like a sequential assembly program with a custom instruction set:

### var

`var: [<var_name>, <var_name>, ...]`

`var: {<var_name>:<var_val>, <var_name>:<var_val>, ...]`

Variables that are local to current program call, meaning it's lifetime begins with `var` instrunction and ends with the last instruction of the program. Variables can be used as arguments in `varinst` instructions, or used to bind values to `queue_setting` fields.

### staticvar

`staticvar: [<var_name>, <var_name>, ...]`

`staticvar: {<var_name>:<var_val>, <var_name>:<var_val>, ...]`

Variables that are static allocated to the program, meaning its lifetime spans across all calls of the same program calls. The scope of each static variable is the current program, hence it cannot be used across programs, any cross program information sharing must be done via user managed `param` variables. Or the use of shadow variables, which are static variables that mimic the actions of another program's static variable.

### param

`param: [<param_name>, <param_name>, ...]`

Parameters passed in by the caller to the program. Each paramer can be used anywhere where a variable is used in the program.

### varinst

`varinst: [<out>, <op_code>, <args>]`

Variable instructions are used to manipulate variables and store the result in `out`, the type of arithmetic operation is defined by `opcode`, and the operands are specified by `args`. 

The instruction opcode can be one of the following:

| OpCode       | Description                    | 
| ------------ | ------------------------------ | 
| set          | Sets the output variable to the first argument. | 
| add          | Sets the output variable to the sum of the first two arguments. | 
| mul          | Sets the output variable to the product of the first two arguments. | 
| inc          | Increments the output variable by the first argument. | 
| incwrap      | Increments the output variable by the first argument, and wraps around if the output variable exceeds the second argument. |

### allocate_queue

`allocate_queue: [<io_name>, <io_name>, ...]`

Allocates queues for the input and output IO used by the graph. Each allocate queues instruction must contain a list of IO names whose lifetimes are defined by the `allocate_queues` and `deallocate_queues` instructions.

### deallocate_queue

`deallocate_queue: [<io_name>, <io_name>, ...]`

Deallocates queues for the input and output IO used by the graph. Each deallocate queues instruction must contain a list of IO names whose lifetimes are defined by the `allocate_queues` and `deallocate_queues` instructions.

### loop

`loop: $loop_count`

The start of a loop. The next argument defines `loop_count` where the loop is executed `loop_count` times. The loop body is defined by the instructions between `loop` and `endloop`.

### endloop

`endloop`

Marks the end of a loop body, which is defined by the instructions between `loop` and `endloop`. The loop counter is incremented and the loop is checked for termination.

### endprogram

`endprogram`

Marks the end of a program. Optional as it does not perform any action but is purely for aesthetic purposes.

### execute

`execute: {<graph_name>, <queue_settings>}`

Executes an epoch. Each execute instruction must contain a map with `graph_name` indicating the `graph` to be executed during the current epoch, and an optional `queue_settings` map that specifies the queue settings for the input and output IO used by the graph. Details of `queue_settings` are described below:

#### Queue Settings 

Queue settings is an optional map found in `execute` instruction. It is used to specify the queue settings for the input and output IO used by the graph. The supported fields are the following:

| Queue Settings Field | Description                    | 
| -------------------- | ------------------------------ | 
| prologue             | Graph epoch prologue action. If set to `true`, the consumer op performs a preload of the data into cores' private L1 memory before the epoch starts execution. If set to `false`, the data will be streamed in from IO during epoch exeuction in similar to input activation streaming from another op. This is a _static property_. | 
| epilogue             | Graph epoch epilogue action. If set to `true`, the producer op performs a writeback the data from cores' private L1 memory after the epoch finished execution. If set to `false`, the data will be streamed out to IO during epoch exeuction in similar to output activation streaming to another op. This is a _static property_. | 
| zero                 | Graph epoch prologue action. If set to `true`, contents of the IO node's data will be zero'd before epoch starts execution. This is commonly used to zero accumulated gradients to start the next mini batch of training. | 
| rd_ptr_local         | Consumer OP's local pointer for reading data, changes to this value must NOT be written back to the globally visible IO node's state. It's commonly used for forked outputs to peek at the common input IO at different locations and strides. | 
| rd_ptr_global        | Consumer OP's global pointer for reading data, changes to this value is in IO node's globally visible state to `host` and all other devices in the system. It's commonly used for popping a queue's entries, and any changes implies a pop action (increment to the previous `rd_ptr_global` value). | 
| wr_ptr_global        | Producer OP's global pointer for reading data, changes to this value is in IO node's globally visible state to `host` and all other devices in the system. It's commonly used for pushing a queue's entries, and any changes implies a push action (increment to the previous `wr_ptr_global` value). | 
| global_rdptr_autoinc | Auto increment stride for `rd_ptr_global` by the consumer OP, if set to `0` then device does not increment the value. | 
| rd_ptr_autoinc       | Auto increment stride for `rd_ptr_local` by the consumer OP, if set to `0` then a device default behaviour takes place. | 
| global_wrptr_autoinc | Auto increment stride for `wr_ptr_global` by the producer OP, if set to `0` then a device default behaviour takes place. | 
| read_only            | Only used for `dual-view` IO nodes, which aliases two views with different blocking schemes but the same entry sizes to the same underlying memory. This field explicitly uses the reader view of the IO. More documentation to follow. | 

Unless otherwise noted above, all queue settings are _dynamic properties_ that can be toggled/changed during runtime, implying variables can bind to such properties; whereas _static properties_ must be set with constant values and cannot be changed during runtime.

## Optional Sections

Users of the netlist may chose to implement custom sections for backend specific user behaviours. BUDA backend currently implements the following custom sections for internal testing purposes.

### test-config

Used for testing purposes. Custom stimulus and expected output can be specified in this section.

### performance-check

Used for performance analysis. Custom performance metrics can be specified in this section.

# Interactions & Limitations

## BUDA Frontend

BUDA frontend (PyBUDA) is the frontend compiler used to compile models from popular frameworks such as PyTorch, TensorFlow, ONNX/TVM to generate netlists and backend runtime configurations. PyBUDA also contains a thin python runtime layer that wraps around BUDA backend runtime to execute the generated netlist on the targeted TT devices.

The typical use case for PyBUDA runtime is to structure separate programs for different phases of training, ie. forward, backward, and optimizer programs. The forward program is used to compute the loss, the backward program is used to compute the gradients, and the optimizer program is used to update the model parameters. 

In a temporal approach (typically used in `grayskull` architecture), the forward, backward and optimizer programs are executed sequentially, and can be mapped to the same physical resources as temporal epochs can be time-multiplexed.

In a spatial approach (typically used in `wormhole` architecture), the forward and backward phases are executed in parallel, and the optimizer phase executed sequentially after the forward and backward phases have completed. In this case the forward and backward phases must be combined into a single program to create a monolithic temporal epoch (via combining multiple spatial epochs that are executable in parallel).

While each approach may run more effectively on specific architectures, they are supported on all architectures and can be mixed and matched. For example, an inference workload may only contain a forward phase but the number of physical devices may be limited making it not possible to fully unroll all the graphs to concurrent spatial epochs. In this case, the forward phase can be broken into multiple temporal epochs that each contain many spatial epochs.

## BUDA Backend

BUDA backend (BBE) is the backend runtime that executes the netlists. BBE can be used with multiple frontends such as PyBUDA or TinyTensor as long as their output netlist adhere to the netlist specification specified above.

While BUDA backend implementation of the netlist is outside the scope of this document, it is key to understanding the performance and limitations of device targetted by the netlist.

#### Constraints

The following is a list of architecture specific constraints due to physical limitations of the architectures or the configuration of the systems. These are not explicitly enforced by the netlist specification, but are enforced by the BBE netlist parser and compiler.

- Number of HOST devices
- HOST capacity
- Number of DRAM channels
- Number of DRAM subchannels/access nodes
- DRAM capacity
- Number of Tensix cores (compute grid size used by OPs)
- Number of Ethernet cores and their connections (topology of the cluster)
- Tensix L1 capacity
- Ethernet L1 capacity
- Supported data formats
- Number of forked paths, join paths, and join points between nodes
- TM pipes feature set
- Fused OPs feature set
