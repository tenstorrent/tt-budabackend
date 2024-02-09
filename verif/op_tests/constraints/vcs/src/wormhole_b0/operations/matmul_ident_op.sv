`ifndef __MATMUL_IDENT_OP__
`define __MATMUL_IDENT_OP__

class matmul_ident_op extends operation_constraints;

    rand tensor_constraints input_0_weights;
    rand tensor_constraints input_1_activations;
    rand tensor_constraints input_2_encodings;
    rand tensor_constraints input_3_bias;

    rand integer in0_weights_buffer;
    rand integer in1_activations_buffer;
    rand integer in2_encodings_buffer;
    rand integer in3_bias_buffer;
    rand integer out_buffer;
    rand integer intermed_buffer;

    rand int num_sparse_tiles;
    rand int num_index_tiles;
    rand int sparse_tile_ptr_bits;
    rand int sparse_ublock_idx_bits;
    rand int nz_tiles_in_ublock;

    function new(string name);
        super.new(name);
        this.node_type = "matmul";

        // Enabled features
        m_k_enabled = 1;
        srnd_enabled = 1;
        relu_enabled = 1;
        quant_enabled = 0;
        accumulation_enabled = 0;
        // BUG: PCC error with bias
        bias_enabled = 0;
        l1_acc_enabled = 1;
        // Not supported
        gradient_op_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 0;

        in[1].is_dim_equal = 0;
        // FIXME: There is test issue with t dimension, when there is accumulation
        in[1].is_t_accumulation_enabled = 0;

        // FIXME: It looks it hangs if transpose is enabled
        in[1].transpose_enabled = 0;

        in[2].is_dim_equal = 0;

        in[2].special_data_format = 1;

        in[3].producer.is_node_always_used = 0;

        input_0_weights     = in[0].post_tm_tensor;
        input_0_weights.custom_block_dims = 1;
        in[0].producer.tensor.custom_block_dims = 1;

        input_1_activations = in[1].post_tm_tensor;

        input_2_encodings   = in[2].post_tm_tensor;
        input_2_encodings.custom_block_dims = 1;
        in[2].producer.tensor.custom_block_dims = 1;

        input_3_bias        = in[3].post_tm_tensor;
    endfunction

    constraint set_num_sparse_tiles {
        num_sparse_tiles == 128; //always pad to this size

        num_sparse_tiles <= 256; // 2^sparse_tile_ptr_bits
    }

    constraint set_num_index_tiles {
        num_index_tiles == 16; //always pad to this size
    }

    constraint set_sparse_tile_ptr_bits {
        sparse_tile_ptr_bits == 8;

        // limit size of ublock - bits for locating tiles in ublock are 16 - sparse_tile_ptr_bits
        u_kt * tensor.ublock_rt <= 256; // 2^(16 - sparse_tile_ptr_bits)
    }

    constraint rand_sparse_ublock_idx_bits {
        sparse_ublock_idx_bits == 8;

        tensor.mblock_m <= 256; // 2^(sparse_ublock_idx_bits)
    }

    constraint rand_nz_tiles_in_ublock {
        nz_tiles_in_ublock == 256; // 2^(16 - sparse_ublock_idx_bits)
        u_kt * tensor.ublock_rt <= nz_tiles_in_ublock;
    }

    // some of the inputs does not have to be used at all
    constraint enable_nodes {
        if (is_node_used) {
            // First two inputs are always used
            in[0].producer.is_node_used == 1;
            in[1].producer.is_node_used == 1;
            in[2].producer.is_node_used == 1;
            in[3].producer.is_node_used == bias_en;
        }
    }

    constraint rand_input_data_format {
        if (dest_data_format == int32) {
            input_0_weights.data_format == int8;
            input_1_activations.data_format == int8;
            input_2_encodings.data_format == RawUInt32;
            bias_en == 0;
        } else {
            input_0_weights.data_format inside {bfp2, bfp2_b};
            input_1_activations.data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
            input_2_encodings.data_format == RawUInt32;
        }
    }

    constraint rand_input_dims {
        input_0_weights.grid_size_x == 1;

        input_0_weights.t == 1;
        input_0_weights.mblock_m == 1;
        input_0_weights.mblock_n == num_sparse_tiles;
        input_0_weights.ublock_rt == 1;
        input_0_weights.ublock_ct == 1;

        input_1_activations.grid_size_y == 1;
        input_1_activations.mblock_m == m_k;
        input_1_activations.mblock_n == tensor.mblock_n;
        input_1_activations.ublock_rt == u_kt;
        input_1_activations.ublock_ct == tensor.ublock_ct;

        input_2_encodings.t == 1;
        input_2_encodings.mblock_m == 1;
        input_2_encodings.mblock_n == num_index_tiles;
        input_2_encodings.ublock_rt == 1;
        input_2_encodings.ublock_ct == 1;
    }

    constraint rand_fit_in_l1 {
        in0_weights_buffer == 2 * input_0_weights.mblock_m * `get_ublock_size_in_bytes(input_0_weights);
        in1_activations_buffer == 2 * input_1_activations.mblock_n * `get_ublock_size_in_bytes(input_1_activations);
        in2_encodings_buffer == 2 * num_index_tiles * `get_tile_size(input_2_encodings.data_format);
        in3_bias_buffer == ((bias_en == 1) ? (2 * `get_ublock_size_in_bytes(input_3_bias)) : 0);
        out_buffer == 2 * `get_mblock_size_in_bytes(tensor);
        intermed_buffer == ((tensor.data_format != intermed_data_format) ? (`get_tiles_per_mblock(tensor) * `get_tile_size(intermed_data_format)) : 0);

        in0_weights_buffer + in1_activations_buffer + in2_encodings_buffer + in3_bias_buffer + out_buffer + intermed_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_matmul_ident_constraints {
        gradient_op_en == 1 -> { bias_en == 0; relu_en == 0; dest_data_format != int32; }

        dest_data_format == int32 -> l1_acc_en == 0;
    }

    constraint rand_tile_dims {
        input_0_weights.enable_tiny_tile == 0;
        input_1_activations.out_tile_dim_c == tensor.out_tile_dim_c;
        input_2_encodings.enable_tiny_tile == 0;
    }

    virtual function bit feeder_enabled(integer input_id);
        // TODO: Datacopy cannot copy encodings
        return input_id != 2;
    endfunction

    virtual function bit has_attributes();
        return 1;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        $fwrite(out_filehandle, "identity: true, num_sparse_tiles: %0d, num_index_tiles: %0d, sparse_tile_ptr_bits: %0d, ",
            num_sparse_tiles, num_index_tiles, sparse_tile_ptr_bits);
    endfunction

    virtual function integer get_num_inputs();
        if (bias_en == 1) return 4;
        return 3;
    endfunction

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        if (index == 1) return in[1];
        if (index == 2) return in[2];
        if (index == 3 && bias_en == 1) return in[3];

        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg = tst_cfg.get_matmul_comparison_config(tensor.data_format, num_inputs, m_k,
            math_fidelity, 0, 0, intermed_data_format, _z, (relu_en) ? get_relu_mode(relu_mode) : "");
        cfg.Type = "AllClose";
        cfg.verbosity = "Concise";
        return cfg;
    endfunction

    virtual function int get_max_inputs();
        return 4;
    endfunction

    virtual function string get_stimulus_config(int input_id);
        if (input_id == 0) return $sformatf("{type: sparse, matmul_ident_name: %0s}", name);
        if (input_id == 1) return super.get_stimulus_config(input_id);
        if (input_id == 2) return $sformatf("{type: sparse_encoding, matmul_ident_name: %0s, zero_strip_freq: 0.5, zero_ublock_freq: 0.5, zero_tile_freq: 0.5, enc_tile_byte_size: 4096}", name);
        if (input_id == 3) return super.get_stimulus_config(input_id);
        $fatal(0, "Invalid input_id %0d", input_id);
    endfunction

endclass

`endif // __MATMUL_IDENT_OP__