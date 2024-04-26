
$binary_intermediate2_streaming_test_graph = {}

##

num_msgs = $PARAMS[:num_tiles] ? $PARAMS[:num_tiles] : 8
msg_size = $PARAMS[:tile_size] ? $PARAMS[:tile_size] :(1024 * 2) + 16 + 16

num_input0_park_buf_msgs =  $PARAMS[:num_input0_park_buf_msgs] ?  $PARAMS[:num_input0_park_buf_msgs] : num_msgs;
num_input1_park_buf_msgs =  $PARAMS[:num_input1_park_buf_msgs] ?  $PARAMS[:num_input1_park_buf_msgs] : num_msgs;
input0_park_buf_msg_size =  $PARAMS[:input0_park_buf_tile_size] ?  $PARAMS[:input0_park_buf_tile_size] : msg_size;
input1_park_buf_msg_size =  $PARAMS[:input1_park_buf_tile_size] ?  $PARAMS[:input1_park_buf_tile_size] : msg_size;

input0_park_buf_size = num_input0_park_buf_msgs*input0_park_buf_msg_size
input1_park_buf_size = num_input1_park_buf_msgs*input1_park_buf_msg_size

num_input0_data_buf_msgs =  $PARAMS[:num_input0_data_buf_msgs] ?  $PARAMS[:num_input0_data_buf_msgs] : num_msgs;
num_input1_data_buf_msgs =  $PARAMS[:num_input1_data_buf_msgs] ?  $PARAMS[:num_input1_data_buf_msgs] : num_msgs;
input0_data_buf_msg_size =  $PARAMS[:input0_data_buf_tile_size] ?  $PARAMS[:input0_data_buf_tile_size] : msg_size;
input1_data_buf_msg_size =  $PARAMS[:input1_data_buf_tile_size] ?  $PARAMS[:input1_data_buf_tile_size] : msg_size;

input0_data_buf_size = num_input0_data_buf_msgs*input0_data_buf_msg_size
input1_data_buf_size = num_input1_data_buf_msgs*input1_data_buf_msg_size

num_output0_data_buf_msgs =  $PARAMS[:num_output0_data_buf_msgs] ?  $PARAMS[:num_output0_data_buf_msgs] : num_msgs;
num_output0_park_buf_msgs =  $PARAMS[:num_output0_park_buf_msgs] ?  $PARAMS[:num_output0_park_buf_msgs] : num_msgs;
output0_data_buf_msg_size =  $PARAMS[:output0_data_buf_tile_size] ?  $PARAMS[:output0_data_buf_tile_size] : msg_size;
output0_park_buf_msg_size =  $PARAMS[:output0_park_buf_tile_size] ?  $PARAMS[:output0_park_buf_tile_size] : msg_size;

output0_data_buf_size = num_output0_data_buf_msgs*output0_data_buf_msg_size
output0_park_buf_size = num_output0_park_buf_msgs*output0_park_buf_msg_size

data_buffer_space_base = $PARAMS[:data_buffer_space_base] + $extra_tile_header_buffer_size
input0_data_base = $PARAMS[:input0_data_base] ? $PARAMS[:input0_data_base] : data_buffer_space_base;
input1_data_base = $PARAMS[:input1_data_base] ? $PARAMS[:input1_data_base] : (data_buffer_space_base + (128 * 1024))
output0_data_base = $PARAMS[:output0_data_base] ? $PARAMS[:output0_data_base] : (data_buffer_space_base + (256 * 1024));
intermediate0_data_base = $PARAMS[:intermediate0_data_base] ? $PARAMS[:intermediate0_data_base] : output0_data_base + (128 * 1024);
intermediate1_data_base = $PARAMS[:intermediate1_data_base] ? $PARAMS[:intermediate1_data_base] : intermediate0_data_base + (128 * 1024);

input0_park_stream_id = $PARAMS[:input0_park_stream_id] ? $PARAMS[:input0_park_stream_id] : 1
input1_park_stream_id = $PARAMS[:input1_park_stream_id] ? $PARAMS[:input1_park_stream_id] : 2
output0_park_stream_id = $PARAMS[:output0_park_stream_id] ? $PARAMS[:output0_park_stream_id] : 3

input0_data_stream_id = $PARAMS[:input0_data_stream_id] ? $PARAMS[:input0_data_stream_id] : $PARAMS[:chip] == "grayskull" ? 8 : 4
input1_data_stream_id = $PARAMS[:input1_data_stream_id] ? $PARAMS[:input1_data_stream_id] : $PARAMS[:chip] == "grayskull" ? 9 : 5
output0_data_stream_id = $PARAMS[:output0_data_stream_id] ? $PARAMS[:output0_data_stream_id] : 24

intermediate0_data_buf_msg_size = output0_data_buf_msg_size
num_intermediate0_data_buf_msgs = num_output0_data_buf_msgs
intermediate0_data_buf_size = num_intermediate0_data_buf_msgs*intermediate0_data_buf_msg_size
intermediate0_stream_id = $PARAMS[:intermediate0_stream_id] ? $PARAMS[:intermediate0_stream_id] : 32

intermediate1_data_buf_msg_size = output0_data_buf_msg_size
num_intermediate1_data_buf_msgs = num_output0_data_buf_msgs
intermediate1_data_buf_size = num_intermediate1_data_buf_msgs*intermediate1_data_buf_msg_size
intermediate1_stream_id = $PARAMS[:intermediate1_stream_id] ? $PARAMS[:intermediate1_stream_id] : 33
##

num_phases = 1

chip_id = 0
x = 0
y = 0

input0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_data_stream_id}"
input0_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_park_stream_id}"
input1_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input1_data_stream_id}"
input1_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input1_park_stream_id}"
output0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_data_stream_id}"
output0_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_park_stream_id}"
intermediate0_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{intermediate0_stream_id}"
intermediate1_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{intermediate1_stream_id}"

start_seed_0 = 0xa0000000
start_seed_1 = 0xb0000000
start_seed_2 = 0x00000000

for p in 1..num_phases

  phase = :"phase_#{p}"

  $binary_intermediate2_streaming_test_graph[phase] = {}

  $binary_intermediate2_streaming_test_graph[phase][input0_data_stream] = {
    :input_index => 0,
    :auto_run => true,
    :buf_addr => input0_data_base,
    :buf_size => input0_data_buf_size,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :legacy_pack => true,
    :dest => [],
    :source_endpoint => true,  
    :receiver_endpoint => $PARAMS[:chip] == "grayskull"? false : true ,
    :local_receiver => $PARAMS[:chip] == "grayskull"? true : false,
    :local_receiver_tile_clearing => $PARAMS[:chip] == "grayskull"? true : false,   
    :num_msgs => num_input0_data_buf_msgs,
    :msg_size => input0_data_buf_msg_size,
    :next_phase_src_change => true,
    :reg_update_vc => 1
  }

  $binary_intermediate2_streaming_test_graph[phase][input1_data_stream] = {
    :input_index => 1,
    :auto_run => true,
    :buf_addr => input1_data_base,
    :buf_size => input1_data_buf_size,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :legacy_pack => true,
    :dest => [],
    :source_endpoint => true,  
    :receiver_endpoint => $PARAMS[:chip] == "grayskull"? false : true ,
    :local_receiver => $PARAMS[:chip] == "grayskull"? true : false,
    :local_receiver_tile_clearing => $PARAMS[:chip] == "grayskull"? true : false,     
    :num_msgs => num_input1_data_buf_msgs,
    :msg_size => input1_data_buf_msg_size,
    :next_phase_src_change => true,
    :reg_update_vc => 1
  }

  $binary_intermediate2_streaming_test_graph[phase][intermediate0_stream] = {
    :input_index => 2,
    :output_index => 0,
    :auto_run => true,
    :buf_addr => intermediate0_data_base,
    :buf_size => intermediate0_data_buf_size,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :legacy_pack => true,
    :dest => [],
    :num_msgs => num_intermediate0_data_buf_msgs,
    :msg_size => intermediate0_data_buf_msg_size,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 0,
    :vc => 2,
#    :preload_data => (0..(intermediate0_data_buf_size/intermediate0_data_buf_msg_size)-1).map { |i| (start_seed_2 + (0 << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }

  $binary_intermediate2_streaming_test_graph[phase][intermediate1_stream] = {
    :input_index => 3,
    :output_index => 1,
    :auto_run => true,
    :buf_addr => intermediate1_data_base,
    :buf_size => intermediate1_data_buf_size,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :dest => [],
    :num_msgs => num_intermediate1_data_buf_msgs,
    :msg_size => intermediate1_data_buf_msg_size,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 0,
    :vc => 2,
    :preload_data => (0..(intermediate1_data_buf_size/intermediate1_data_buf_msg_size)-1).map { |i| (start_seed_2 + (0 << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true,
    :park_input => true
  }

end

