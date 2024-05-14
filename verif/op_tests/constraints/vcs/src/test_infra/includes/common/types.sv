`define BUFFER_LOC               dram, host
`define MATH_FIDELITY            LoFi, HiFi2, HiFi3, HiFi4
`define DATA_FORMATS             bfp8, bfp8_b, fp16, fp16_b, fp32, bfp4, bfp4_b, bfp2, bfp2_b, int8, int32, RawUInt32
`define RELU_MODE                Min, Max
`define STOCH_RND_MODE           None, Fpu, Pack, All
`define BINARY_SFPU_TYPES        quantization, requantization, dequantization
`define VECTOR_MODE              vector_mode_r, vector_mode_c, vector_mode_rc
`define FLOAT_FORMATS            bfp8, bfp8_b, fp16, fp16_b, fp32, bfp4, bfp4_b, bfp2, bfp2_b
`define FLOAT_OUTPUT_FORMATS     fp16, fp16_b, fp32, bfp8, bfp8_b
`define FLOAT_A_FORMATS          bfp2, bfp4, bfp8, fp16
`define FLOAT_B_FORMATS          bfp2_b, bfp4_b, bfp8_b, fp16_b, fp32
`define UNARY_TYPES              datacopy, nop, exp, log, sqrt, gelu, gelu_derivative, reciprocal, sigmoid, tanh  
`define BINARY_TYPES             add, multiply, subtract, maximum
`define REDUCE_TYPES             max
`define REDUCE_DIMS              r, c, z


`define TILE_SIZE_BFP2           (384)
`define TILE_SIZE_BFP4           (640)
`define TILE_SIZE_BFP8           (1152)
`define TILE_SIZE_INT8           (1152)
`define TILE_SIZE_INT32          (4160)
`define TILE_SIZE_FP16           (2112)
`define TILE_SIZE_FP32           (4160)

`define get_tile_size(data_format) ((data_format==bfp2 | data_format==bfp2_b)      ?`TILE_SIZE_BFP2:  \
                                    (data_format==bfp4 | data_format==bfp4_b)      ?`TILE_SIZE_BFP4:  \
                                    (data_format==bfp8 | data_format==bfp8_b)      ?`TILE_SIZE_BFP8:  \
                                    (data_format==int8)                            ?`TILE_SIZE_INT8:  \
                                    (data_format==int32 | data_format==RawUInt32)  ?`TILE_SIZE_INT32: \
                                    (data_format==fp16 | data_format==fp16_b)      ?`TILE_SIZE_FP16:  \
                                                                              `TILE_SIZE_FP32)


typedef enum {`MATH_FIDELITY}           e_math_fidelity;
typedef enum {`DATA_FORMATS}            e_data_format;
typedef enum {`RELU_MODE}               e_relu_mode;
typedef enum {`STOCH_RND_MODE}          e_stoch_rnd_mode;
typedef enum {`VECTOR_MODE}             e_vector_mode;
typedef enum {`BUFFER_LOC}              e_buffer_loc;
typedef enum {`BINARY_SFPU_TYPES}       e_binary_sfpu_type;
typedef enum {`UNARY_TYPES}             e_unary_type;
typedef enum {`BINARY_TYPES}            e_binary_type;
typedef enum {`REDUCE_TYPES}            e_reduce_type;
typedef enum {`REDUCE_DIMS}             e_reduce_dim;



function string get_math_fidelity(input e_math_fidelity math_fidelity);
   case (math_fidelity)
      LoFi   : get_math_fidelity = "LoFi";
      HiFi2  : get_math_fidelity = "HiFi2";
      HiFi3  : get_math_fidelity = "HiFi3";
      HiFi4  : get_math_fidelity = "HiFi4";
      default: get_math_fidelity = "INVALID";
   endcase 
endfunction

function string get_data_format(input e_data_format data_format);
   case (data_format)
      fp16      : get_data_format = "Float16";
      fp16_b    : get_data_format = "Float16_b";
      bfp8      : get_data_format = "Bfp8";
      bfp8_b    : get_data_format = "Bfp8_b";
      bfp4      : get_data_format = "Bfp4";
      bfp4_b    : get_data_format = "Bfp4_b";
      bfp2      : get_data_format = "Bfp2";
      bfp2_b    : get_data_format = "Bfp2_b";
      fp32      : get_data_format = "Float32";
      int8      : get_data_format = "Int8";
      int32     : get_data_format = "Int32";
      RawUInt32 : get_data_format = "RawUInt32";
      default   : get_data_format = "INVALID";
   endcase 
endfunction

function string get_relu_mode(input e_relu_mode relu_mode);
   case (relu_mode)
      Min : get_relu_mode = "min";
      Max : get_relu_mode = "max";
      default: get_relu_mode = "INVALID";
   endcase 
endfunction

function string get_vector_mode(input e_vector_mode vector_mode);
   case (vector_mode)
      vector_mode_r: get_vector_mode = "r";
      vector_mode_c: get_vector_mode = "c";
      vector_mode_rc: get_vector_mode = "rc";
      default: get_vector_mode = "INVALID";
   endcase
endfunction


function string get_unary_type(input e_unary_type unary_type);
   case (unary_type)
      datacopy        : get_unary_type = "datacopy";
      nop             : get_unary_type = "nop";
      exp             : get_unary_type = "exp";
      log             : get_unary_type = "log";
      sqrt            : get_unary_type = "sqrt";
      gelu            : get_unary_type = "gelu";
      gelu_derivative : get_unary_type = "gelu_derivative";
      reciprocal      : get_unary_type = "reciprocal";
      sigmoid         : get_unary_type = "sigmoid";
      tanh            : get_unary_type = "tanh";
      default         : get_unary_type = "INVALID";
   endcase 
endfunction


function string get_binary_type(input e_binary_type binary_type);
   case (binary_type)
      add      : get_binary_type = "add";
      multiply : get_binary_type = "multiply";
      maximum : get_binary_type = "maximum";
      subtract : get_binary_type = "subtract";
      default  : get_binary_type = "INVALID";
   endcase 
endfunction

function string get_reduce_type(input e_reduce_type reduce_type);
   case (reduce_type)
      max : get_reduce_type = "max";
      default: get_reduce_type = "INVALID";
   endcase 
endfunction


function string get_reduce_dim(input e_reduce_dim reduce_dim);
   case (reduce_dim)
      r : get_reduce_dim = "r";
      c : get_reduce_dim = "c";
      z : get_reduce_dim = "z";
      default: get_reduce_dim = "INVALID";
   endcase 
endfunction

function string get_stoch_rnd_mode(input e_stoch_rnd_mode stoch_rnd_mode);
   case (stoch_rnd_mode)
      None : get_stoch_rnd_mode = "none";
      Fpu : get_stoch_rnd_mode = "fpu";
      Pack : get_stoch_rnd_mode = "pack";
      All: get_stoch_rnd_mode = "all";
      default: get_stoch_rnd_mode = "invalid";
   endcase 
endfunction

// Issue #2080:
// 1x32 tile_dim -> data format can't be bfp4(a|b) or bfp2(a|b): tile_size ends up as 48 and 40 bytes, which is not 32B aligned.
// 2x32 tile_dim -> data format can't be bfp2(a|b): tile_size ends up as 48 bytes.
`define constraint_data_format_tile_dim_1x32(data_format)                                        \
   data_format != bfp2 && data_format != bfp2_b && data_format != bfp4 && data_format != bfp4_b  \

`define constraint_data_format_tile_dim_2x32(data_format)   \
   data_format != bfp2 && data_format != bfp2_b             \

