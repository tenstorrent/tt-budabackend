// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <utility>
#include <boost/functional/hash.hpp>

#include "common/model/base_types.hpp"
#include "netlist/tt_backend_api_types.hpp"

namespace tt 
{

/** 
 * @brief A descriptor of the high-level kernel args. Contains pairs of <member_name>:<value(s)> corresponding to hlk_args_t structs in various HLKs.
*/
class tt_hlk_args_desc {
   public:
    std::vector<std::pair<std::string, std::vector<std::int32_t>>> hlk_args_desc;

    void push_scalar_value(const std::string& arg_name, std::int32_t arg_value) {
        hlk_args_desc.push_back({arg_name, std::vector<std::int32_t>{arg_value}});
    }

    void push_1d_vector_values(const std::string& arg_name, const std::vector<std::int32_t>& arg_values) {
        // Prepend {} to 1d vector name, in order to differentiate it from scalar values and 2d vectors
        hlk_args_desc.push_back({"{}" + arg_name, std::vector<std::int32_t>(arg_values)});
    }

    void push_2d_vector_values(const std::string& arg_name, const std::vector<std::vector<std::int32_t>> arg_values) {
        std::vector<std::int32_t> serialized_arg_values;

        // All columns must have the same size
        std::uint32_t num_cols = arg_values[0].size();

        // First push number of columns to the array
        serialized_arg_values.push_back(num_cols);
        for (std::uint32_t i = 0; i < arg_values.size(); i++) {
            log_assert(
                arg_values[i].size() == num_cols,
                "All columns of 2d array should be of size {}, but number of columns at row {} is {}",
                num_cols,
                i,
                arg_values[i].size());
            for (std::uint32_t j = 0; j < num_cols; j++) {
                serialized_arg_values.push_back(arg_values[i][j]);
            }
        }

        // Prepend {}{} to 2d vector name, in order to differentiate it from scalar values and 1d vectors
        hlk_args_desc.push_back({"{}{}" + arg_name, std::vector<std::int32_t>(serialized_arg_values)});
    }

    std::string print() const {
        // Print out args
        std::ostringstream args;
        args << "const hlk_args_t hlk_args = {\n";

        for (std::uint32_t i = 0; i < hlk_args_desc.size(); i++) {
            size_t two_dim_arr_pos = hlk_args_desc[i].first.find("{}{}");
            bool is_two_dim_array = two_dim_arr_pos != std::string::npos;

            // 2d array special case
            if (is_two_dim_array) {
                // If we have 2d array, the first arg in descriptor is number of columns.
                // E.g. int two_dim_array[3][5], the first arg is 5.

                int num_cols = hlk_args_desc[i].second[0];
                int num_elements = hlk_args_desc[i].second.size() - 1;

                log_assert(
                    num_cols != 0,
                    "The first arg in hlk_args_descriptor in case of 2d arrays represents the number of columns of "
                    "that 2d array. The passed in value is 0");
                log_assert(
                    (num_elements > 0) && (num_elements % num_cols == 0),
                    "Number of elements in array {} is {}, it needs to be divisible by {}.",
                    hlk_args_desc[i].first,
                    num_elements,
                    num_cols);

                std::string member_name = hlk_args_desc[i].first;

                // Erase {}{} from the name
                member_name = member_name.erase(two_dim_arr_pos, 4);
                args << "." << member_name << " =\n"
                     << "{\n";

                int num_elements_in_current_row = 0;
                for (std::uint32_t j = 1; j < hlk_args_desc[i].second.size(); j++) {
                    if (num_elements_in_current_row == 0) {
                        args << "{\n";
                    }

                    args << std::to_string(hlk_args_desc[i].second[j]) + ",\n";

                    num_elements_in_current_row++;
                    if (num_elements_in_current_row == num_cols) {
                        args << "},\n";
                        num_elements_in_current_row = 0;
                    }
                }

                args << "},\n";
            } else {
                size_t one_dim_arr_pos = hlk_args_desc[i].first.find("{}");
                bool is_one_dim_array = one_dim_arr_pos != std::string::npos;

                std::string member_name = hlk_args_desc[i].first;
                if (is_one_dim_array) {
                    // Erase {} from the name if we have array
                    member_name = member_name.erase(one_dim_arr_pos, 2);
                }
                args << "." << member_name << " = ";

                if (is_one_dim_array) {
                    args << "\n{\n";
                }

                for (std::uint32_t j = 0; j < hlk_args_desc[i].second.size(); j++) {
                    args << std::to_string(hlk_args_desc[i].second[j]) << ",\n";
                }

                if (is_one_dim_array) {
                    args << "},\n";
                }
            }
        }
        args << "};\n";

        return args.str();
    }
};

/** 
 * @brief A descriptor of the high-level kernel. Contains I/O buffer sizing/format, HLK filename, HLK args descriptor.
*/
class tt_hlk_desc
{
    public:
    int core_rc[2] = {0};
    int input_buf_size_arr[8] = {0};
    int param_buf_size_arr[8] = {0};
    int output_buf_size_arr[8] {0};
    int intermediate_buf_size_arr[8] {0};

    // data formats spec for the I/O operands (i.e., buffers)
    DataFormat input_buf_dataformat_arr[8];
    DataFormat param_buf_dataformat_arr[8];
    DataFormat output_buf_dataformat_arr[8];
    DataFormat intermediate_buf_dataformat_arr[8];

    tt::MathFidelity math_fidelity;
    bool approximation_mode;

    std::string hlk_cpp_file_name; // HLK kernel file name (user writes)
    tt::tt_hlk_args_desc hlk_args_descriptor;  

    // Allowed to acquire/release dest without packing anything. Used to bypass an assert in model, not a problem on HW
    bool allow_unpacked_dst = false;
    tt_hlk_desc()
    {
        core_rc[0] = -1;
        core_rc[1] = -1;

        math_fidelity = tt::MathFidelity::Invalid;
        hlk_cpp_file_name = "";

        approximation_mode = true;

        for (int i = 0; i < 8; ++i)
        {
            input_buf_size_arr[i] = 0;
            param_buf_size_arr[i] = 0;
            output_buf_size_arr[i] = 0;
            intermediate_buf_size_arr[i] = 0;

            input_buf_dataformat_arr[i] = DataFormat::Invalid;
            param_buf_dataformat_arr[i] = DataFormat::Invalid;
            output_buf_dataformat_arr[i] = DataFormat::Invalid;
            intermediate_buf_dataformat_arr[i] = DataFormat::Invalid;
        }
    }

    tt_hlk_desc(tt_hlk_desc &in)
    {
        core_rc[0] = in.core_rc[0];
        core_rc[1] = in.core_rc[1];

        for(int i=0;i<8;++i)
        {
            input_buf_size_arr[i]  = in.input_buf_size_arr[i] ;
            param_buf_size_arr[i] = in.param_buf_size_arr[i] ;
            output_buf_size_arr[i] = in.output_buf_size_arr[i];
            intermediate_buf_size_arr[i] = in.intermediate_buf_size_arr[i];

            input_buf_dataformat_arr[i]  = in.input_buf_dataformat_arr[i] ;
            param_buf_dataformat_arr[i] = in.param_buf_dataformat_arr[i] ;
            output_buf_dataformat_arr[i] = in.output_buf_dataformat_arr[i];
            intermediate_buf_dataformat_arr[i] = in.intermediate_buf_dataformat_arr[i];
        }

        math_fidelity = in.math_fidelity;
        hlk_cpp_file_name = in.hlk_cpp_file_name;
        approximation_mode = in.approximation_mode;  
    }
    void set_input_buf_size(int buf_idx, int size)
    {
        input_buf_size_arr[buf_idx] = size;
    }

    void set_param_buf_size(int buf_idx, int size)
    {
        param_buf_size_arr[buf_idx] = size;
    }

    void set_output_buf_size(int buf_idx, int size)
    {
        output_buf_size_arr[buf_idx] = size;
    }

    void set_intermediate_buf_size(int buf_idx, int size)
    {
        intermediate_buf_size_arr[buf_idx] = size;
    }

    int get_input_buf_size(int buf_idx) const
    {
        return input_buf_size_arr[buf_idx];
    }

    int get_param_buf_size(int buf_idx) const
    {
        return param_buf_size_arr[buf_idx];
    }

    int get_output_buf_size(int buf_idx) const
    {
        return output_buf_size_arr[buf_idx];
    }

    int get_intermediate_buf_size(int buf_idx) const
    {
        return intermediate_buf_size_arr[buf_idx];
    }

    DataFormat get_input_buf_dataformat(int buf_idx) const
    {
        return input_buf_dataformat_arr[buf_idx];
    }

    void set_input_buf_dataformat(int buf_idx, DataFormat data_format)
    {
        input_buf_dataformat_arr[buf_idx] = data_format;
    }

    DataFormat get_param_buf_dataformat(int buf_idx) const
    {
        return param_buf_dataformat_arr[buf_idx];
    }

    void set_param_buf_dataformat(int buf_idx, DataFormat data_format)
    {
        param_buf_dataformat_arr[buf_idx] = data_format;
    }

    DataFormat get_output_buf_dataformat(int buf_idx) const
    {
        return output_buf_dataformat_arr[buf_idx];
    }

    void set_output_buf_dataformat(int buf_idx, DataFormat data_format)
    {
        output_buf_dataformat_arr[buf_idx] = data_format;
    }

    DataFormat get_intermediate_buf_dataformat(int buf_idx) const
    {
        return intermediate_buf_dataformat_arr[buf_idx];
    }

    void set_intermediate_buf_dataformat(int buf_idx, DataFormat data_format)
    {
        intermediate_buf_dataformat_arr[buf_idx] = data_format;
    }
    
    void set_hlk_cpp_file_name(const std::string & file_name)
    {
        hlk_cpp_file_name = file_name;
    }

    const std::string & get_hlk_cpp_file_name()
    {
        return hlk_cpp_file_name;
    }

    void set_hlk_math_fidelity(tt::MathFidelity math_fi)
    {
        math_fidelity = math_fi;
    }

    tt::MathFidelity get_hlk_math_fidelity() const
    {
        return math_fidelity;
    }

    void set_hlk_math_approx_mode(bool approx_mode)
    {
        approximation_mode = approx_mode;
    }

    bool get_hlk_math_approx_mode() const
    {
        return approximation_mode;
    }

    void set_hlk_allow_unpacked_dst(bool in_allow_unpacked_dst)
    {
        allow_unpacked_dst = in_allow_unpacked_dst;
    }

    bool get_hlk_allow_unpacked_dst() const
    {
        return allow_unpacked_dst;
    }
};  // tt_hlk_desc
}  // namespace tt

template<>
struct std::hash<tt::tt_hlk_args_desc>
{
    std::size_t operator()(tt::tt_hlk_args_desc const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (std::uint32_t i = 0; i < obj.hlk_args_desc.size(); i++)
        {
            const std::pair<std::string, std::vector<int>> pair = obj.hlk_args_desc[i];
            boost::hash_combine(hash_value, hash<std::string>{}(pair.first));
            for (std::uint32_t j = 0; j < pair.second.size(); j++)
            {
                boost::hash_combine(hash_value, hash<std::int32_t>{}(pair.second[j]));
            }
        }
        return hash_value;
    }
};

template<>
struct std::hash<tt::tt_hlk_desc>
{
    std::size_t operator()(tt::tt_hlk_desc const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (int i = 0; i < 2; i++)
        {
            boost::hash_combine(hash_value, hash<int>{}(obj.core_rc[i]));
        }
        for (int i = 0; i < 8; i++)
        {
            boost::hash_combine(hash_value, hash<int>{}(obj.input_buf_size_arr[i]));
            boost::hash_combine(hash_value, hash<int>{}(obj.param_buf_size_arr[i]));
            boost::hash_combine(hash_value, hash<int>{}(obj.output_buf_size_arr[i]));
            boost::hash_combine(hash_value, hash<int>{}(obj.intermediate_buf_size_arr[i]));
        }
        for (int i = 0; i < 8; i++)
        {
            boost::hash_combine(hash_value, hash<tt::DataFormat>{}(obj.input_buf_dataformat_arr[i]));
            boost::hash_combine(hash_value, hash<tt::DataFormat>{}(obj.param_buf_dataformat_arr[i]));
            boost::hash_combine(hash_value, hash<tt::DataFormat>{}(obj.output_buf_dataformat_arr[i]));
            boost::hash_combine(hash_value, hash<tt::DataFormat>{}(obj.intermediate_buf_dataformat_arr[i]));
        }
        boost::hash_combine(hash_value, hash<tt::MathFidelity>{}(obj.math_fidelity));
        boost::hash_combine(hash_value, hash<bool>{}(obj.approximation_mode));
        boost::hash_combine(hash_value, hash<bool>{}(obj.allow_unpacked_dst));
        boost::hash_combine(hash_value, hash<tt::tt_hlk_args_desc>{}(obj.hlk_args_descriptor));

        return hash_value;
    }
};