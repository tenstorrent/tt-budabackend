`ifndef __QUEUE_CONSTRAINTS_SV__
`define __QUEUE_CONSTRAINTS_SV__

virtual class queue_constraints extends node_constraints;

    string input_name; // HOST or operation name

    rand bit[39:0] q_dram_addr;
    rand bit[39:0] q_host_addr;
    rand bit[39:0] q_tensor_size_per_core;
    rand bit[39:0] q_size;
    rand bit[15:0] num_entries;
    rand bit[15:0] num_loops;
    rand bit[2:0]  target_device;

    rand bit prologue;
    rand bit epilogue;
    rand bit zero;

    function new(string name, string node_type = "queue");
        super.new(name);
        this.node_type = node_type;
    endfunction

    constraint rand_q_tile_size_per_core {
        q_tensor_size_per_core == `get_tiles_per_core(tensor) * `get_tile_size(tensor.data_format);
    }

    constraint rand_q_size {
        q_size == q_tensor_size_per_core * num_entries * num_loops + `DRAM_QUEUE_HEADER_SIZE;
    }

    constraint rand_q_host_addr {
        q_host_addr[5:0] == 6'b000000; // align to 64B
        q_host_addr inside {[`HOST_BUFFER_START_ADDR:`HOST_BUFFER_END_ADDR]};
    }

    constraint rand_q_dram_addr {
        q_dram_addr[5:0] == 6'b000000; // align to 64B
        q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    }

    constraint rand_target_device {
        target_device == 0;
    }

    virtual function void write_to_file(int out_filehandle);
        $fwrite(out_filehandle, "input: %0s, ", input_name);
        $fwrite(out_filehandle, "entries: %0d, ", num_entries);
        $fwrite(out_filehandle, "target_device: %0d, ", target_device);
        write_loc_addresses(out_filehandle);
    endfunction

    virtual function write_queue_settings_to_file(int out_filehandle);
        if (is_node_used) begin
            write_queue_settings_to_file_internal(out_filehandle);
        end else begin
            write_empty_output_to_file(out_filehandle);
        end
    endfunction

    protected pure virtual function write_queue_settings_to_file_internal(int out_filehandle);
    protected pure virtual function write_loc_addresses(int out_filehandle);

endclass

class input_queue_constraints extends queue_constraints;

    function new(string name);
        super.new(name);
        input_name = "HOST";
    endfunction

    constraint rand_prolog_epilog {
        prologue == 0;
        epilogue == 0;
        zero == 0;
    }

    virtual function write_queue_settings_to_file_internal(int out_filehandle);
        $fwrite(out_filehandle, "'%0s: {", name);
        $fwrite(out_filehandle, "prologue: %0s, ", prologue ? "true" : "false");
        $fwrite(out_filehandle, "epilogue: %0s, ", epilogue ? "true" : "false");
        $fwrite(out_filehandle, "zero: %0s, ",     zero     ? "true" : "false");
        $fwrite(out_filehandle, "rd_ptr_local: $lptr, rd_ptr_global: $gptr},'\n");
    endfunction

    function write_stimulus_to_file(int out_filehandle, string stimulus);
        if (is_node_used) begin
            $fwrite(out_filehandle, "'%s: %0s'\n", name, stimulus);
        end else begin
            write_empty_output_to_file(out_filehandle);
        end
    endfunction

    function write_loc_addresses(int out_filehandle);
        $fwrite(out_filehandle, "loc: dram, ");
        $fwrite(out_filehandle, "dram: ");
        `WRITE_ARRAY(out_filehandle, `get_core_count(tensor), $sformatf("[%0d, 0x%0x]", 0, q_dram_addr + i * q_size));
    endfunction
endclass

class output_queue_constraints extends queue_constraints;
    rand operation_constraints input_op;

    function new(string name, operation_constraints input_op);
        super.new(name);
        this.input_op = input_op;
        this.input_name = input_op.name;
        this.is_node_always_used = 1;
        this.input_op.is_node_always_used = 1;
        this.input_op.op_consumer_is_queue = 1;
    endfunction

    constraint rand_data_format {
        `equal_tensors(input_op.tensor, tensor);
    }

    constraint rand_limit_tile_dim {
        input_op.tensor.out_tile_dim_r == tensor.out_tile_dim_r;
        input_op.tensor.out_tile_dim_c == tensor.out_tile_dim_c;
    }

    constraint rand_prolog_epilog {
        if (input_op.gradient_op_en == 1) {
            prologue == 1;
            epilogue == 1;
            zero == 1;
        } else {
            prologue == 0;
            epilogue == 0;
            zero == 0;
        }
    }

    virtual function write_queue_settings_to_file_internal(int out_filehandle);
        if (input_op.gradient_op_en == 1) begin
            $fwrite(out_filehandle, "'%0s: {", name);
            $fwrite(out_filehandle, "prologue: %0s, ", prologue ? "true" : "false");
            $fwrite(out_filehandle, "epilogue: %0s, ", epilogue ? "true" : "false");
            $fwrite(out_filehandle, "zero: %0s, ",     zero     ? "true" : "false");
            $fwrite(out_filehandle, "wr_ptr_global: 0, rd_ptr_global: 0},'\n");
        end else begin
            $fwrite(out_filehandle, "''\n");
        end
    endfunction

    virtual function string get_node_type();
        if (input_op.gradient_op_en == 1) return "ram";
        return super.get_node_type();
    endfunction

    function write_loc_addresses(int out_filehandle);
        if (input_op.gradient_op_en == 0) begin
            $fwrite(out_filehandle, "loc: host, ");
            $fwrite(out_filehandle, "host: ");
            `WRITE_ARRAY(out_filehandle, `get_core_count(tensor), $sformatf("[0, 0x%0x]", q_host_addr + i * q_size));
        end else begin
            $fwrite(out_filehandle, "loc: dram, ");
            $fwrite(out_filehandle, "dram: ");
            `WRITE_ARRAY(out_filehandle, `get_core_count(tensor), $sformatf("[%0d, 0x%0x]", 0, q_dram_addr + i * q_size));
        end
    endfunction
endclass

`endif // __QUEUE_CONSTRAINTS_SV__