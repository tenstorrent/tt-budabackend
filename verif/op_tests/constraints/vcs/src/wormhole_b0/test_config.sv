`ifndef _TEST_CONFIG_H_
`define _TEST_CONFIG_H_

// Test config 
typedef struct {
   string Type;
   string atol;
   string rtol;
   string check_pct;
   string check_pcc;
   string verbosity;
   int check_tile_cols_range;
} s_comparison_config;

typedef struct {
   string Type;
   string uniform_lower_bound;
   string uniform_upper_bound;
} s_stimulus_config;

typedef enum {Concise, AllFails, Verbose} e_comparison_config_verbosity;
typedef enum {Exact, AllClose, AllCloseHw} e_comparison_config_type;

class test_config;
  // Global config
  e_comparison_config_verbosity config_verbosity = Concise;
  e_comparison_config_type config_type = AllCloseHw;

  function string get_comparison_config_verbosity(input e_comparison_config_verbosity verbosity);
     case (verbosity)
        Concise : get_comparison_config_verbosity = "'Concise'";
        AllFails : get_comparison_config_verbosity = "'AllFails'";
        default: get_comparison_config_verbosity = "'Verbose'";
     endcase 
  endfunction

  function string get_comparison_config_type(input e_comparison_config_type Type);
     case (Type)
        Exact : get_comparison_config_type = "'Exact'";
        AllClose : get_comparison_config_type = "'AllClose'";
        default: get_comparison_config_type = "'AllCloseHw'";
     endcase 
  endfunction

  function write_unary_comparison_config(integer out_filehandle, e_data_format out_data_format, e_unary_type unary_type, integer input_tile_dim_r, 
                                          integer output_tile_dim_r, integer input_tile_dim_c=32, e_vector_mode vector_mode=vector_mode_rc);
     begin
       s_comparison_config comparison_config;
       integer comparison_config_tile_dim_r = 32;
       integer comparison_config_tile_dim_c = 32;

       comparison_config.Type = get_comparison_config_type(config_type);
       comparison_config.verbosity = get_comparison_config_verbosity(config_verbosity);

       case (out_data_format)
          bfp8, bfp8_b, fp16_b:  begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.65";
                         if (unary_type == reciprocal) comparison_config.check_pcc = "0.95";
                         else comparison_config.check_pcc = "0.99";
                         end
         bfp4, bfp4_b:  begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.65";
                         comparison_config.check_pcc = "0.95"; 
                         end
         bfp2, bfp2_b:  begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.65";
                         if (unary_type == gelu_derivative) comparison_config.check_pcc = "0.80"; //adjust for gelu derivative
                         else comparison_config.check_pcc = "0.86";
                         end     
          default:       begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.15";
                         if (unary_type == gelu_derivative) comparison_config.check_pct = "0.65"; //adjust for gelu derivative
                         else comparison_config.check_pct = "0.90";
                         comparison_config.check_pcc = "0.99";
                         end
       endcase
       $fwrite(out_filehandle, "  comparison_config_type: %s\n", comparison_config.Type);
       $fwrite(out_filehandle, "  comparison_config_atol: %s\n", comparison_config.atol);
       $fwrite(out_filehandle, "  comparison_config_rtol: %s\n", comparison_config.rtol);
       $fwrite(out_filehandle, "  comparison_config_check_pct: %s\n", comparison_config.check_pct);
       $fwrite(out_filehandle, "  comparison_config_check_pcc: %s\n", comparison_config.check_pcc);
       $fwrite(out_filehandle, "  comparison_config_verbosity: %s\n", comparison_config.verbosity);

       // full tile for sfpu op
       if (input_tile_dim_r == 32 && input_tile_dim_c == 32 && unary_type != datacopy) begin
         // vector mode row considers first 4 rows, while vector mode column considers first 16 rows
         if (vector_mode == vector_mode_r) begin
            comparison_config_tile_dim_r = 4;
         end else if (vector_mode == vector_mode_c) begin
            comparison_config_tile_dim_c = 16;
         end
       end else begin
          // small tile
         if (unary_type != datacopy && unary_type != nop && unary_type != sqrt && unary_type != tanh) begin
            if (input_tile_dim_r < 4 && output_tile_dim_r > input_tile_dim_r) begin
               // Sfpu ops work on at least 4 rows in a tile at once
               // Hence, if input tile_dim_r is < 4, and output tile_dim_r is > input tile_dim_r
               // hardware will output sfpu(0), which can be != 0, in rows between [input_tile_dim_r +1, 4]
               // That is the padding that we don't care about, so it can be ignored when comparing with Golden
               // See BudaBackend issue #2060
               comparison_config_tile_dim_r = input_tile_dim_r;
            end
         end
       end
      $fwrite(out_filehandle, "  comparison_config_tile_dim_r: %0d\n", comparison_config_tile_dim_r);
      $fwrite(out_filehandle, "  comparison_config_tile_dim_c: %0d\n", comparison_config_tile_dim_c);
     end
  endfunction

function write_unary_stimulus_config(integer out_filehandle, e_unary_type unary_type, e_data_format in_data_format, 
                                          e_data_format out_data_format, bit dest_fp32_en=0, bit gradient_op=0);
   begin
      s_stimulus_config stimulus_config;
      stimulus_config.Type = "'Uniform'";

      if (in_data_format == int8) begin
         stimulus_config.uniform_lower_bound = "'-127.0'";;
         stimulus_config.uniform_upper_bound = "'127.0'";
      end else begin
         stimulus_config.uniform_lower_bound = "'-1.0'";
         stimulus_config.uniform_upper_bound = "'1.0'";

         if (unary_type == sqrt) begin
            stimulus_config.uniform_lower_bound = "'0.0'";
            stimulus_config.uniform_upper_bound = "'2.0'";
         end else if (unary_type == reciprocal || unary_type == log) begin
            if (in_data_format == bfp4 || in_data_format == bfp4_b || out_data_format == bfp4 || out_data_format == bfp4_b) begin
               stimulus_config.uniform_lower_bound = "'0.5'";
            end else begin
               stimulus_config.uniform_lower_bound = "'0.01'";
            end
         end else if(gradient_op && dest_fp32_en) begin
            //Formats with 5 bits exponent and fp32 acc need larger range
            if(out_data_format == bfp2 || out_data_format == bfp4  || out_data_format == bfp8) begin
               stimulus_config.uniform_lower_bound = "'-5'";
               stimulus_config.uniform_upper_bound = "'5'";
            end
         end
      end

      $fwrite(out_filehandle, "  stimulus_config_type: %s\n", stimulus_config.Type);
      $fwrite(out_filehandle, "  stimulus_config_uniform_lower_bound: %s\n", stimulus_config.uniform_lower_bound);
      $fwrite(out_filehandle, "  stimulus_config_uniform_upper_bound: %s\n", stimulus_config.uniform_upper_bound);
     end
  endfunction

  function write_binary_comparison_config(integer out_filehandle, e_data_format out_data_format, e_binary_type binary_type);
     begin
       s_comparison_config comparison_config;

       comparison_config.Type = get_comparison_config_type(config_type);
       comparison_config.verbosity = get_comparison_config_verbosity(config_verbosity);

       case (out_data_format)
          bfp8, bfp8_b:  begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.85";
                         comparison_config.check_pcc = "0.99";
                         end
          default:       begin 
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.15";
                         comparison_config.check_pct = "0.90";
                         comparison_config.check_pcc = "0.99";
                         end
       endcase
       $fwrite(out_filehandle, "  comparison_config_type: %s\n", comparison_config.Type);
       $fwrite(out_filehandle, "  comparison_config_atol: %s\n", comparison_config.atol);
       $fwrite(out_filehandle, "  comparison_config_rtol: %s\n", comparison_config.rtol);
       $fwrite(out_filehandle, "  comparison_config_check_pct: %s\n", comparison_config.check_pct);
       $fwrite(out_filehandle, "  comparison_config_check_pcc: %s\n", comparison_config.check_pcc);
       $fwrite(out_filehandle, "  comparison_config_verbosity: %s\n", comparison_config.verbosity);
     end
  endfunction

  function write_binary_stimulus_config(integer out_filehandle, e_data_format out_data_format, e_binary_type binary_type);
     begin
       s_stimulus_config stimulus_config;
       stimulus_config.Type = "'Uniform'";
       stimulus_config.uniform_lower_bound = "'-2.0'";;
       stimulus_config.uniform_upper_bound = "'2.0'";

       if (out_data_format == int8) begin
         stimulus_config.uniform_lower_bound = "'-127.0'";;
         stimulus_config.uniform_upper_bound = "'127.0'";
       end

       $fwrite(out_filehandle, "  stimulus_config_type: %s\n", stimulus_config.Type);
       $fwrite(out_filehandle, "  stimulus_config_uniform_lower_bound: %s\n", stimulus_config.uniform_lower_bound);
       $fwrite(out_filehandle, "  stimulus_config_uniform_upper_bound: %s\n", stimulus_config.uniform_upper_bound);
     end
  endfunction

   function s_comparison_config get_matmul_comparison_config(e_data_format out_data_format, integer num_inputs, integer m_k=1,
   e_math_fidelity math_fidelity=HiFi3, bit gradient_op=0, bit identity=0, e_data_format intermed_data_format=fp16, integer z=0, string relu_mode="");
   begin
      s_comparison_config comparison_config;

      comparison_config.Type = get_comparison_config_type(config_type);
      comparison_config.verbosity = get_comparison_config_verbosity(config_verbosity);

       if (gradient_op) begin
           comparison_config.atol = "0.01";
           comparison_config.rtol = "0.15";
           comparison_config.check_pct = m_k*num_inputs>=40 ? "0.00" : "0.40"; 
           comparison_config.check_pcc = m_k*num_inputs>=20 ? (m_k*num_inputs >= 120 ? "0.80" : "0.90") : (intermed_data_format == fp16_b) ? "0.98" : "0.99";
       end else if (identity) begin
           comparison_config.atol = "0.01";
           comparison_config.rtol = "0.30";
           comparison_config.check_pct = m_k>=50 ? "0.0"  : "0.60"; //FIXME: review
           comparison_config.check_pcc = m_k>=50 ? "0.90" : "0.99"; //FIXME: review
       end else begin
          real accumulate_z_pct = (z>0) ? (m_k*z>=40 ? 0.0 : 0.40) : 0.8;
          real accumulate_z_pcc = (z>0 && m_k*z >= 20) ? (m_k*z >= 120 ? 0.80 : 0.90) : 0.99;
          real out_df_pct = (m_k>=20) ? 0.70 : 0.8;
          real out_df_pcc = (relu_mode == "max") ? 0.95 : 0.99; // Issue: tenstorrent/budabackend#1206
          real intermed_df_pct = (out_data_format == intermed_data_format) ? 0.70 : 0.55;
          real intermed_df_pcc = 0.99;

          case (out_data_format)
             bfp2, bfp2_b:  begin 
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.30";
                            out_df_pct = 0; //FIXME: review
                            out_df_pcc = 0.75;
                            end
             bfp4, bfp4_b:  begin 
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.30";
                            out_df_pct = 0.5; //FIXME: review
                            out_df_pcc = 0.90;
                            end
             bfp8, bfp8_b:  begin 
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.30";
                            out_df_pcc = out_df_pcc < 0.98 ? out_df_pcc: 0.98;
                            end
             fp16, fp32:    begin 
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.15";
                            out_df_pct = 0.8; //FIXME: review
                            end
             default:       begin 
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.15";
                            end
          endcase


          if ((intermed_data_format == bfp8) || (intermed_data_format == bfp8_b)) begin
            intermed_df_pct = 0.4;
            intermed_df_pcc = 0.90;
            
            if(m_k>=20) begin
               intermed_df_pct = 0;
               intermed_df_pcc = 0.90;
            end
            
            if((z*m_k) >= 200) begin
               intermed_df_pct = 0;
               intermed_df_pcc = 0.80;
            end
          end

          comparison_config.check_pct.realtoa($min($min(accumulate_z_pct, out_df_pct), intermed_df_pct)); 
          comparison_config.check_pcc.realtoa($min($min(accumulate_z_pcc, out_df_pcc), intermed_df_pcc)); 
       end
      return comparison_config;
   end
  endfunction

  function write_matmul_comparison_config(integer out_filehandle, e_data_format out_data_format, integer num_inputs, integer m_k=1, 
  e_math_fidelity math_fidelity=HiFi3, bit gradient_op=0, bit identity=0, e_data_format intermed_data_format=fp16, integer z=0, string relu_mode="");
     begin
       s_comparison_config comparison_config = get_matmul_comparison_config(out_data_format, num_inputs, m_k, math_fidelity, gradient_op, identity, intermed_data_format, z, relu_mode);

       $fwrite(out_filehandle, "  comparison_config_type: %s\n", comparison_config.Type);
       $fwrite(out_filehandle, "  comparison_config_atol: %s\n", comparison_config.atol);
       $fwrite(out_filehandle, "  comparison_config_rtol: %s\n", comparison_config.rtol);
       $fwrite(out_filehandle, "  comparison_config_check_pct: %s\n", comparison_config.check_pct);
       $fwrite(out_filehandle, "  comparison_config_check_pcc: %s\n", comparison_config.check_pcc);
       $fwrite(out_filehandle, "  comparison_config_verbosity: %s\n", comparison_config.verbosity);
     end
  endfunction

  // write_matmul_stimulus_config function declaration needs to be the same in every test_config.sv file
  // even if we don't use all arguments in specific implementation
  function write_matmul_stimulus_config(integer out_filehandle, e_data_format out_data_format=fp16);
     begin
       s_stimulus_config stimulus_config;
       stimulus_config.Type = "'Uniform'";
       stimulus_config.uniform_lower_bound = "'-2.0'";;
       stimulus_config.uniform_upper_bound = "'2.0'";

       if (out_data_format == int8 || out_data_format == int32) begin
         stimulus_config.uniform_lower_bound = "'-127.0'";;
         stimulus_config.uniform_upper_bound = "'127.0'";
       end

       $fwrite(out_filehandle, "  stimulus_config_type: %s\n", stimulus_config.Type);
       $fwrite(out_filehandle, "  stimulus_config_uniform_lower_bound: %s\n", stimulus_config.uniform_lower_bound);
       $fwrite(out_filehandle, "  stimulus_config_uniform_upper_bound: %s\n", stimulus_config.uniform_upper_bound);
     end
  endfunction

  function write_topk_comparison_config(integer out_filehandle, e_data_format in_data_format, integer k, e_topk_sort sort);
    begin
      s_comparison_config comparison_config;

      comparison_config.Type = get_comparison_config_type(config_type);
      comparison_config.verbosity = get_comparison_config_verbosity(config_verbosity);
      if (in_data_format == bfp8_b) begin
         if (sort == k_max) begin
            comparison_config.atol = "0.01";
            comparison_config.rtol = "0.01";
            comparison_config.check_pct = "0.70";
            comparison_config.check_pcc = "0.999";
         end else begin
            comparison_config.atol = "0.01";
            comparison_config.rtol = "0.01";
            comparison_config.check_pct = "0.70";
            comparison_config.check_pcc = "0.70";
         end
      end else begin
         comparison_config.atol = "0.01";
         comparison_config.rtol = "0.01";
         comparison_config.check_pct = "1.0";
         comparison_config.check_pcc = "0.99999";
      end

      $fwrite(out_filehandle, "  comparison_config_type: %s\n", comparison_config.Type);
      $fwrite(out_filehandle, "  comparison_config_atol: %s\n", comparison_config.atol);
      $fwrite(out_filehandle, "  comparison_config_rtol: %s\n", comparison_config.rtol);
      $fwrite(out_filehandle, "  comparison_config_check_pct: %s\n", comparison_config.check_pct);
      $fwrite(out_filehandle, "  comparison_config_check_pcc: %s\n", comparison_config.check_pcc);
      $fwrite(out_filehandle, "  comparison_config_verbosity: %s\n", comparison_config.verbosity);
      if (k == 64) begin
         $fwrite(out_filehandle, "  comparison_config_check_tile_cols_range: 32\n");
      end else begin
         $fwrite(out_filehandle, "  comparison_config_check_tile_cols_range: %0d\n", k);
      end
    end
  endfunction

  function write_topk_stimulus_config(integer out_filehandle, integer k);
    begin
      $fwrite(out_filehandle, "  stimulus_config_type: Normal\n");
      $fwrite(out_filehandle, "  stimulus_config_normal_mean: 0.0\n");
      $fwrite(out_filehandle, "  stimulus_config_normal_stddev: 5.0\n");
    end
  endfunction

endclass

`endif

