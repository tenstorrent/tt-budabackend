# Tapout demo

```
ARCH_NAME=grayskull dbd/tapout_sweep.py --test_command "./build/test/verif/op_tests/test_op --netlist verif/graph_tests/netlists/netlist_bert_mha.yaml --silicon --arch grayskull" --out_dir "output"
```
This command will create a modified netlist for each operation with one additional tapout queue. Then, it will execute a given test command for each modified netlist.
In the output folder, you can find the modified netlists in file DBG_{operation_name}.yaml, console output of test operation in DBG_{operation}.console.log, op_errors.log where all mismatched tiles are logged, and tapout_result.log.\
ex. tapout_result.log
```
op_name: DBG_mha_0_as_mask
	Command: ./build/test/verif/op_tests/test_op --netlist output/netlist_DBG_mha_0_as_mask.yaml --silicon --arch grayskull
	Log: output/DBG_mha_0_as_mask.console.log
	Result: PASSED
```
## Broken test command
This is an example of a bad command:
```
dbd/tapout_sweep.py --test_command "./build/test/verif/op_tests/test_op --netlist verif/graph_tests/netlists/netlist_bert_mha.yaml --dummy --silicon --arch grayskull" --out_dir "output"
```

tapout_result.log will look like following
```
op_name: DBG_mha_0_output
	Command: ./build/test/verif/op_tests/test_op --netlist output/netlist_DBG_mha_0_output.yaml --dummy --silicon --arch grayskull
	Log: output/DBG_mha_0_output.console.log
	Result: TAPOUT OUTPUT NOT PROCESSED
```

## Tapout with only one run
If possible, you can tapout all operations in one run. Here is an example.
```
dbd/tapout_sweep.py --test_command "./build/test/verif/op_tests/test_op --netlist verif/graph_tests/netlists/netlist_bert_mha.yaml --silicon --arch grayskull" --out_dir "output" --one_run
```

## Debuda abs/streams/tiles

```
build/test/verif/op_tests/test_op --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --seed 0 --silicon --timeout 500  --debug
```

## Netlist graph used for simulation

[Download netlist file](../../../../verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml)
```
┌────────┐  0   ┌───────┐  0   ┌─────┐  0   ┌───────┐  0   ┌────────┐
│ input0 │ ───▶ │ f_op0 │ ───▶ │ op2 │ ───▶ │ d_op3 │ ───▶ │ output │
└────────┘      └───────┘      └─────┘      └───────┘      └────────┘
                                 ▲
                                 │
                                 │
┌────────┐  0   ┌───────┐  1     │
│ input1 │ ───▶ │ f_op1 │ ───────┘
└────────┘      └───────┘
```
### Used cores

Command ```d``` shows current core usage. We can see that selected netlist uses 4 cores.

```
==== Device 0
09  .   .   .   .   .   .   .   .   .   .   .   .   Coordinate system: rc
08  .   .   .   .   .   .   .   .   .   .   .   .   Legend:
07  .   .   .   .   .   .   .   .   .   .   .   .   . - Functional worker
06  .   .   .   .   .   .   .   .   .   .   .   .   + - Functional worker with configured stream(s)
05  .   .   .   .   .   .   .   .   .   .   .   .
04  .   .   .   .   .   .   .   .   .   .   .   .
03  .   .   .   .   .   .   .   .   .   .   .   .
02  .   .   .   .   .   .   .   .   .   .   .   .
01  .   .   .   .   .   .   .   .   .   .   .   .
00  +   +   +   +   .   .   .   .   .   .   .   .
    00  01  02  03  04  05  06  07  08  09  10  11
```
### Initial status

We can use command ```abs``` to analyze problems.
Command ```abs 1``` shows more detailed information about status of active streams.

```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > abs 1
Analyzing device 1/1
X-Y    Op                        Stream  Type      Epoch    Phase    MSGS_REMAINING    MSGS_RECEIVED  Depends on    State    Flag
-----  ----------------------  --------  ------  -------  -------  ----------------  ---------------  ------------  -------  ------
1-1    test_op/f_op0:datacopy         8  input         0        1                 1                1                Active   All core inputs ready, but no output generated
                                     24  output        0        1                 1                0                Active
2-1    test_op/f_op1:datacopy         8  input         0        1                 1                1                Active   All core inputs ready, but no output generated
                                     24  output        0        1                 1                0                Active
3-1    test_op/op2:matmul             8  input         0        1                 1                0  2-1 1-1       Active
                                      9  input         0        1                 1                0                Active
                                     24  output        0        1                 1                0                Active
4-1    test_op/d_op3:datacopy         8  input         0        1                 1                0  2-1 3-1 1-1   Active
                                     24  output        0        1                 1                0                Active
```

Looking at the output, we can identify problem on cores 1-1 and 2-1.
Also, cores 3-1 and 4-1 both depend on 1-1 and 2-1.

Input0 and Input1 are all 1. Currently only streams 8 on core 1-1 and 2, has data loaded into L1.
Command ```s 1 1 8``` will provide detailed information about stream 8 on core 1-1

```
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:8 > 0
Non-idle streams              Registers                                                     Stream (blob.yaml)                        Buffer 10000080000                          Pipe 10000160000
------------------  --------  ------------------------------------------------  ----------  ---------------------------  -----------  -----------------------------  -----------  ------------------  -------------
8                             STREAM_ID                                         8           buf_addr                     196608       md_op_name                     f_op0        id                  10000160000
24                  RR-3-1-8  PHASE_AUTO_CFG_PTR (word addr)                    0x18410     buf_id                       10000080000  id                             0            input_list          [10000050000]
                              CURR_PHASE                                        1           buf_size                     4160         uniqid                         10000080000  output_list         [10000080000]
                              CURR_PHASE_NUM_MSGS_REMAINING                     1           buf_space_available_ack_thr  1            epoch_tiles                    1            outgoing_noc_id     0
                              NUM_MSGS_RECEIVED                                 1           data_auto_send               True         chip_id                        [0]          mcast_core_rc       [0, 0, 0]
                              NEXT_MSG_ADDR                                     0x3000      dest                         []           core_coordinates               (0, 0)
                              NEXT_MSG_SIZE                                     0x82        dram_buf_noc_addr            5100273664   size_tiles                     2
                              OUTGOING_DATA_NOC                                 0           dram_input                   True         scatter_gather_num_tiles       1
                              LOCAL_SOURCES_CONNECTED                           0           dram_io                      True         is_scatter                     0
                              SOURCE_ENDPOINT                                   1           fork_stream_ids              []           replicate                      0
                              REMOTE_SOURCE                                     0           incoming_data_noc            0            tile_size                      2080
                              RECEIVER_ENDPOINT                                 1           input_index                  0            dram_io_flag_is_remote         0
                              LOCAL_RECEIVER                                    0           intermediate                 False        dram_buf_flag                  0
                              REMOTE_RECEIVER                                   0           msg_info_buf_addr            0            dram_buf_streaming             0
                              NEXT_PHASE_SRC_CHANGE                             1           msg_size                     2080         write_dram_buf_flag            0
                              NEXT_PHASE_DST_CHANGE                             1           next_phase_dest_change       True         dram_prefetch_incoming_noc_id  0
                              BUF_START (word addr)                             0x3000      next_phase_src_change        True         outgoing_noc_id                0
                              BUF_SIZE (words)                                  0x104       num_fork_streams             0            dram_chan                      0
                              BUF_RD_PTR (word addr)                            0x0         num_iters_in_epoch           1            dram_sub_chan                  0
                              BUF_WR_PTR (word addr)                            0x82        num_msgs                     1            dram_addr                      0
                              MSG_INFO_PTR (word addr)                          0x2001      num_unroll_iter              1            q_slots                        0
                              MSG_INFO_WR_PTR (word addr)                       0x2001      phase_auto_advance           True         dram_io_flag                   0
                              STREAM_BUF_SPACE_AVAILABLE_REG_INDEX (word addr)  0x82        phase_auto_config            False        dram_io_allow_overwrite        0
                              DATA_BUF_NO_FLOW_CTRL                             0           producer_epoch_id            0            dram_io_skip_flow_ctrl         0
                              UNICAST_VC_REG                                    0           ptrs_not_zero                True         dram_ram_flag                  0
                              REG_UPDATE_VC_REG                                 1           receiver_endpoint            True         use_shadow_rd_ptr              0
                              SCRATCH_REG0                                      0x00000000  reg_update_vc                1            ublock_rt                      0
                              SCRATCH_REG1                                      0x00000000  source_endpoint              True         ublock_ct                      0
                              SCRATCH_REG2                                      0x00000000  unroll_iter                  0            mblock_m                       0
                              SCRATCH_REG3                                      0x00000000  phase_id                     1            mblock_n                       0
                              SCRATCH_REG4                                      0x00000000                                            mblock_k                       0
                              SCRATCH_REG5                                      0x00000000                                            untilized_output               0
                              DEBUG_STATUS[0]                                   0x00100001                                            untilized_output_full_r_dim    0
                              DEBUG_STATUS[1]                                   0x00000000                                            untilized_output_full_c_dim    0
                              DEBUG_STATUS[2]                                   0x0000003d                                            untilized_output_r_dim         0
                              DEBUG_STATUS[3]                                   0x00000000                                            untilized_output_c_dim         0
                              DEBUG_STATUS[4]                                   0x00000000                                            untilized_output_z_dim         0
                              DEBUG_STATUS[5]                                   0x00000001                                            untilized_output_type_0_zdim   0
                              DEBUG_STATUS[6]                                   0x00823000                                            untilized_output_type_1_zdim   0
                              DEBUG_STATUS[7]                                   0x00330255
                              DEBUG_STATUS[8]                                   0x00000000
                              DEBUG_STATUS[9]                                   0x00000008
```

This stream has 1 message in buffer.
We can use command ```t 1 1```, to read first message, 

```
1-1 buffer_addr: 0x00030000 buffer_size: 0x1040 msg_size:2080
1-1 0x00030000 => 0x00000082 0x00000000 0x00000000 0x00000000 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030040 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030080 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000300c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030100 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030140 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030180 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000301c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030200 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030240 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030280 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000302c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030300 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030340 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030380 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000303c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030400 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030440 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030480 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000304c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030500 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030540 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030580 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000305c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030600 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030640 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030680 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000306c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030700 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030740 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030780 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x000307c0 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00
1-1 0x00030800 => 0x3c003c00 0x3c003c00 0x3c003c00 0x3c003c00 0xa0000820 0x0a060020 0x2240a224 0x22a00020
```

or ```t 1 0``` to read first tile. Difference between these commands is that, message is in row format and has some extra bytes at the end of message.
```
-1 buffer_addr: 0x00030000 buffer_size: 0x1040 msg_size:2080
Buffer: id: 10000080000, coord: (0, 0)
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000   1.0000 
  ```

Stream that hasn't received any message yet, like stream 24 on core, we can get something like this.
```
s 1 1 24
t 1 1
Current epoch:0(test_op) device:0 core:1-1 rc:0,0 stream:24 > t 1 1
1-1 buffer_addr: 0x00031040 buffer_size: 0x1040 msg_size:2080
1-1 0x00031040 => 0x0e1e9076 0x641dd2a7 0x4d4037c0 0x0b558339 0xe77cd30c 0x98169ad6 0x98ae0874 0x68dc0016 0x7d3cdc7f 0x900d23f1 0xe0da3ac7 0x1cb6d9b6 0x41451d77 0x96427366 0x45ae6cc2 0x4e2b2539
1-1 0x00031080 => 0x2a06a7e9 0x593190b1 0x168af072 0xbf05ce40 0x1f58bc92 0xbd4bcc99 0x522d1117 0x58536695 0xe9271017 0x2ab92f28 0xf33d3768 0xe824a6a2 0x6a245cf6 0xaec08fab 0xe118f40a 0x28068778
1-1 0x000310c0 => 0xbe197ba4 0xbf842c86 0xc330a50e 0x406398ac 0xc2c4822a 0xc9a076d1 0x264e51a9 0x4db45913 0x057bfe2e 0xd9b2dc29 0x374d4cbf 0x14113ac4 0x81da11d0 0xdc1d05a5 0xf80605bb 0x9a711124
1-1 0x00031100 => 0xe3e9a2d5 0xfb04c329 0x6e0d6ff2 0xd2953444 0x1ac51093 0xdbeb0351 0x908727dc 0x98956d20 0x00a528c1 0xb50cace0 0x0a995f78 0x516efd8f 0x0dde25e5 0x16991775 0x78a6171f 0xcce95739
1-1 0x00031140 => 0x963583fc 0xb57036d4 0x123c6aa7 0xd87cd8f3 0x52775672 0x2a3e2a61 0x9d89166e 0x2116f8f5 0x26acffb6 0x53512323 0x5fff5829 0xea92cac5 0x8cda1b94 0x506822f1 0xb7dc088c 0x5214f025
1-1 0x00031180 => 0x3c53aa0b 0x68c7e0d9 0x2232aec2 0x6fc89ce5 0xcb605e0e 0xa675e629 0x472413ab 0xee258ba0 0xd37ad490 0xff0d3f55 0xfad9df24 0xdce3b5d0 0x7574de53 0x5e0edfe4 0xc072d8ed 0x20f38455
1-1 0x000311c0 => 0x320ab139 0xa96a8516 0xbed10f62 0x5490d545 0x7950309c 0xf5d5deba 0x9b4236a7 0x6a1bc1ec 0x2a44021e 0x10decd71 0x30c656ff 0x7a2bc575 0x1a0f790e 0xb32b9d97 0x85f2412b 0x82f70c1e
1-1 0x00031200 => 0x96a96e17 0x0b3443ca 0x8827320b 0xe7351ab5 0x06d4d29f 0x5f445f10 0x3aa55b88 0x8f9670f6 0x29366183 0xc3d81bcc 0x5f4ba8c1 0x8dc998da 0xe8ea671d 0x46243f0b 0xc7fa6f4c 0xed1226e1
1-1 0x00031240 => 0x1d89c594 0x39fb21fc 0xfb0aa504 0xcfd5b4ac 0xc99fc72e 0x7406c321 0x9f51eac8 0x887665f4 0xe1474515 0x679c7875 0x945b5e7b 0xe5b36c19 0x54de9340 0xea4067ca 0x39f09c44 0x7057e69a
1-1 0x00031280 => 0x253135be 0xdd471b71 0x4097ccb4 0x6b0039a8 0xe67ffb23 0x86cef03e 0x0910340a 0x90c7ab2d 0xc55921d1 0xc70b9949 0x8d6dc1d6 0x9cfe8460 0x5cc2a1c1 0xce82ba7b 0x360f2ca0 0x3cf5ebd0
1-1 0x000312c0 => 0x400d0222 0xf907018e 0xbdd0cf1a 0xd16f250d 0xb81dd16b 0xf24cffa8 0x01dbca42 0xe1a39542 0xb5329600 0x98462511 0x11669812 0x8292616c 0x0327a876 0xd76aab2b 0x232388a6 0xc8edbb4c
1-1 0x00031300 => 0x0668b180 0x6cdf0858 0x2bd5da4e 0xf133ad16 0x7656890e 0xa13ee94c 0x4c56a624 0x65d3e8de 0xed15e88f 0x5cd831f1 0x1c528969 0x3b31764f 0x8b617122 0xe863b57d 0x3c3a65e2 0xab6d49c1
1-1 0x00031340 => 0xcaa5c457 0xfa8d73bb 0x42702e58 0x323911f9 0x824c73a3 0x53162917 0x06e1857b 0xe48e9473 0xf3f05145 0x8f15a5b5 0xdc488aec 0xc26ee071 0x01b6a96c 0x630ea343 0x7caa95fc 0x65209554
1-1 0x00031380 => 0x7bd8a1d1 0xb2bab25c 0x0be11111 0x2025b273 0xdf4fc222 0x83dab770 0xe90ed4fa 0x0895ba98 0x45b6fad2 0x8b478dcd 0xef10f764 0xc1a24838 0xcf92933c 0xe946a14e 0x0fa5b6f3 0x81f5d8e0
1-1 0x000313c0 => 0x7cac1aab 0xd705652f 0x98e065b5 0x4c0c19a6 0x81e4049c 0xae9469cd 0x83c0b52b 0x8b1c22c9 0x88537f11 0xac3c9aaf 0x292276a5 0x5034deb7 0x53864ed8 0x1684bcb9 0x5ec61287 0x6eb893ae
1-1 0x00031400 => 0x058c4718 0xe34dd859 0x820ade41 0xaddaca4e 0x204948e8 0x90de0407 0x8c4204b1 0x97c82115 0x7305be3b 0x371d00a1 0x69a6cdf0 0xfc8f6f81 0xc0e96787 0x8813d466 0xdb29c5ce 0x67c46fb5
1-1 0x00031440 => 0xa887d3cb 0x66eeaf2d 0xacaee62f 0x4a492902 0x1b43728b 0x0af3822e 0xa5f8a415 0xdeff5572 0x02bbd4c4 0x349dfa0a 0xce5ff551 0x0f913ddf 0x27c14383 0xf4ad9643 0x846702a7 0x3dd881eb
1-1 0x00031480 => 0x8d761cee 0x142bcbfe 0xa6bd6d1a 0xd9b42919 0x44f3ce33 0xd632f8c8 0xb520a93d 0x890b49da 0xd78650c7 0xf8652213 0x28cdcba1 0x5c13e794 0xddaa5c5a 0x07d9dc60 0x095bb625 0x8aed4e29
1-1 0x000314c0 => 0x99382092 0x88dfc52e 0xfae38a9c 0x72bc3489 0x0318fa0c 0x7447346a 0x12a63394 0x44e81017 0x8b0a1308 0x19de38f6 0x5d25388c 0xdcb0de56 0xc9c00f95 0xb2283484 0x19a28d8a 0x7e8cb425
1-1 0x00031500 => 0xb05463da 0xce690676 0x3e2ae1da 0x2575456a 0x16746e95 0x4c7a65d5 0x74b7eeeb 0x81c9decf 0x19f0113f 0x75be6265 0x4ed5c59d 0x3d044afb 0x9b83338a 0x78420811 0x19359c12 0x9576e4f5
1-1 0x00031540 => 0xf6fd8fb4 0xcd30a4fa 0x1fd452b9 0xb7ff36f9 0x82dab9c8 0x5e988833 0xe8f69184 0x2f5dbc79 0xb56022e8 0x559cf653 0x1ebc8337 0xb314ea98 0xdbdc61a9 0xe11d2c47 0x4686549b 0xd21f9e53
1-1 0x00031580 => 0x27f06265 0xf2d242f9 0xb2d0389b 0xe3c29946 0x77e4f6d1 0xcf18982d 0xfaeee5b7 0xe4fb4b81 0x13fca5c8 0x5e5bd98b 0x2337a040 0x5996a1e5 0xa08edc2c 0xe341699d 0x1476c5cf 0x0275834a
1-1 0x000315c0 => 0xbbf13529 0x1ef43016 0xaaef0ff2 0x58dbd249 0xb1ae9962 0x3b562474 0x0c831031 0xe7085e2a 0x0d8089dc 0x4d1607d4 0x021072c6 0x03133d40 0xef8fdbcd 0x17e295e1 0x91530536 0x2dc55a47
1-1 0x00031600 => 0xb5e728f0 0x4f2d9ffc 0x20af2b3c 0x0a5f8283 0xad046117 0x2a063fbc 0x29e83445 0x89981764 0xa95ad7f2 0x62d4e698 0x19b65b14 0x5e0ac8de 0x75bf2d6f 0xdd8f81ce 0x76b86c07 0x00880a33
1-1 0x00031640 => 0x2ea16016 0xeabc841d 0x20705fc2 0x58c2c6d7 0x7bbb305a 0x662e184a 0x7bfd9d33 0xbd43b50f 0xfac6d9be 0x1b913864 0x4b2644b4 0xb05d6674 0x9c79c693 0x65510a07 0x748a35a0 0x42df604a
1-1 0x00031680 => 0xcea089a6 0xe25f8a69 0x5e4cfa40 0x202a8a4a 0xd1700b0e 0x941932fe 0x501ec2e9 0xde6549dc 0xe1ea15e5 0x5d7d65ae 0xca021b85 0x30872580 0xeb0197c8 0x82feedc6 0x02606c15 0x366bf873
1-1 0x000316c0 => 0x974bda9a 0xf35fc83a 0x462da3db 0xba06236d 0x50a4c502 0xda29ad3e 0x2371c411 0x27ac5597 0x98375126 0x961d4328 0x5f1988d5 0xdd26db9c 0xce2ffc86 0x586ed5e5 0xc7b7ddca 0x4be1364e
1-1 0x00031700 => 0xefbfa2cd 0x5a595009 0x71723fc9 0x711ba89e 0x2c8c1a17 0xc1196d99 0x4c28659b 0x30a8e035 0x9a83807a 0xa3d21290 0xd9468109 0x89737d15 0x42e68059 0x74cadec3 0x1d298c7b 0x64072b9e
1-1 0x00031740 => 0xdba5e7ce 0xdcfd7ef9 0x5f711407 0xf097c044 0x7a94e46f 0x3f4f0810 0xe1279900 0x21242881 0x02b26851 0xf8530c97 0xb0908f64 0x197b4608 0xaa0fb0a6 0xc6f8c291 0xac7af367 0x69d39191
1-1 0x00031780 => 0x40c490a4 0x75b64908 0x8afcda73 0x26e2ae13 0x8b40fd92 0x02836565 0x368ddbe8 0x08493691 0xfed70454 0x0c61ed58 0xc7822472 0x55ffa007 0x55de1a38 0x14a00c53 0x689cf541 0xae867372
1-1 0x000317c0 => 0xe5246478 0x7769c902 0x88231ed2 0xe46467c9 0x59fe9e70 0x18c304bd 0x3d58c07f 0x9a9233a9 0xafa922dc 0xdde00e20 0x7323765e 0x7b70a8c3 0xfde0fe5f 0x09698bb8 0x0349e697 0xec875c2c
1-1 0x00031800 => 0xa43f2e56 0x4cea3195 0xce916bb4 0x993bfc82 0xaee4e30c 0x88beb2ad 0xa409a798 0x6f383179 0x9bf2de41 0x00526add 0xc520ad1b 0x3b8e7652 0xe5fbf71f 0x85226a64 0x95dc3904 0x39f2e59c
1-1 0x00031840 => 0x9c1e2f11 0x31fb9230 0xc6c741f7 0x9b8629d1 0x759ef2c0 0x58d3b26e 0x7c8c23a1 0x28b9039b
```
After unblocking core 1-1 (```dcu - dcp```) data are reaching L1 buffer used by stream 24 and core 3-1.

You can see difference using command ```abs 1```, where operation on core 1-1 is not active anymore, and input 8 got data on core 3-1. There is no more dependency on 1-1, but still there is dependency on 2-1.
```
Analyzing device 1/1
X-Y    Op                        Stream  Type      Epoch    Phase    MSGS_REMAINING    MSGS_RECEIVED  Depends on    State    Flag
-----  ----------------------  --------  ------  -------  -------  ----------------  ---------------  ------------  -------  ------
2-1    test_op/f_op1:datacopy         8  input         0        1                 1                1                Active   All core inputs ready, but no output generated
                                     24  output        0        1                 1                0                Active
3-1    test_op/op2:matmul             8  input         0        1                 1                1  2-1           Active
                                      9  input         0        1                 1                0                Active
                                     24  output        0        1                 1                0                Active
4-1    test_op/d_op3:datacopy         8  input         0        1                 1                0  2-1 3-1       Active
                                     24  output        0        1                 1                0                Active
```

To unblock cores 2-1 and 3-1
```
s 2 1 8
dcu
dcp
s 3 1 8
dcu
dcp
```
Now data reached core 4-1, and ```abs 1``` command will show following
```
Analyzing device 1/1
X-Y    Op                        Stream  Type      Epoch    Phase    MSGS_REMAINING    MSGS_RECEIVED  Depends on    State    Flag
-----  ----------------------  --------  ------  -------  -------  ----------------  ---------------  ------------  -------  ------
4-1    test_op/d_op3:datacopy         8  input         0        1                 1                1                Active   All core inputs ready, but no output generated
                                     24  output        0        1                 1                0                Active
```
If we read tiles of stream 8, we are getting result of previous matmul operation
```
s 4 1 8
t 1 0

4-1 buffer_addr: 0x00030000 buffer_size: 0x1040 msg_size:2080
Buffer: id: 10000060000, coord: (0, 3)
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
 32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000  32.0000 
```

This same information is also visible on output of core ```3-1```, by selecting output stream.
```
s 3 1 24
t 1 0
```


## Commands used in Demo
```
dbd/tapout_sweep.py --test_command "./build/test/verif/op_tests/test_op --netlist verif/graph_tests/netlists/netlist_bert_mha.yaml --silicon --arch grayskull" --out_dir "output"
dbd/tapout_sweep.py --test_command "./build/test/verif/op_tests/test_op --netlist verif/graph_tests/netlists/netlist_bert_mha.yaml --silicon --arch grayskull" --out_dir "output" --one_run

build/test/verif/op_tests/test_op --netlist verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml --seed 0 --silicon --timeout 500  --debug
```
