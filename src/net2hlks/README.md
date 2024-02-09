# NET2HLKS

## Fused op constraints


### General constraints


1. Intra tile broadcast (tile_broadcast in the netlist) is supported only on input 1 since hardware allows tile broadcast
   only on source B. Also tile_broadcast is only supported on binary ops (add, sub, mul).
2. Dest cannot be forked.
3. Dest cannot be used as input 1 on Grayskull because of the hardware limitations.
4. Dest cannot be used as an input of matmul and reduce ops, since they use dest to load intermediate results.
5. Dest cannot be used as an input of maximum op.
6. Dest cannot be used outside of schedule (as a consequence matmul and reduce can't use dest as output).
7. New schedule is needed only after matmul/reduce op. This means that each schedule must represent one of the 3 cases:
   (1) it contains 1 matmul/reduce as the last op
   (2) it consumes output of the matmul/reduce from previous schedules
   (3) it is the only schedule in the fused op 
8. Grid size of fused op cannot be arbitrary:
  (1) If there is matmul, op grid size needs to be [m, 1]
  (2) If there is a broadcast, op grid size dimension being broadcasted needs to be 1. Broadcast is supposed to be done on the whole input.
      If we split input rows/columns on multiple cores, we would end up broadcasting partial inputs and connecting them together on the output,
      instead of broadcasting the whole input. (row bcast -> grid_size == [1, n], col bcast -> grid_size == [m, 1])
9. If grid size is not [1, 1], than fused op can support only two matmul instructions (since we need one mcast per matmul op to send input1 to multiple cores).
   Number of multicasts is limited to two, because we have only two NOCs on grayskull/wormhole_b0 device.


### Not supported

1. We don't support fusing splice, embedding op, depthwise and sparse matmul.
2. Small tile feature is not supported (fused ops assume 32x32 tile size).
3. Reduce-max on rc or z dimension is not supported.
4. Reblocking is not supported. (issue [1300](tenstorrent/budabackend#1300))
5. All ops inside the fused op have the same math fidelity, it is not possible to set individual op math fidelity.
6. Intermed buffers can't be used outside of the shedule where they are produced, unless the producing op is matmul or reduce, or 
   mblock dimension is [1, 1].
7. Intermediate buffers cannot be reused for different size of the ublock.
8. All fused operations must have the same ublock order.
9. Reduce row expects input to come in column-major order, so if input comes from an intermediate buffer, the fused op needs to
   have a column-major ublock order. If the input comes from an external input, the order is handled properly.
10. Reduce column expects input to come in row-major order, so if input comes from an intermediate buffer, the fused op needs to have
    a row-major ublock order. If the input comes from an external input, the order is handled properly.
11. Transpose TM is not supported.
12. Binary Sfpu ops (add with Int32 inputs, maximum, quantisation, requantisation and dequantisation) limits:
   * Internal fused op broadcasts are not supported
   * dest is not supported as op output.

#### Matmul
12. Matmul input1 can be forked only if it's a column of ublocks or if it's fork(s) go to another matmul input1.
13. Matmul input 0 can come from the intermediate buffer only if input 1 of the matmul is a column of ublocks.
14. Matmul input 1 can come from the intermediate buffer only if its mblock dimensions are [1, 1] because intermed buffer can fit only
   2 ublocks with double buffering. Otherwise, it needs to come from the fused op input buffer.
15. Matmul input1 can be forked only if it's a column of ublocks or if it's fork(s) go to another matmul input1.
16. Matmul input 0 can come from the intermediate buffer only if input 1 of the matmul is a column of ublocks.
17. Matmul input 1 can come from the intermediate buffer only if its mblock dimensions are [1, 1] because intermed buffer can fit only
   2 ublocks with double buffering. Otherwise, it needs to come from the fused op input buffer.
18. Matmul and reduce op cannot output directly to output if m_k != 1 - they need to output to intermediate buffer in this case.

#### Broadcasts
19. Mblock dimension that is broadcasted needs to be 1 (row bcast -> mb_m == 1; col bcast -> mb_n == 1) because intermed buffer can fit only
   2 ublocks with double buffering.
20. If we do mblock row broadcast, mblock column also needs to be 1 because intermed buffer can fit only 2 ublocks with double buffering.
21. Input ublock dimension that is broadcasted needs to be 1 because HLK implementation doesn't support the correct indexing
   (row bcast -> ub_r == 1; col bcast -> ub_c == 1). ([issue 1064](tenstorrent/budabackend#1064))
22. R and C broadcast are supported only on the inputs coming from the intermediate buffers.
23. RC broadcast is not supported. (issue [#1134](tenstorrent/budabackend#1134))
24. Matmul column bcast is supported on input0 only for cases (m, 1)(r, c) -> (m, n)(r, k) and (m, n)(r, c) -> (m, n)(r, k)
25. If input for matmul is coming from intermed buffer, then these constraints apply (and if it's coming from outside inputs, the constraints don't apply):
   - If input0 is coming from intermed, and it is not broadcasted, then the dimensions must follow this rule: mblock(m, n) ublock(r, c), n == m_k and c == u_kt. 
     If input is broadcasted, this doesn't apply.
   - If input1 is coming from intermed, then the dimensions must follow this rule: mblock(m, n) ublock(r, c), m == m_k and r == u_kt.


### Fusing int ops constraints
1. Only supported ops ATM are nop, add, subtract, multiply, quantization, dequantization, requantization. 
2. In fused op integer and float FPU operations can't be fused together.
3. Dest register is allowed as input only on requantization and dequantization ops.
4. Intermed df has to be Int8 or Int32, accumulation df has to be Int32.
5. All op inputs have to be ints unless they are input to quantization or scalar inputs for dequantization/requantization.
6. Output df has to be Int32 or Int8 unless dequantionzation is last sub op, than output has to be Float32.
7. If dequantization or requantization ops are used micro block size has to be <= 2 (otherwise it's <=4).
8. For quantization op both operands have come from inputs and to be of type Float32.
9. For dequantization or requantization one input0 has to be Float32 form op input and input1 has to be dest register.
