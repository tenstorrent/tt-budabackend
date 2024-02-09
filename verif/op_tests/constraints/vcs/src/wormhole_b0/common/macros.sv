`ifndef __MACROS_SV__
`define __MACROS_SV__

`define get_tiles_per_ublock(tensor) \
    (tensor.ublock_rt * tensor.ublock_ct)

`define get_ublock_size_in_bytes(tensor) \
    (`get_tiles_per_ublock(tensor) * `get_tile_size(tensor.data_format))

`define get_tiles_per_mblock(tensor) \
    (tensor.mblock_m * tensor.mblock_n * `get_tiles_per_ublock(tensor))

`define get_mblock_size_in_bytes(tensor) \
    (`get_tiles_per_mblock(tensor) * `get_tile_size(tensor.data_format))

`define get_tiles_per_core(tensor) \
    (`get_tiles_per_mblock(tensor) * tensor.t)

`define get_tiles_per_core_in_bytes(tensor) \
    (`get_tiles_per_core(tensor) * `get_tile_size(tensor.data_format)

`define get_core_count(tensor) \
    (tensor.grid_size_x * tensor.grid_size_y)

`define get_tiles_in_tensor(tensor) \
    (`get_tiles_per_core(tensor) * `get_core_count(tensor))

`define get_tensor_size_in_bytes(tensor) \
    (`get_tiles_in_tensor(tensor) * `get_tile_size(tensor.data_format))

`define equal_tensors(t1, t2) \
   (t1.t == t2.t && t1.mblock_m == t2.mblock_m && t1.mblock_n == t2.mblock_n && \
    t1.ublock_rt == t2.ublock_rt && t1.ublock_ct == t2.ublock_ct && t1.data_format == t2.data_format && t1.grid_size_x == t2.grid_size_x && \
    t1.grid_size_y == t2.grid_size_y)

`define equal_tensors_except_data_format(t1, t2) \
    (t1.t == t2.t && t1.mblock_m == t2.mblock_m && t1.mblock_n == t2.mblock_n && \
     t1.ublock_rt == t2.ublock_rt && t1.ublock_ct == t2.ublock_ct && t1.grid_size_x == t2.grid_size_x && \
     t1.grid_size_y == t2.grid_size_y)

`define get_max_tiles_in_dest(df) \
    (df == fp32  ? `MAX_TILES_IN_DEST_FP32 :  \
     df == int32 ? `MAX_TILES_IN_DEST_INT32 : \
                   `MAX_TILES_IN_DEST )

`define get_queue_size_in_memory(size_in_bytes_per_core, num_cores) \
    ((size_in_bytes_per_core + `DRAM_QUEUE_HEADER_SIZE) * num_cores)

`define WRITE_ARRAY(out_filehandle, size, element) \
    $fwrite(out_filehandle, "["); \
    for (int i = 0; i < size; i++) begin \
        $fwrite(out_filehandle, "%0s", element); \
        if (i < size - 1) \
            $fwrite(out_filehandle, ", "); \
    end \
    $fwrite(out_filehandle, "], ");

`endif // __MACROS_SV__