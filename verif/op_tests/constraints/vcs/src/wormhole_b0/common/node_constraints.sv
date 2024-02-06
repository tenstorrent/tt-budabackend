`ifndef __NODE_CONSTRAINTS_SV__
`define __NODE_CONSTRAINTS_SV__
virtual class node_constraints;
    string name; // unique name of the operation or queue
    string node_type; // type of node  - operation type matmul/add/... for operations or queue/ram for queues
    rand tensor_constraints tensor;

    // If node is not used, then it is not written to the output file
    rand bit is_node_used;
    bit is_node_always_used = 0;

    function new(string name);
        this.name = name;
        this.tensor = new();
    endfunction

    function void write_node_to_file(int out_filehandle);
        if (is_node_used == 1) begin
            $fwrite(out_filehandle, "'%0s: {", name);
            $fwrite(out_filehandle, "type: %0s, ", get_node_type());
            $fwrite(out_filehandle, "grid_size: [%0d, %0d], ", tensor.grid_size_y, tensor.grid_size_x);
            $fwrite(out_filehandle, "t: %0d, ", tensor.t);
            $fwrite(out_filehandle, "mblock: [%0d, %0d], ", tensor.mblock_m, tensor.mblock_n);
            $fwrite(out_filehandle, "ublock: [%0d, %0d], ", tensor.ublock_rt, tensor.ublock_ct);
            $fwrite(out_filehandle, "%s: %0s, ", get_data_format_name(), get_data_format(tensor.data_format));
            if (tensor.enable_tiny_tile == 1) begin
                $fwrite(out_filehandle, "tile_dim: [%0d, %0d], ",tensor.out_tile_dim_r, tensor.out_tile_dim_c);
            end
            write_to_file(out_filehandle);
            $fwrite(out_filehandle, "}'\n");
        end else begin
            write_empty_output_to_file(out_filehandle);
        end
    endfunction

    protected function void write_empty_output_to_file(int out_filehandle);
        $fwrite(out_filehandle, "''\n");
    endfunction

    protected pure virtual function void write_to_file(int out_filehandle);

    virtual function string get_data_format_name();
        return "df";
    endfunction

    virtual function string get_node_type();
        return node_type;
    endfunction

    constraint rand_node_is_used {
        is_node_always_used == 1 -> is_node_used == 1;
    }

endclass

`endif // __NODE_CONSTRAINTS_SV__