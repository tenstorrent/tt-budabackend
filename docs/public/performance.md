\page performance_debug Performance Debug

# Performance Debug

- [**Performance Debug**](#performance-debug)
  - [**Examining the Host Profiler**](#examining-the-host-profiler)
  - [**Examining the Device Performance Trace**](#examining-the-device-performance-trace)
  - [**Common Performance Issues**](#common-performance-issues)
    - [**Case 1**](#case-1)
      - [**Low Pipe BW**](#low-pipe-bw)
      - [**Fork/Join**](#forkjoin)
    - [**Case 2, 3**](#case-2-3)
    - [**Case 4, 5**](#case-4-5)

In this section, we will explore how to use the existing performance infra tools to debug the performance for different models.

Here is a summary of the process for debugging performance. We will elaborate on each step later in this section:

1. Run the test with `light performance trace (--dump-perf-events)` and `backend profiler` enabled See [performance trace configuration](#performance-trace-configuration)
2. On host side, using the [`backend profiler`](#visualizer-gui) check to make sure host is not stalling the device:
   - Host must push input tensors to device faster than device processing them
   - Host must push the epoch commands/Binaries faster than device processing them
3. Check the throughput of each epoch using the [`perf_info_all_epochs.csv`](#post-processor-and-reports) report
   - Select one of the slower epochs to debug. We will refer to this epoch as `Selected Epoch`, and to the graph that it is running `Selected Graph`.
4. After we selected which epoch we want to examine, rerun the test with [`verbose performance trace`](#performance-trace-configuration) and record performance only for the selected graph.
   - Example command for running verbose perf with selected graphs: `--dump-perf-events-intermediate --perf-level 1 --perf-target-graphs <GRAPH_NAME>`
   - NOTE: The reason why we initially run the test in light mode, is to get the throughput with minimal perf-trace overhead.
   - NOTE: The reason why we use `--perf-target-graphs`, is to ensure we will not run out of dram perf buffer space for our target epoch [See Dram spill mode](#performance-trace-architecture). Otherwise, if there are several epochs executed before the `Selected Epoch`, we might run out of dram space for perf events and skip recording anything for this epoch.
   - NOTE: However, since the overhead of the verbose perf trace so far has been less than 3%, we can skip running the test in light mode as well.
   - At this point we will start optimizing device performance for the `Target Graph`.
   - Goal at each step: Find the longest op in the pipeline of ops
5. Examine the visualizer (host/device) to find the bottlenecks
   - What is the longest op in the pipeline and why
   - Might have to hack the netlist to root cause
6. Modify/hack the netlist to improve/solve the previous bottlenecks
7. Go back to step 4

## Examining the Host Profiler

The goal in this step is to ensure host is not stalling the device. There are two cases where host might create bottlenecks:

1. Host not pushing inputs fast enough
2. Host not pushing [epoch commands/Binaries](#device) fast enough

Backend profiler records the start and end of all the instances where host tries to push inputs to device. There are two stages to pushing inputs to device:

1. Converting the data formats, and tilizing them (Taking 32x32 chunks of data and appending headers to each one).
2. Writing the tiles to device dram.

There are two flows in backend to push inputs to device:

1. `HW Tilizer`: This is the faster way to push inputs to device. Uses avx based data format convertor.
   1. If the target dram buffer is within the mmmio-mapped region (top 256 MB of dram channel-0 for GS, and between addresses `0x30000000 - 0x40000000` for WH) we use the faster writes to dram.
   2. If the target dram buffer is not within the mmio-mapped region the write to device will be slower
2. `SW TIlizer`: This is the slower way to push inputs to device. It is only used when the data format conversion is not supported in avx.

The input queue to the graph (which host pushes into) is double-buffered by default. Meaning if `microbatch_size = N`, `num_entries = 2 \* N` for the queue. This allows device to start working on the first `N` inputs, while host is pushing the second `N` inputs.

Here is an example of backend profiler for a `Bert Large Bfp8b` test running on a single `WH-B0` chip.

- In this example only the first three loops are shown.
- Each loops in this contexts referes to running a microbatch size of 128 on all the 24 encoders.

![22](images/22.png)

## Examining the Device Performance Trace

To optimize performance for a specific epoch, we can use the the [reports](#post-processor-and-reports) and [device trace gui](#visualizer-gui).

To find a bottleneck in the pipelines let's take the following example:

![24](images/24.png)

The ops are pipelined over macroblocks. Adding input/output buffers to the figure above:

Each op can only push tiles if there's space in its output buffer. Each op can start math only if there are tiles in its input buffers.

When an op is finished doing math on a block, it pops it (frees up that space) from its own input buffer and the output buffer of the producer op.

![25](images/25.png)

The input and output buffer size:

- `output_buffer_size` = `buf_size_mb` \* `Macroblock_size`
- `input_buffer_size`:
  - Binary/Unary: `2 \* microblock_size`
  - Matmul: Activations: `2 \* one_column_of_macroblock`, Weights: `2 \* one_row_of_macroblock`

![26](images/26.png)

The throughput of the graph will be equal to the longest op in the pipeline.

- All the ops after the longest op in the pipeline will be starved (waiting for tiles)
- All the ops before the longest op in the pipeline will be backpressured (waiting for space in the output buffer)

Both these events get captured for all ops in `--perf-level 1`

So to find the bottleneck we can start from any op and: - Move forward if we saw `wait-for-free-tiles` (backpressure) - Move backwards if we saw `wait-for-tiles`

![27](images/27.png)

![28](images/28.png)

- NOTE: On a `wait-for-free-tiles`, if we see a fork, meaning `OpA` (the op that is back-pressured) is feeding `OpB` and `OpC`, we only stop if all consumers (`B` and `C`) are waiting for tiles. Otherwise, we continue to the consumer op that is back-pressured (`wait-for-free-tiles`). This is because, if one branch of the fork is back-pressuring the producer, the other branch will be stalled.
- NOTE: Similarly on a `wait-for-tiles`, if we see more than one producer, we only stop if all producers are back-pressured. Otherwise, we move to the direction of the op that is also waiting for tiles. This is because if one producer is starving the consumer, all other producers will be back-pressured.

![29](images/29.png)

## Common Performance Issues

- Following the process in the previous section we will eventually observe one of the following:
  1. `OpA` with wait-for-free-tiles feeding `OpB` with wait-for-tiles
     1. Low Pipe BW
     2. Fork/Join
  2. `OpA` with wait-for-free-tiles feeding `OpB` without any waits (no wait-for-tiles or wait-for-free-tiles)
  3. `OpA` without any waits feeding `OpB` with wait-for-tiles
  4. `OpA` with wait-for-free-tiles feeding DRAM (or Ethernet)
  5. `OpA` with wait-for-tiles reading from DRAM (or Ethernet)
  - Note that if we have a fork meaning `OpA` feeds more than one op (`OpB` and `OpC`) and `OpB` has wait-for-tiles and \`OpC:
    - We move to the direction

### Case 1

#### Low Pipe BW

If two ops are feeding each other, one is backpressured and one is waiting, it means the the data movement between the ops is not fast enough. Not enough bw between the two ops.

- Check the [performance report section](#post-processor-and-reports) to see how to read the bw numbers for each op.
- The ideal maximum bw between two ops is `32GB/s`. This value considering the fw overheads is about `28GB/s`.
- The ideal maximum bw for an op reading or writing to a dram channel is `12GB/s` per channel for `grayskull` and `32GB/s` for `wormhole`.
- The maximum bw we can get from a pipe which gathers from multiple input buffers is half the noc bw `16GB/s`. (This value depending on how the pipe is configured can go down to about `10GB/s`)
- To examine the bw more closely we need to introduce the concept of pipes.
- The pipes determine the source, destination and schedule of the data movement. We can see all the pipes for each epoch, under `tt_build/<TEST_OUTPUT_DIR>/temporal_epoch\_#/overlay/pipegen.yaml`
  - Each buffer in this op is tagged with the op_name and operand inded: `e.g. buffer_100440300000: # Op add_17: \[r=2, c=7\], kernel input buf 0`
  - Searching for this buffer_id in the pipe section of the file:

```yaml
pipe_100460300000:  # Op: add_17, input 0
  id: 100460300000
  input_list: [100454400000]
  pipe_periodic_repeat: 0
  pipe_consumer_repeat: 1
  output_list: [100440300000]
  incoming_noc_id: 0
  outgoing_noc_id: 0
  outgoing_vc: 2
  incoming_vc: 2
  mcast_core_rc: [0, 2, 5]
```

- This indicates that the input to this pipe is a single buffer and output is a single buffer as well.
- A more complecated pipe which gathers from multiple buffers:

```yaml
pipe_100468800000:  # Op: matmul_33, input 1
  id: 100468800000
  input_list: [100207800000, 100207800032, 100207800064, 100207800096]
  pipe_periodic_repeat: 0
  pipe_consumer_repeat: 1
  output_list: [100449400000]
  incoming_noc_id: 0
  outgoing_noc_id: 0
  outgoing_vc: 0
  incoming_vc: 0
  mcast_core_rc: [0, 9, 3]
  dram_pipe_total_readers: [1, 1, 1, 1]
  dram_pipe_reader_index: [0, 0, 0, 0]
  dram_pipe_num_forks: 1
  dram_pipe_fork_index: 0
```

- The more important variable for pipes performance is `scatter_gather_num_tiles` on the input buffer of the pipe. This determines the number of tiles that stream sends before it reconfigures. The smaller this value the more overall overhead. Values smaller than `scatter_gather_num_tiles = 8` usually result in suboptimal bw.
- To improve this, we can try to change the block dimensions / grid_size of the consumer and producer op to `scatter_gather_num_tiles`.

#### Fork/Join

Fork/join refers to a situation where an op forks into two branches and those two join later in the graph.

In this situation, if one branch is longer than the other one (in terms of latency), it will stall the fork op. In the example below, `OpC` will only free up the space on the output of `OpA` when it receives the tiles from `OpB` as well. So `OpA` will be stalled until the tiles reach `OpC` on the longer path. This will cause the gaps that can be seen in the figure below.

![31](images/31.png)

![30](images/30.png)

To resolve this, the buffering on the shorter path should be increased:

1. Increase `buf_size_mb` to bump up the output buffer of the fork op or the ops on the shorter path.
2. Increase `input_buf_min_size_tiles` to upsize the input buffer of the join op or the ops on the shorter path.
3. If all the fork/join ops and the ones on the shorter path run out of l1 space, and we still needed more buffering, we can use other tensix cores just for buffering. In this case we isnert a nop (datacopy) on the shorter path.
4. If we still needed more buffering, and the longer path was sufficiently long, we can redirect the shorter path to dram. In this case, we insert intermediate queues where both the input and output of the queue are on the same epoch.
   - NOTE: After adding a queue, it has to be added to `queue_settings` in the program section for the corresponding epoch.

In the example below from bert large, `\_fused_op_3` is forking to `matmul_41` and `add_51`:

```yaml
_fused_op_3: {type: fused_op, grid_loc: [4, 4], grid_size: [2, 1], inputs: [layernorm_38.dc.subtract.1, _fused_op_2, encoder.layer.0.attention.output.LayerNorm.weight, encoder.layer.0.attention.output.LayerNorm.bias], grid_transpose: true,
      t: 1, mblock: [3, 8], ublock: [2, 4], buf_size_mb: 2, ublock_order: c, in_df: [Float16, Float16, Float16, Float16], out_df: Float16, intermed_df: Float16, acc_df: Float16, math_fidelity: HiFi4,
      input_3_tms: [broadcast: {r: 12}], input_2_tms: [broadcast: {r: 12}], input_1_tms: [broadcast: {c: 32}],
      attributes: {approximate_mode: true, fused_op_id: 3}}
matmul_41: {type: matmul, grid_loc: [6, 0], grid_size: [4, 4], inputs: [_fused_op_3, encoder.layer.0.intermediate.dense.weight, encoder.layer.0.intermediate.dense.bias],
      t: 1, mblock: [3, 4], ublock: [1, 8], buf_size_mb: 2, ublock_order: r, in_df: [Float16, Bfp8, Bfp8], out_df: Bfp8, intermed_df: Bfp8, acc_df: Float16, math_fidelity: LoFi,
      input_2_tms: [broadcast: {r: 12}],
      attributes: {bias: true, m_k: 2, min_buffer_input: 1, sfpu_op: gelu, u_kt: 16, sfpu_execution_thread: math}}
matmul_47: {type: matmul, grid_loc: [7, 4], grid_size: [3, 4], inputs: [matmul_41, encoder.layer.0.output.dense.weight, encoder.layer.0.output.dense.bias],
      t: 1, mblock: [4, 1], ublock: [1, 8], buf_size_mb: 2, ublock_order: r, in_df: [Bfp8, Bfp8, Bfp8], out_df: Bfp8, intermed_df: Bfp8, acc_df: Float16, math_fidelity: LoFi,
      input_2_tms: [broadcast: {r: 12}],
      attributes: {bias: true, m_k: 8, min_buffer_input: 1, u_kt: 16}}
add_51: {type: add, grid_loc: [5, 7], grid_size: [1, 1], inputs: [matmul_47, _fused_op_3],
      t: 1, mblock: [6, 8], ublock: [2, 4], buf_size_mb: 2, ublock_order: c, in_df: [Bfp8, Float16], out_df: Bfp8, intermed_df: Float16, acc_df: Float16, math_fidelity: HiFi4}
```

Looking at the performance trace, `\_fused_op_3`, is backpressured, and both `matmul_41` and `add_51` are waiting for tiles.

![32](images/32.png)

Using method 4 for fixing this issue: Adding this queue and using this to feed add_51:

```yaml
e2e__fused_op_3_0: {input: _fused_op_3, type: queue, entries: 256, grid_size: [2, 1], t: 1, mblock: [3, 8], ublock: [2, 4], ublock_order: c, df: Float16, target_device: 0, loc: dram, dram: [[3, 0x7d085c0], [4, 0xde885e0]]}

add_51: {type: add, grid_loc: [5, 7], grid_size: [1, 1], inputs: [matmul_47, e2e__fused_op_3_0],
      t: 1, mblock: [6, 8], ublock: [2, 4], buf_size_mb: 2, ublock_order: c, in_df: [Bfp8, Float16], out_df: Bfp8, intermed_df: Float16, acc_df: Float16, math_fidelity: HiFi4}
```

The performance trace after this change looks like below. The bottleneck is moved to earlier in the pipeline:

![33](images/33.png)

### Case 2, 3

Math Bound

If one of the two ops is not waiting at all either for the wait-for-tile or wait-for-free-tiles, it indicates that the op is math bound.

Two ways to improve this:

1. Increase the grid-size. Splite the workload across larger number of cores, each core operates on smaller blocks.
   - NOTE: When changing the grid size, the overall tensor dims must stay the same:
   - `gird_size_r \* mb_r \* ub_r` must not change
   - `grid_size_c \* mb_c \* ub_c` must not change
2. Check the utilization of the op and improve if lower than expected. This option is only applicable to certain ops that are more sensitive to kernel parameters. Currently only Matmul and MatmulSparse.
   - For Matmuls the sweep on different parameters:
     - GS: [https://tenstorrent.sharepoint.com/:x:/s/Software/Ec6Al9_a1idAr1OP4_4kU7QB6UPXtwHcWBTY7jnad15BcA?e=xawB3S](https://tenstorrent.sharepoint.com/:x:/s/Software/Ec6Al9_a1idAr1OP4_4kU7QB6UPXtwHcWBTY7jnad15BcA?e=xawB3S)
     - WH-B0: [https://tenstorrent.sharepoint.com/:x:/s/Software/EXcpVkfaNrNJpHglUsrE5PsBIjSY_Zm8PW8AHM7l4EITBw?e=nn9HCK](https://tenstorrent.sharepoint.com/:x:/s/Software/EXcpVkfaNrNJpHglUsrE5PsBIjSY_Zm8PW8AHM7l4EITBw?e=nn9HCK)
   - Matmuls are most sensitive to inner block dims (`mk`, `uk`). We need to try to maximize `uk` and minimize `mk`.
     - As described previously in this section, input buffer size for
     - Activations =`mb_column_size: mb_r \* ub_r \* uk`
     - Weights = `mb_row_size: uk \* mb_c \* ub_c`
   - Therefore, increasing `uk` will require a larger buffer space in l1. Hence, the l1 space avaibale determins what the maximum value for `uk` can be.
   - There are two additional options to increase uk without consuming more l1 space. With this feature, instead of buffering an entire column on activation or row on weights, for one of them we only buffer on one ublock:
     - `min_buffer_input: 0` For activations only buffer a single ublock.
     - `min_buffer_input: 1` For weights only buffer a single ublock.
     - These two cannot be used at the same time.
     - Math utilization drops by about 3% using any of these attributes, however it allows us to increase uk which will increase utilization more than before.

In the example below from bert large, matmul_14 is not stalled at all, but `\_fused_op_3` is backpressured.

```yaml
_fused_op_3: {type: fused_op, grid_loc: [4, 4], grid_size: [2, 1], inputs: [layernorm_38.dc.subtract.1, _fused_op_2, encoder.layer.0.attention.output.LayerNorm.weight, encoder.layer.0.attention.output.LayerNorm.bias], grid_transpose: true,
      t: 1, mblock: [3, 8], ublock: [2, 4], buf_size_mb: 2, ublock_order: c, in_df: [Float16, Float16, Float16, Float16], out_df: Float16, intermed_df: Float16, acc_df: Float16, math_fidelity: HiFi4,
      input_3_tms: [broadcast: {r: 12}], input_2_tms: [broadcast: {r: 12}], input_1_tms: [broadcast: {c: 32}],
      attributes: {approximate_mode: true, fused_op_id: 3}}
matmul_41: {type: matmul, grid_loc: [6, 0], grid_size: [2, 4], inputs: [_fused_op_3, encoder.layer.0.intermediate.dense.weight, encoder.layer.0.intermediate.dense.bias],
      t: 1, mblock: [6, 4], ublock: [1, 8], buf_size_mb: 2, ublock_order: r, in_df: [Float16, Bfp8, Bfp8], out_df: Bfp8, intermed_df: Bfp8, acc_df: Float16, math_fidelity: LoFi,
      input_2_tms: [broadcast: {r: 12}],
      attributes: {bias: true, m_k: 32, min_buffer_input: 1, sfpu_op: gelu, u_kt: 1, sfpu_execution_thread: math}}
```

![34](images/34.png)

Increasing both grid size of `matmul_41` and `u_kt`, we increased the utilization from 42% to 57%. Also, since grid size is doubled, workload on each core is cut in half.

```yaml
matmul_41: {type: matmul, grid_loc: [6, 0], grid_size: [4, 4], inputs: [e2e__fused_op_3_0, encoder.layer.0.intermediate.dense.weight, encoder.layer.0.intermediate.dense.bias],
      t: 1, mblock: [3, 4], ublock: [1, 8], buf_size_mb: 2, ublock_order: r, in_df: [Float16, Bfp8, Bfp8], out_df: Bfp8, intermed_df: Bfp8, acc_df: Float16, math_fidelity: LoFi,
      input_2_tms: [broadcast: {r: 12}],
      attributes: {bias: true, m_k: 2, min_buffer_input: 1, sfpu_op: gelu, u_kt: 16, sfpu_execution_thread: math}}
```

![35](images/35.png)

### Case 4, 5

This case is an insufficient dram/ethernet bw.

1. If the dram bw was less than the ideal (12GB/s per channel for gs and 32GB/s for wh):
   1. In the epoch, check for all the readers and writers to the same channels, and calculate the total bw that they are consuming.
   - For example if another core is also writing a tensor with the same size to dram, the total dram bw will be split between these two cores.
   - If the total bw used from channel was still less than ideal, similar to case-1, we need to check the pipe and try to improve the reblocking to increase the size of each rd/wr.
2. If we were utilizing the maximum bw for a channel, Split the rd/wr between as many channels as possible. To do this, we might need to increase the grid-size of the op. Each core on the grid can read/write to a different buffer in dram, and those buffers can be split across mutliple channels