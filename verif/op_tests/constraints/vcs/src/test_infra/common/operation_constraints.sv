`ifndef __OPERATION_CONSTRAINTS_SV__
`define __OPERATION_CONSTRAINTS_SV__

virtual class operation_constraints extends node_constraints;
    rand input_constraints in[];
    rand architecture arch;

    rand bit[2:0] target_device;
    rand bit[3:0] grid_loc_x;
    rand bit[3:0] grid_loc_y;

    bit op_consumer_is_queue = 0;
    rand e_vector_mode sfpu_vector_mode; 

    rand e_math_fidelity math_fidelity;

    bit bfp_enabled = 1;

    rand e_data_format dest_data_format;
    rand e_data_format intermed_data_format;
    rand e_data_format output_data_format;

    bit untilize_enabled = 1;
    rand bit untilize;

    rand integer accumulation_count; // this number is specific to operation. it represents the number of accumulations performed on single tile
    rand integer gradient_accumulation_count; // depends on inputs and num loops

    // attributes
    bit srnd_enabled = 1;
    rand bit srnd_en;
    rand e_stoch_rnd_mode stoch_rnd_mode;

    bit relu_enabled = 0;
    bit vector_mode_enabled = 0;

    rand bit relu_en;
    rand integer relu_threshold;
    rand integer relu_threshold_int;
    rand e_relu_mode relu_mode;

    bit quant_enabled = 0;
    rand bit quant_en;
    rand bit requantize;
    rand bit dequantize;
    rand integer zero_point;

    bit accumulation_enabled = 0;
    rand bit accumulate_en;
    rand integer _z;

    bit bias_enabled = 0;
    rand bit bias_en;

    bit m_k_enabled = 0;
    rand integer m_k;
    rand integer u_kt;

    bit l1_acc_enabled = 0;
    rand bit l1_acc_en;

    bit gradient_op_enabled = 0;
    rand bit gradient_op_en;

    bit kernel_broadcast_enabled = 0;
    rand bit kernel_broadcast_op_en;
    rand bit kernel_broadcast_en[];
    rand int kernel_broadcast_factors[];
    rand int kernel_broadcast_period_height[];
    rand int kernel_broadcast_period_width[];
    rand bit is_kernel_broadcast_per_t[];

    function new(string name);
        super.new(name);
    endfunction

    function void set_inputs(node_constraints inputs[]);
        in = new[inputs.size()];
        kernel_broadcast_factors = new[inputs.size()];
        kernel_broadcast_en = new[inputs.size()];
        kernel_broadcast_period_height = new[inputs.size()];
        kernel_broadcast_period_width = new[inputs.size()];
        is_kernel_broadcast_per_t = new[inputs.size()];
        foreach (inputs[i])
            in[i] = new(inputs[i], this);
        initialize_inputs();
    endfunction

    protected pure virtual function void initialize_inputs();

    pure virtual function int get_max_inputs();

    constraint rand_kernel_broadcast {
        kernel_broadcast_op_en dist {0:=80, 1:=20};
        foreach (in[i]) {
            kernel_broadcast_enabled == 0 -> kernel_broadcast_en[i] == 0;
            is_kernel_broadcast_per_t[i] dist {0:=50, 1:=50};
            is_kernel_broadcast_per_t[i] == 0 -> in[i].producer.tensor.t == 1; 
            in[i].broadcast_en == 0 -> kernel_broadcast_en[i] == 0;
            kernel_broadcast_en[i] == 1 -> in[i].producer.tensor.grid_size_x == 1 && in[i].producer.tensor.grid_size_y == 1;
            kernel_broadcast_en[i] == 1 -> { // assume that ublock_order == r
                kernel_broadcast_period_height[i] % in[i].post_tm_tensor.ublock_rt == 0;
                kernel_broadcast_period_height[i] <= in[i].post_tm_tensor.ublock_rt * in[i].post_tm_tensor.mblock_m;
                kernel_broadcast_period_height[i] % (in[i].producer.tensor.mblock_m * in[i].producer.tensor.ublock_rt) == 0;
                kernel_broadcast_period_width[i] == in[i].post_tm_tensor.mblock_n * in[i].post_tm_tensor.ublock_ct;
                kernel_broadcast_factors[i] == kernel_broadcast_period_height[i] * kernel_broadcast_period_width[i];
                kernel_broadcast_factors[i] < in[i].post_tm_tensor.mblock_m * in[i].post_tm_tensor.mblock_n * in[i].post_tm_tensor.ublock_rt * in[i].post_tm_tensor.ublock_ct;
                kernel_broadcast_factors[i] > 0;
            }
        }
    }

    constraint rand_same_exponent_out_intermed {
        if (arch.same_exponent_out_intermed == 0) {
            if (gradient_op_en || m_k > 1) {
                output_data_format inside {bfp2, bfp4, bfp8, fp16} && intermed_data_format inside {bfp2, bfp4, bfp8, fp16} ||
                output_data_format inside {bfp2_b, bfp4_b, bfp8_b, fp16_b} && intermed_data_format inside {bfp2_b, bfp4_b, bfp8_b, fp16_b} ||
                output_data_format == fp32 && intermed_data_format inside {bfp2_b, bfp4_b, bfp8_b, fp16_b};
            }
        }
    }

    constraint rand_gradient_op_en {
        dest_data_format == int32 -> { gradient_op_en == 0;}
    }

    constraint rand_t {
        gradient_op_en == 1 -> tensor.t == 1;
    }

    constraint rand_target_device {
        target_device inside {[0:`NUM_TARGET_DEVICES-1]};
    }

    constraint rand_max_dest_tiles {
        `get_tiles_per_ublock(tensor) <= `get_max_tiles_in_dest(dest_data_format) / 2;
    }

    constraint rand_grid_loc {
        grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
        grid_loc_x + tensor.grid_size_x <= `GRID_SIZE_X;

        grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
        grid_loc_y + tensor.grid_size_y <= `GRID_SIZE_Y;
    }

    constraint rand_operation_attributes {
        tensor.grid_size_x != 1 || tensor.grid_size_y != 1 -> untilize == 0;
        output_data_format == tensor.data_format;

        // TODO: This is currently a hack
        gradient_accumulation_count == in[0].post_tm_tensor.t;
    }

    constraint rand_disable_bfp_formats {
        (bfp_enabled == 0 && dest_data_format != int32) -> {
            foreach(in[i]) {
                in[i].producer.tensor.data_format inside {fp32, fp16_b, fp16};
            }
            intermed_data_format inside {fp32, fp16_b, fp16};
            output_data_format inside {fp32, fp16_b, fp16};
        }
    }

    constraint rand_disable_bfp2_bfp4_input_data_format {
        foreach(in[i]) {
            (in[i].producer.tensor.data_format != bfp2) &&
            (in[i].producer.tensor.data_format != bfp2_b) &&
            (in[i].producer.tensor.data_format != bfp4) &&
            (in[i].producer.tensor.data_format != bfp4_b);
        }
    }

    constraint rand_dest_data_format {
        dest_data_format inside {int32, fp32, fp16_b, fp16};
    }

    constraint rand_intermed_data_format {
        dest_data_format == int32 -> intermed_data_format == int32;
        dest_data_format != int32 -> intermed_data_format inside {`FLOAT_OUTPUT_FORMATS};

        l1_acc_en == 1 -> intermed_data_format inside {fp16_b, fp32, int32};
        dest_data_format == fp32 && l1_acc_en == 1 -> intermed_data_format == fp32;

        output_data_format inside {bfp2_b, bfp4_b, bfp8_b, fp32, fp16_b} -> intermed_data_format inside {bfp8_b, fp32, fp16_b};
        output_data_format inside {bfp2, bfp4, bfp8, fp16} -> intermed_data_format inside {bfp8, fp16};

        gradient_op_en -> intermed_data_format == output_data_format;
    }

    constraint rand_output_data_format {
        if (dest_data_format == int32) {
            dequantize == 1 -> output_data_format inside {`FLOAT_OUTPUT_FORMATS};
            dequantize == 0 -> output_data_format inside {int32, int8};
            requantize == 1 -> output_data_format == int8;
            relu_en    == 1 -> output_data_format == int8 || output_data_format == fp32;
        } else {
            output_data_format inside {`FLOAT_OUTPUT_FORMATS};
        }

        // TODO: Check if this works for int32
        untilize == 1 -> output_data_format inside {fp32, fp16_b, fp16};
    }

    constraint rand_tiny_tiles {
        dest_data_format == int32 -> tensor.enable_tiny_tile == 0;
    }

    constraint rand_dest_int32_math_fidelity {
        dest_data_format == int32 -> math_fidelity == HiFi4;
    }

    constraint rand_attributes_disabled {
        m_k_enabled          == 0 -> m_k            == 1;
        srnd_enabled         == 0 -> srnd_en        == 0;
        relu_enabled         == 0 -> relu_en        == 0;
        quant_enabled        == 0 -> quant_en       == 0;
        accumulation_enabled == 0 -> accumulate_en  == 0;
        bias_enabled         == 0 -> bias_en        == 0;
        l1_acc_enabled       == 0 -> l1_acc_en      == 0;
        gradient_op_enabled  == 0 -> gradient_op_en == 0;
    }

    constraint rand_relu_threshold {
        relu_threshold inside {[0:100]};
        relu_threshold_int inside {[0:12700]};
        relu_en dist { 0:=80, 1:=20 };
        relu_en == 0 -> { relu_threshold == 0; relu_threshold_int == 0; }
    }

    constraint quant_constraints {
        requantize == 1 -> dequantize == 0;
        dequantize == 1 -> requantize == 0;
        zero_point inside {[-127:127]};
        quant_en == 0 -> requantize == 0 && dequantize == 0 && zero_point == 0;
        quant_en == 1 -> requantize == 1 || dequantize == 1;
        dest_data_format != int32 -> quant_en == 0;
    }

    constraint accumulate_constraints {
        gradient_op_en == 1 -> accumulate_en == 0;

        _z inside {[1:32]};
        if (accumulate_en == 0) {
            _z == 1;
        } else {
            _z > 1;
        }
    }

    constraint rand_arch_srnd {
       arch.srnd_enable == 0 -> srnd_en == 0;
    }

    constraint rand_srnd_enable {
        // Stochastic rounding and Float32 dest accumulator are not supported together
        // Issue : #1816
        dest_data_format == fp32 -> srnd_en == 0;
    }

    constraint rand_srnd_mode {
        srnd_en == 0 -> stoch_rnd_mode == None;
        (srnd_en == 1 && dest_data_format == int32) -> stoch_rnd_mode inside {Fpu, None};
    }

    constraint rand_accumulation_count {
        if (gradient_op_en == 0) {
            accumulation_count == m_k * _z + bias_en + quant_en;
        } else {
            accumulation_count == (m_k + bias_en + quant_en) * gradient_accumulation_count;
        }

        (srnd_en == 0 || stoch_rnd_mode == None) && output_data_format == fp16_b -> accumulation_count < 128;
        (srnd_en == 0 || stoch_rnd_mode == None) && output_data_format == fp16 -> accumulation_count < 512;

        untilize == 1 -> accumulation_count == 1;
    }

    constraint rand_math_fidelity {
        dest_data_format == int32 -> math_fidelity == HiFi4;
        math_fidelity == arch.math_fidelity;
    }

    constraint rand_untilize {
        untilize_enabled == 0 -> untilize == 0;
        untilize dist {0:=80, 1:=20};
        gradient_op_en == 1 -> untilize == 0;
        op_consumer_is_queue == 0 -> untilize == 0;
    }

    constraint rand_current_limitations {
        dest_data_format == int32 -> {
            // TODO: Bug #2376
            relu_en == 1 -> relu_mode == Max;
        }

        // TODO: Bug #2573
        output_data_format == fp32 -> relu_en == 0;

        // TODO: Bug #2274
        stoch_rnd_mode inside {None, Fpu};

        // TODO: Create issue and investigate
        tensor.enable_tiny_tile == 1 -> gradient_op_en == 0;
    }

    constraint rand_force_emulator_grid_size {
        arch.force_emulator_grid_size == 1 -> {
            foreach (in[i]) {
                in[i].post_tm_tensor.grid_size_x dist {1:=50, 2:=50};
            }
        }
    }

    virtual function bit feeder_enabled(integer input_id);
        return 1;
    endfunction

    virtual function bit has_attributes();
        bit has_kernel_broadcast = 0;
        foreach (in[i]) begin
            has_kernel_broadcast = has_kernel_broadcast | kernel_broadcast_en[i];
        end
        return relu_en || quant_en || accumulate_en || bias_en || m_k_enabled || l1_acc_en || srnd_en || has_kernel_broadcast;
    endfunction


    virtual function write_attributes_to_file(int out_filehandle);
        if (relu_en) begin
            $fwrite(out_filehandle, "relu_en: %0s, ", relu_en ? "true" : "false");
            $fwrite(out_filehandle, "relu_mode: %0s, ", get_relu_mode(relu_mode));
            if (dest_data_format == int32) begin
                $fwrite(out_filehandle, "relu_threshold: %1d, ", relu_threshold_int/100);
            end else begin
                $fwrite(out_filehandle, "relu_threshold: %f, ", real'(relu_threshold)/100);
            end      
        end

        if (quant_en) begin
            $fwrite(out_filehandle, "requant: %0s, ", requantize ? "true" : "false");
            $fwrite(out_filehandle, "dequant: %0s, ", dequantize ? "true" : "false");
            $fwrite(out_filehandle, "zero_point: %0d, ", zero_point);
        end

        if (accumulate_en) begin
            $fwrite(out_filehandle, "accumulate: %0s, ", accumulate_en ? "true" : "false");
            $fwrite(out_filehandle, "z: %0d, ", _z);
        end

        if (srnd_en) begin
            $fwrite(out_filehandle, "stoch_rnd_mode: %0s, ", get_stoch_rnd_mode(stoch_rnd_mode));
        end

        if (bias_en) begin
            $fwrite(out_filehandle, "bias: %0s, ", bias_en ? "true" : "false");
        end

        if (m_k_enabled) begin
            $fwrite(out_filehandle, "m_k: %0d, ", m_k);
            $fwrite(out_filehandle, "u_kt: %0d, ", u_kt);
        end

        if (l1_acc_en) begin
            $fwrite(out_filehandle, "l1_acc: %0s, ", l1_acc_en ? "true" : "false");
        end

        if (kernel_broadcast_enabled) begin 
            for (int i = 0; i < get_num_inputs(); i = i + 1) begin
                if (kernel_broadcast_en[i] == 1) begin
                    $fwrite(out_filehandle, "kernel_broadcast");
                    if (is_kernel_broadcast_per_t[i]) begin
                        $fwrite(out_filehandle, "_per_t");
                    end
                    $fwrite(out_filehandle, " : {input_%0d : %0d}, ", i, kernel_broadcast_factors[i]);
                end
            end      
        end
    endfunction

    virtual function integer get_num_inputs();
        return in.size();
    endfunction

    virtual function input_constraints get_op_input(int index);
        return in[index];
    endfunction

    virtual function string get_template_identifier_name();
        return "op";
    endfunction

    virtual function void write_to_file(int out_filehandle);
        $fwrite(out_filehandle, "grid_loc: [%0d, %0d], ", grid_loc_y, grid_loc_x);
        $fwrite(out_filehandle, "inputs: ");
        `WRITE_ARRAY(out_filehandle, get_num_inputs(), get_op_input(i).producer.name);
        $fwrite(out_filehandle, "in_df: ");
        `WRITE_ARRAY(out_filehandle, get_num_inputs(), get_data_format(get_op_input(i).producer.tensor.data_format));
        $fwrite(out_filehandle, "math_fidelity: %0s, ", get_math_fidelity(math_fidelity));
        $fwrite(out_filehandle, "intermed_df: %0s, ", get_data_format(intermed_data_format));
        $fwrite(out_filehandle, "acc_df: %0s, ", get_data_format(dest_data_format));
        if (has_attributes()) begin
            $fwrite(out_filehandle, "attributes: {");
            write_attributes_to_file(out_filehandle);
            $fwrite(out_filehandle, "}, ");
        end
        $fwrite(out_filehandle, "ublock_order: r, ");
        $fwrite(out_filehandle, "buf_size_mb: 2, ");
        $fwrite(out_filehandle, "untilize_output: %0s, ", untilize ? "true" : "false");
        $fwrite(out_filehandle, "gradient_op: %0s, ", gradient_op_en ? "true" : "false");
        for(int i =0; i<get_num_inputs(); i++) begin
            $fwrite(out_filehandle, "input_%0d_tms: [", i);
            if (get_op_input(i).broadcast_en == 1) begin
                if (get_op_input(i).is_broadcast_r == 1) begin
                    $fwrite(out_filehandle, "broadcast: {r: %0d}, ", get_op_input(i).broadcast_factor_r);
                end 
                if (get_op_input(i).is_broadcast_c == 1) begin
                    $fwrite(out_filehandle, "broadcast: {c: %0d}, ", get_op_input(i).broadcast_factor_c);
                end 
                if (get_op_input(i).is_broadcast_z == 1) begin
                    $fwrite(out_filehandle, "broadcast: {z: %0d}, ", get_op_input(i).broadcast_factor_z);
                end 
            end
            if (get_op_input(i).transpose_en == 1) begin
                $fwrite(out_filehandle, "transpose");
            end
            $fwrite(out_filehandle, "], ");
        end
    endfunction

    virtual function string get_data_format_name();
        return "out_df";
    endfunction

    function e_data_format get_input_data_format(int index);
        return in[index].producer.tensor.data_format;
    endfunction

    virtual function string get_stimulus_config(int input_id);
        if (get_input_data_format(input_id) inside {int8, int32}) begin
            return "{type: Uniform, uniform_lower_bound: -10.0, uniform_upper_bound: 10.0}";
        end else begin
            return "{type: Uniform, uniform_lower_bound: -2.0, uniform_upper_bound: 2.0}";
        end
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg;
        cfg.Type = "AllCloseHw";
        cfg.atol = "0.01";
        cfg.rtol = "0.15";
        cfg.check_pct = "0.9";
        cfg.check_pcc = "0.99";
        cfg.verbosity = "Concise";
        return cfg;
    endfunction
endclass
`endif // __OPERATION_CONSTRAINTS_SV__