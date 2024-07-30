`ifndef __NETLIST_CONSTRAINTS_SV__
`define __NETLIST_CONSTRAINTS_SV__
function string comparison_config_to_str(s_comparison_config cfg, int tile_rows = 32, int tile_cols = 32);
    return $sformatf("{type: %s, atol: %s, rtol: %s, check_pct: %s, check_pcc: %s, verbosity: %s, check_tile_rows_range: [1, %0d], check_tile_cols_range: [1, %0d] }",
        cfg.Type, cfg.atol, cfg.rtol, cfg.check_pct, cfg.check_pcc, cfg.verbosity, tile_rows, tile_cols);
endfunction

class netlist_constraints;
    rand input_queue_constraints in_q[];
    rand output_queue_constraints out_q[];
    rand operation_constraints op[];
    rand queue_constraints q[]; // just pointers to in_q and out_q for easier randomization

    rand bit[15:0] num_entries;
    rand bit[15:0] num_loops;
    rand bit[15:0] num_inputs;
    rand integer target_device;

    // This number is used to determine how many inputs are defined in the netlist
    integer max_inputs;

    function new(integer max_inputs = 4);
        this.max_inputs = max_inputs;
    endfunction

    function initialize(input_queue_constraints in[], output_queue_constraints out[], operation_constraints ops[]);
        in_q = in;
        out_q = out;
        op = ops;

        q = new[in_q.size() + out_q.size()];

        foreach (in_q[i])
        begin
            q[i] = in_q[i];
        end

        foreach (out_q[i])
        begin
            q[in_q.size() + i] = out_q[i];
        end

    if (this.max_inputs < in_q.size())
            $fatal(0, "max_inputs cannot be greater than number of inputs");
    endfunction

     constraint rand_dram_addr {
        foreach (q[i]) {
            if (i < q.size() - 1) {
                q[i].q_dram_addr + q[i].q_size *`get_core_count(q[i].tensor) < (q[i+1].q_dram_addr);
            } else {
                q[i].q_dram_addr + q[i].q_size *`get_core_count(q[i].tensor) < (`DRAM_BUFFER_END_ADDR);
            }
        }
    }

    constraint rand_host_addr {
        foreach (q[i]) {
            if (i < q.size() - 1) {
                q[i].q_host_addr + q[i].q_size *`get_core_count(q[i].tensor) < (q[i+1].q_host_addr);
            } else {
                q[i].q_host_addr + q[i].q_size *`get_core_count(q[i].tensor) < (`HOST_BUFFER_END_ADDR);
            }
        }
    }

    constraint rand_emulator_num_inputs {
        op[0].arch.force_emulator_input_count == 1 -> num_inputs <= 2;
    }

    constraint rand_num_inputs {
        //num_inputs dist {[1:16]:=95, [17:1023]:/4, [1024:1024]:=1};
        op[0].gradient_op_en == 1 -> num_inputs <= 16;

        // must be 1 or multiple of 2 due to pipegen limitations
        //(num_inputs != 1) -> (num_inputs%2==0);
        // Sparse MM on BH hangs with num_input > 1
        num_inputs == 1;
    }

    constraint rand_entries {
        num_entries dist {num_inputs:=20, 2*num_inputs:=80};
    }

    constraint rand_num_loops {
        num_loops inside {[1:2]};
        num_loops * num_inputs == num_entries;
    }

    constraint rand_q_entries {
        foreach (q[i]) {
            if (q[i].prologue == 1 && q[i].epilogue == 1) {
                q[i].num_entries == 1;
            } else {
                q[i].num_entries == num_entries;
            }
        }
    }

    constraint rand_q_num_loops {
        foreach (q[i]) {
            q[i].num_loops == num_loops;
        }
    }

    constraint rand_target_device {
        target_device == 0;
    }

    constraint rand_grid_loc {
        op[0].grid_loc_x == 0;
        op[0].grid_loc_y == 0;

        foreach(op[i]) {
            foreach(op[j]) {
                if (i != j) {
                    op[i].grid_loc_x + op[i].tensor.grid_size_x <= op[j].grid_loc_x ||
                    op[j].grid_loc_x + op[j].tensor.grid_size_x <= op[i].grid_loc_x ||
                    op[i].grid_loc_y + op[i].tensor.grid_size_y <= op[j].grid_loc_y ||
                    op[j].grid_loc_y + op[j].tensor.grid_size_y <= op[i].grid_loc_y;
                }
            }
        }
    }

    function void write_to_file(int out_filehandle);
        $fwrite(out_filehandle, "  loop_count: %0d\n", num_loops);
        $fwrite(out_filehandle, "  input_count: %0d\n", num_inputs);
        $fwrite(out_filehandle, "  target_device: %0d\n", target_device);
        $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2 * num_entries);

        // inputs
        for (int i = 0; i < max_inputs; i++) begin
            if (i < in_q.size()) begin
                $fwrite(out_filehandle, "  in%0d: ", i);
                in_q[i].write_node_to_file(out_filehandle);
                $fwrite(out_filehandle, "  stimulus_in%0d: ", i);
                in_q[i].write_stimulus_to_file(out_filehandle, op[0].get_stimulus_config(i));
                $fwrite(out_filehandle, "  queue_settings_in%0d: ", i);
                in_q[i].write_queue_settings_to_file(out_filehandle);
            end else begin
                $fwrite(out_filehandle, "  in%0d: ''\n", i);
                $fwrite(out_filehandle, "  stimulus_in%0d: ''\n", i);
                $fwrite(out_filehandle, "  queue_settings_in%0d: ''\n", i);
            end
        end

        // Output
        for (int i = 0; i < out_q.size(); i++) begin
            $fwrite(out_filehandle, "  out%0d: ", i);
            out_q[i].write_node_to_file(out_filehandle);
            $fwrite(out_filehandle, "  queue_settings_out%0d: ", i);
            out_q[i].write_queue_settings_to_file(out_filehandle);
        end

        // Operation
        for (int i = 0; i < max_inputs + 2 /* feeders + op + drainer */; i++) begin
            if (i < op.size()) begin
                $fwrite(out_filehandle, "  op%0d: ", i);
                op[i].write_node_to_file(out_filehandle);
            end else begin
                $fwrite(out_filehandle, "  op%0d: ''\n", i);
            end
        end

        // Comparison config
        if (op[0].vector_mode_enabled) begin
            if (op[0].sfpu_vector_mode == vector_mode_r && op[0].tensor.out_tile_dim_r == 32) begin
                $fwrite(out_filehandle, "  comparison_config: '%s'\n", comparison_config_to_str(op[0].get_comparison_config(num_inputs), 4, op[0].tensor.out_tile_dim_c));
            end else if (op[0].sfpu_vector_mode == vector_mode_c && op[0].tensor.out_tile_dim_c == 32) begin
                $fwrite(out_filehandle, "  comparison_config: '%s'\n", comparison_config_to_str(op[0].get_comparison_config(num_inputs), op[0].tensor.out_tile_dim_r, 16));
            end else begin
                $fwrite(out_filehandle, "  comparison_config: '%s'\n", comparison_config_to_str(op[0].get_comparison_config(num_inputs), op[0].tensor.out_tile_dim_r));
            end
        end else begin
            $fwrite(out_filehandle, "  comparison_config: '%s'\n", comparison_config_to_str(op[0].get_comparison_config(num_inputs), op[0].tensor.out_tile_dim_r));
        end
    endfunction

endclass

`endif // __NETLIST_CONSTRAINTS_SV__