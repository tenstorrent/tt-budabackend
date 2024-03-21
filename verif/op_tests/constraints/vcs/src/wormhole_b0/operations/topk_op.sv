`ifndef __TOPK_OP__
`define __TOPK_OP__

class topk_op extends operation_constraints;

    rand integer in0_buffer;
    rand integer in1_buffer;
    rand integer out_buffer;

    rand tensor_constraints input_0;
    rand tensor_constraints input_1;

    rand integer attributes_k;
    rand e_topk_sort attributes_sort;
    rand e_topk_reduce attributes_kreduce;

    function new(string name);
        super.new(name);
        this.node_type = "topk";

        // Enabled features

        m_k_enabled = 1;
        relu_enabled = 0;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        l1_acc_enabled = 0;
        gradient_op_enabled = 0;
        tensor.tiny_tiles_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 0;
        in[0].is_t_accumulation_enabled = 0;
        in[0].transpose_enabled = 0;

        in[1].is_dim_equal = 0;
        in[1].is_t_accumulation_enabled = 0;
        in[1].transpose_enabled = 0;

        input_0 = in[0].post_tm_tensor;
        input_1 = in[1].post_tm_tensor;

        in[0].special_data_format = 1;
        in[1].special_data_format = 1;
    endfunction

    virtual function string get_node_type();
        return "topk";
    endfunction

    virtual function int get_max_inputs();
        return 2;
    endfunction

    constraint rand_input_data_format {
        input_0.data_format inside {bfp8_b, fp16_b};
        input_1.data_format inside {uint16};

        (attributes_sort == k_max) -> (output_data_format == input_0.data_format);
        (attributes_sort == k_argmax) -> (output_data_format == input_1.data_format);
    }

    constraint rand_node_used {
        in[0].producer.is_node_used == is_node_used;
        in[1].producer.is_node_used == is_node_used;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(input_0.data_format);
        in1_buffer == 2 * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(input_1.data_format);

        out_buffer == 2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format);
        
        in0_buffer + in1_buffer + out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_topk_constraints {
        attributes_k inside {4, 8, 16, 32, 64};
        // bug with K=4/8 and N = 32
        (attributes_k == 4) -> (input_0.mblock_n * input_0.ublock_ct >= 2);
        (attributes_k == 8) -> (input_0.mblock_n * input_0.ublock_ct >= 2);
        
        input_0.mblock_m == 1;
        input_0.mblock_n inside {4, 8, 16, 32};
        input_0.ublock_rt == 1;
        (attributes_k == 64) -> (input_0.ublock_ct inside {2, 4});
        (attributes_k < 64) -> (input_0.ublock_ct inside {1, 2, 4});

        input_0.mblock_m == input_1.mblock_m;
        input_0.mblock_n == input_1.mblock_n;
        input_0.ublock_ct == input_1.ublock_ct;
        input_0.ublock_rt == input_1.ublock_rt;
    }

    constraint rand_output_shape {
        if (attributes_k == 64) {
            tensor.mblock_m == input_0.mblock_m;
            tensor.mblock_n == 1;
            tensor.ublock_rt == input_0.ublock_rt;
            tensor.ublock_ct == 2;
        } else {
            tensor.mblock_m == input_0.mblock_m;
            tensor.mblock_n == 1;
            tensor.ublock_rt == input_0.ublock_rt;
            tensor.ublock_ct == 1;
        }

        m_k == input_0.mblock_n;
        u_kt == input_0.ublock_ct;
        tensor.enable_tiny_tile == 0;
    }

    constraint rand_op_formats {
        math_fidelity == HiFi2;
        dest_data_format == fp16_b;
        intermed_data_format == input_0.data_format;
    }

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        if (index == 1) return in[1];
        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function string get_stimulus_config(int input_id);
        if (input_id == 0) begin
            return "{type: Normal, normal_mean: 0.0, normal_stddev: 5.0}";
        end else begin
            return "{type: StepRowwise, step_start: 0, step_increment: 1}";
        end
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg;
        cfg.Type = "AllCloseHw";
        cfg.verbosity = "Concise";

        if (input_0.data_format == bfp8_b) begin
            if (attributes_sort == k_max) begin
                cfg.atol = "0.01";
                cfg.rtol = "0.01";
                cfg.check_pct = "0.70";
                cfg.check_pcc = "0.999";
            end else begin
                cfg.atol = "0.01";
                cfg.rtol = "0.01";
                cfg.check_pct = "0.70";
                cfg.check_pcc = "0.70";
            end
        end else begin
            cfg.atol = "0.01";
            cfg.rtol = "0.01";
            cfg.check_pct = "1.0";
            cfg.check_pcc = "0.99999";
        end

        if (attributes_k == 64) begin
            cfg.check_tile_cols_range = 32;
        end else begin
            cfg.check_tile_cols_range = attributes_k;
        end

        return cfg;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        if (m_k_enabled) begin
            $fwrite(out_filehandle, "m_k: %0d, ", m_k);
            $fwrite(out_filehandle, "u_kt: %0d, ", u_kt);
        end

        $fwrite(out_filehandle, "k: %0d, ", attributes_k);
        $fwrite(out_filehandle, "sort: %0s, ", get_attribute_sort(attributes_sort));
        $fwrite(out_filehandle, "kreduce: %0s, ", get_attribute_kreduce(attributes_kreduce));

    endfunction

endclass
`endif // __TOPK_OP__