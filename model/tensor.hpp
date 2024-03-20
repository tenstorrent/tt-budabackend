// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>

#include "model/op.hpp"
#include "tile.hpp"
#include "common/model/tensor_hierarchy_metadata.hpp"
#include "buffer_grid.hpp"

// Utils
using std::uint32_t;

// Functions
using std::function;

// Datastructures
using std::array;
using std::deque;
using std::string;
using std::vector;
using namespace tt;
namespace tt
{

class tt_tensor
{
public:
    // tt_tensor owns (creates/destroys) its metadata
    tt_tensor_metadata metadata;
public:
    using tile_data = vector <vector <vector <vector <tt_tile> > > >;
    using tile_buffer_ptrs = vector <vector <vector <vector <tt_tile_buffer *> > > >;
    tile_buffer_ptrs tile_map = {};
    bool tile_level_mappings = false;
    bool owns_tile_buffers = false;
    void delete_tile_buffers()
    {
        for(uint32_t wi=0;wi<getw();++wi)
        {
            for(uint32_t zi=0;zi<getz();++zi)
            {
                for(uint32_t ri=0;ri<getrt();++ri)
                {
                    for(uint32_t ci=0;ci<getct();++ci)
                    {
                        delete tile_map[wi][zi][ri][ci];
                    }
                }
            }
        }

    }
    int grid_shape_r() {
        return tile_map[0][0].size();
        }
    int grid_shape_c() {return tile_map[0][0][0].size();}
    bool is_tile_mapped() {return tile_level_mappings;}
    tt_tile_buffer *get_tile_buf_ptr(int w, int z, int rt, int ct);
    ~tt_tensor()
    {
        if(owns_tile_buffers) this->delete_tile_buffers();
    }
    tile_data tile_tensor;
    vector<float> flat_tensor_data;
    vector<uint16_t> flat_tensor_data_half;
    vector<uint32_t> flat_untilized_tensor_data;
    int total_tiles() const;
    // rti and cti are tensor dimensions in tiles
    // zi and wi are regular scalar dimensions
    tt_tensor() {};
    tt_tensor(const tt_tensor &in);
    tt_tensor(tt_tensor &&in);
    tt_tensor(tt_tensor_metadata const &metadata);
    tt_tensor(tt_shape shape, DataFormat data_format, TensorType tensor_type=TensorType::Activation, bool requires_grad = false, DataFormat gradient_data_format = DataFormat::Invalid, DataFormat optimizer_data_format = DataFormat::Invalid);
    tt_tensor(tt_shape shape, const vector<tt_tile>& tiles, TensorType tensor_type=TensorType::Activation, bool requires_grad = false);
    static tt_tensor make_untilized_tensor(tt_tensor_metadata metadata);

    static tt_tensor *allocate_on_heap(tt_tensor const &&tensor);
    

    tt_tensor copy(bool shape_only = false) const;
    tt_tensor* copy_on_heap() const;
    tt_tensor reshape(tt_shape new_shape, bool shape_only = false) const;
    tt_tensor &operator=(const tt_tensor &rhs);
    tt_tensor& operator=(tt_tensor &&rhs);
    void operator = (float val);
    float &at(int w_index, int z_index, int rt_index, int ct_index, int r_index, int c_index);
    float at(int w_index, int z_index, int rt_index, int ct_index, int r_index, int c_index) const;
    tt_tile get_tile(int rti, int cti, int zi, int wi) const;
    tt_tile* get_tile_ptr(int rti, int cti, int zi, int wi, uint32_t height = 32, uint32_t width = 32) const;
    tt_tile get_tile_from_flat_index(int index, Dim tile_order_dim) const;
    tt_tile* get_tile_ptr_from_flat_index(int index, Dim tile_order_dim) const;

    // metadata accessors
    bool same_shape(const tt_tensor &other) const;
    DataFormat get_data_format() const;
    void set_data_format(DataFormat data_format);
    tt_shape get_shape() const;
    void set_shape(const tt_shape& shape);
    tt_block_shape get_block_shape() const;
    void set_block_shape(const tt_block_shape&);
    tt_grid_shape get_grid_shape() const;
    void set_grid_shape(const tt_grid_shape&);
    TensorType get_tensor_type() const;
    void set_tensor_type(const TensorType& tensor_type);
    tt_op* get_producer_op_ptr() const;
    void set_producer_op_ptr(tt_op* producer_op_ptr);
    std::uint32_t get_producer_op_output_index() const;
    void set_producer_op_output_index(std::uint32_t producer_op_output_index);
    bool get_requires_grad() const;
    void set_requires_grad(bool requires_grad);
    DataFormat get_gradient_data_format() const;
    DataFormat get_optimizer_data_format() const;
    void set_gradient_data_format(DataFormat data_format);
    void set_optimizer_data_format(DataFormat data_format);
    bool is_tilized() const;
    void set_is_tilized(bool is_tilized);
    void tilize_inplace(bool is_c_contiguous, bool shape_only, bool force_megarow = false);
    void untilize_to_flat_tensor_data(bool is_c_contiguous, bool shape_only, bool force_megarow, vector<float>& flattened_tensor_data_vec);
    void untilize_to_flat_tensor_data_half(bool is_c_contiguous, bool shape_only, vector<uint16_t>& flattened_tensor_data_vec);
    void untilize_to_flat_tensor_data_byte(bool is_c_contiguous, bool shape_only, vector<int8_t>& flattened_tensor_data_vec);
    void untilize_to_flat_tensor_data_unsigned_byte(bool is_c_contiguous, bool shape_only, vector<uint8_t>& flattened_tensor_data_vec);
    void untilize_to_flat_tensor_data_dword(bool is_c_contiguous, bool shape_only, vector<int32_t>& flattened_tensor_data_vec);
    void set_epilogue_dram_io(tt_dram_io* dram_io);
    tt_dram_io* get_epilogue_dram_io();

    uint32_t getrt() const;
    uint32_t getct() const;
    uint32_t getrfull() const;
    uint32_t getcfull() const;
    uint32_t getz() const;
    uint32_t getw() const;
    uint32_t get_tile_height() const;
    uint32_t get_tile_width() const;
    bool is_shape_only() const;
    int get_num_stored_tiles() const;
    void reserve_tile_tensor();

    tt_tensor::tile_data extract_tiles_from_range(tt_dims range_start, tt_dims range_end) const;
    tt_tensor extract_range_to_tensor(tt_dims range_start, tt_dims range_end, bool shape_only) const;

    static tile_data get_tensor_tile_data_from_vector(tt_shape shape, DataFormat data_format, vector<vector<vector<vector<int>>>> &data_vector);
    template <typename T, typename = std::enable_if_t<std::is_arithmetic<T>::value, T>>
    static tile_data get_tensor_tile_data_from_distribution(
        tt_shape shape, DataFormat data_format, std::function<void(tt_tile &)> tile_distribution_function);
    static tile_data get_randomized_tensor_tile_data(tt_shape shape, DataFormat data_format, float mean=0.0, float stddev=0.25);
    static tile_data get_randomized_manual_float_tile_data(tt_shape shape, DataFormat data_format, int spread, int man_variance_bits);
    static tile_data get_arange_tensor_tile_data(tt_shape shape, DataFormat data_format, float start = 0.0f);
    static tile_data get_randomized_uniform_float_tile_data(tt_shape shape, DataFormat data_format, float lower_bound = 0.0f, float upper_bound = 1.0f);
    static tt_shape get_tile_tensor_shape(const tile_data& tile_tensor);
    void fill_with_data(const tt_tensor& source_tensor, const tt_dims offset={0,0,0,0});
    void fill_with_data(const tile_data& source_tile_tensor);
    void fill_with_data(const vector<float>& source_data);
    void apply(function<void(tt_tile&)> tile_operation);
    void apply_parallel(function<void(tt_tile&)> tile_operation);
    void pack_data(int tile_height = 32, int tile_width = 32);
    void clear_packed_data();
    void clear_tile_values(int tile_dim_r, int tile_dim_c);
    void set_tile_dims(int tile_dim_r, int tile_dim_c);
    void unpack_data();
    bool packed_data_present();
    void adjust_tensor_for_accuracy(bool truncate_bfp_mantissa = true);
    bool check_for_max_abs_value(float max_value);
    void adjust_man_prec(uint32_t man_prec);
    void randomize(float mean=0.0, float stddev=0.25);
    void randomize_uniform(float lower_bound=0.0, float upper_bound=1.0);
    void randomize_diagonal(float mean = 0.0, float stddev = 0.25);
    void randomize_manual_float(int spread, int man_variance_bits);
    void randomize_per_col_mask(int spread, int man_variance_bits, vector < vector <vector <bool>>> &tensor_col_masks);
    void init_to_xyz_values();
    void init_to_tile_id(float step_size=1.0, float base_val=0.0);
    void init_to_input_value(float step_size=0.0);
    int size_bytes(bool include_header_padding = true);
    int size_bytes(DataFormat input_data_format, bool include_header_padding = true);
    void set_number(float num);
    void set_gradient(tt_tensor* gradient);
    tt_tensor* get_gradient();
    void set_transposed_parameter_copy(tt_tensor* transposed_copy);
    tt_tensor* get_transposed_parameter_copy();

    // Functions for stacking tensor values along a dim in-place
    void hstack(tt_tensor &in_tensor, bool shape_only);
    void vstack(tt_tensor &in_tensor, bool shape_only);
    void zstack(tt_tensor &in_tensor, bool shape_only);
    void wstack(tt_tensor &in_tensor, bool shape_only);
    static tt_tensor wstack(vector<tt_tensor> &in_tensors);

    // Functions for taking the input vectors and merging it into the tensor
    tt_tensor hmerge(const vector<tt_tensor>& inputs, bool shape_only) const;
    tt_tensor vmerge(const vector<tt_tensor>& inputs, bool shape_only) const;
    // Functions for splitting tensors along a dim
    vector<tt_tensor> hsplit(int split_factor, bool shape_only) const;
    vector<tt_tensor> vsplit(int split_factor, bool shape_only) const;
    vector<tt_tensor> zsplit(bool shape_only) const;
    vector<tt_tensor> wsplit(bool shape_only) const;

    // Special reshape functions for extracting z-dim from r-dim/c-dim correctly (mainly used for reshaping tensors gathered from output buffers or to set desired batch_size)
    tt_tensor reshape_c_dim_into_z_dim_and_c_dim(uint32_t z_scaler, bool shape_only=false) const;
    tt_tensor reshape_r_dim_into_z_dim_and_r_dim(uint32_t target_z, bool shape_only=false) const;

    // Functions for stacking z-dim into r-dim/c-dim
    tt_tensor reshape_z_dim_into_c_dim(uint32_t z_scaler=0, bool shape_only = false) const;
    tt_tensor reshape_z_dim_into_r_dim(uint32_t z_scaler=0, bool shape_only = false) const;

    tt_tensor matmul(const tt_tensor &rhs, bool shape_only = false, bool fast = true) const;
    tt_tensor subtract(const tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor add(const tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor multiply(const tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor batched_matmul(tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor reduce(ReduceFunc reduce_func, Dim dim, bool shape_only = false, uint32_t z_dim=1) const;
    tt_tensor conv1x1_matmul(tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor depthwise1x1_matmul(tt_tensor &rhs, bool shape_only = false) const;
    tt_tensor operator - (const tt_tensor &rhs) const;
    // transpose_xy entire xy plane of the tensor (i.e., global)
    tt_tensor transpose_xy(bool shape_only = false, bool tiles_only = false, bool tile_only = false) const;
    tt_tensor transpose_yz(bool shape_only = false) const;
    tt_tensor operator + (const tt_tensor &rhs) const;
    tt_tensor operator*(const tt_tensor &rhs) const;
    tt_tensor unary(function<tt_tile(const tt_tile&)> func, bool shape_only = false) const;
    tt_tensor exp(bool shape_only = false) const;
    tt_tensor sine(bool shape_only = false) const;
    tt_tensor cosine(bool shape_only = false) const;
    tt_tensor log(bool shape_only = false) const;
    tt_tensor sqrt(bool shape_only = false) const;
    tt_tensor dropout(bool shape_only = false, float prob=1.0f) const;
    tt_tensor sigmoid(bool shape_only = false) const;
    tt_tensor abs(bool shape_only = false) const;
    tt_tensor relu(bool shape_only = false) const;
    tt_tensor lrelu(bool shape_only, float slope = 0.01f) const; // Default slope is equivalent to pytorch default
    tt_tensor relu_with_threshold(bool shape_only = false, float threshold = 0.0f, ReluMode mode = ReluMode::None) const;
    tt_tensor gelu(bool shape_only = false) const;
    tt_tensor gelu_derivative(bool shape_only = false) const;
    tt_tensor reciprocal(bool shape_only = false) const;
    tt_tensor tanh(bool shape_only = false) const;
    tt_tensor shift_y(int amount, bool shape_only = false) const;
    tt_tensor shift_z(int amount, bool shape_only = false) const;
    tt_tensor stride_y(int stride, int stride_offset = 0, bool shape_only = false) const;
    tt_tensor stride_z(int stride, int stride_offset = 0, bool shape_only = false) const;
    void dump() const;
    void dump(const string &file_name) const;
    string get_string() const;
    tt_tensor broadcast(const tt_shape& shape, Dim dim, bool shape_only = false) const;
    tt_tensor broadcast_tiles(const tt_shape& shape, Dim dim, bool shape_only = false) const;
    tt_tensor broadcast_within_tiles(Dim dim, bool shape_only) const;
    tt_tensor eltwise_binary_with_broadcast(const tt_tensor &rhs, BinaryOp binary_op, Dim dim, bool shape_only = false) const;

    tt_tensor concatenate(const tt_tensor &rhs, Dim dim, bool shape_only = false) const;
    tt_tensor slice(Dim dim, uint32_t start, uint32_t end, bool shape_only = false) const;
};

} // end namespace tt
