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

  function write_unary_comparison_config(integer out_filehandle, e_data_format out_data_format, e_unary_type unary_type);
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
         bfp4, bfp4_b:  begin
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.80";
                         if (unary_type == gelu_derivative || unary_type == sqrt || unary_type == exp) comparison_config.check_pcc = "0.95"; //ops cause PCC drop
                         else comparison_config.check_pcc = "0.99";
                         end
         bfp2, bfp2_b:  begin
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.30";
                         comparison_config.check_pct = "0.85";
                         comparison_config.check_pcc = "0.99";
                         end
          default:       begin
                         comparison_config.atol = "0.01";
                         comparison_config.rtol = "0.15";
                         if (unary_type == gelu_derivative) comparison_config.check_pct = "0.88"; //adjust for gelu derivative
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
     end
  endfunction

  function write_unary_stimulus_config(integer out_filehandle, e_unary_type unary_type, e_data_format in_data_format, e_data_format out_data_format);
     begin
       s_stimulus_config stimulus_config;
       stimulus_config.Type = "'Uniform'";
       stimulus_config.uniform_lower_bound = "'-1.0'";;
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

  function write_binary_stimulus_config(integer out_filehandle, e_binary_type binary_type);
     begin
       s_stimulus_config stimulus_config;
       stimulus_config.Type = "'Uniform'";
       stimulus_config.uniform_lower_bound = "'-2.0'";;
       stimulus_config.uniform_upper_bound = "'2.0'";

       $fwrite(out_filehandle, "  stimulus_config_type: %s\n", stimulus_config.Type);
       $fwrite(out_filehandle, "  stimulus_config_uniform_lower_bound: %s\n", stimulus_config.uniform_lower_bound);
       $fwrite(out_filehandle, "  stimulus_config_uniform_upper_bound: %s\n", stimulus_config.uniform_upper_bound);
     end
  endfunction

  function write_matmul_comparison_config(integer out_filehandle, e_data_format out_data_format, integer m_k=1, e_math_fidelity math_fidelity=HiFi3, bit gradient_op=0);
     begin
       s_comparison_config comparison_config;

       comparison_config.Type = get_comparison_config_type(config_type);
       comparison_config.verbosity = get_comparison_config_verbosity(config_verbosity);

       if (gradient_op) begin
           comparison_config.atol = "0.01";
           comparison_config.rtol = "0.15";
           comparison_config.check_pct = m_k>=20 ? "0.00" : "0.60"; //FIXME: review
           comparison_config.check_pcc = m_k>=20 ? "0.90" : "0.99"; //FIXME: review
       end else begin
          case (out_data_format)
             bfp8, bfp8_b:  begin
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.30";
                            if (math_fidelity == LoFi) begin
                               if (m_k>=20) begin
                                  comparison_config.check_pct = "0.55"; //FIXME: review
                               end else begin
                                  comparison_config.check_pct = "0.70"; //FIXME: review
                               end
                            end else begin
                               if (m_k>=20) begin
                                  comparison_config.check_pct = "0.70"; //FIXME: review
                               end else begin
                                  comparison_config.check_pct = "0.80"; //FIXME: review
                               end
                            end
                            comparison_config.check_pcc = "0.99";
                            end
             fp16, fp32:    begin
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.15";
                            comparison_config.check_pct = "0.80"; //FIXME: review
                            comparison_config.check_pcc = "0.99";
                            end
             default:       begin
                            comparison_config.atol = "0.01";
                            comparison_config.rtol = "0.15";
                            comparison_config.check_pct = "0.80"; //FIXME: review
                            comparison_config.check_pcc = "0.99";
                            end
          endcase
       end
       $fwrite(out_filehandle, "  comparison_config_type: %s\n", comparison_config.Type);
       $fwrite(out_filehandle, "  comparison_config_atol: %s\n", comparison_config.atol);
       $fwrite(out_filehandle, "  comparison_config_rtol: %s\n", comparison_config.rtol);
       $fwrite(out_filehandle, "  comparison_config_check_pct: %s\n", comparison_config.check_pct);
       $fwrite(out_filehandle, "  comparison_config_check_pcc: %s\n", comparison_config.check_pcc);
       $fwrite(out_filehandle, "  comparison_config_verbosity: %s\n", comparison_config.verbosity);
     end
  endfunction

  function write_matmul_stimulus_config(integer out_filehandle);
     begin
       s_stimulus_config stimulus_config;
       stimulus_config.Type = "'Uniform'";
       stimulus_config.uniform_lower_bound = "'-2.0'";;
       stimulus_config.uniform_upper_bound = "'2.0'";

       $fwrite(out_filehandle, "  stimulus_config_type: %s\n", stimulus_config.Type);
       $fwrite(out_filehandle, "  stimulus_config_uniform_lower_bound: %s\n", stimulus_config.uniform_lower_bound);
       $fwrite(out_filehandle, "  stimulus_config_uniform_upper_bound: %s\n", stimulus_config.uniform_upper_bound);
     end
  endfunction

endclass

`endif

