
$unary_multi_out_test_graph = {}

##

num_msgs = $PARAMS[:num_tiles] ? $PARAMS[:num_tiles] : 8
msg_size = $PARAMS[:tile_size] ? $PARAMS[:tile_size] :(1024 * 2) + 16 + 16

num_input0_park_buf_msgs =  $PARAMS[:num_input0_park_buf_msgs] ?  $PARAMS[:num_input0_park_buf_msgs] : num_msgs;
input0_park_buf_msg_size =  $PARAMS[:input0_park_buf_tile_size] ?  $PARAMS[:input0_park_buf_tile_size] : msg_size;
input0_park_buf_size = num_input0_park_buf_msgs*input0_park_buf_msg_size

num_input0_data_buf_msgs =  $PARAMS[:num_input0_data_buf_msgs] ?  $PARAMS[:num_input0_data_buf_msgs] : num_msgs;
input0_data_buf_msg_size =  $PARAMS[:input0_data_buf_tile_size] ?  $PARAMS[:input0_data_buf_tile_size] : msg_size;
input0_data_buf_size = num_input0_data_buf_msgs*input0_data_buf_msg_size

num_output0_data_buf_msgs =  $PARAMS[:num_output0_data_buf_msgs] ?  $PARAMS[:num_output0_data_buf_msgs] : num_msgs/2;
output0_data_buf_msg_size =  $PARAMS[:output0_data_buf_tile_size] ?  $PARAMS[:output0_data_buf_tile_size] : msg_size;
output0_data_buf_size = num_output0_data_buf_msgs*output0_data_buf_msg_size

num_output0_park_buf_msgs =  $PARAMS[:num_output0_park_buf_msgs] ?  $PARAMS[:num_output0_park_buf_msgs] : num_msgs/2;
output0_park_buf_msg_size =  $PARAMS[:output0_park_buf_tile_size] ?  $PARAMS[:output0_park_buf_tile_size] : msg_size;
output0_park_buf_size = num_output0_park_buf_msgs*output0_park_buf_msg_size

num_output1_data_buf_msgs =  $PARAMS[:num_output1_data_buf_msgs] ?  $PARAMS[:num_output1_data_buf_msgs] : num_msgs/2;
output1_data_buf_msg_size =  $PARAMS[:output1_data_buf_tile_size] ?  $PARAMS[:output1_data_buf_tile_size] : msg_size;
output1_data_buf_size = num_output1_data_buf_msgs*output1_data_buf_msg_size

num_output1_park_buf_msgs =  $PARAMS[:num_output1_park_buf_msgs] ?  $PARAMS[:num_output1_park_buf_msgs] : num_msgs/2;
output1_park_buf_msg_size =  $PARAMS[:output1_park_buf_tile_size] ?  $PARAMS[:output1_park_buf_tile_size] : msg_size;
output1_park_buf_size = num_output1_park_buf_msgs*output1_park_buf_msg_size

num_output2_data_buf_msgs =  $PARAMS[:num_output2_data_buf_msgs] ?  $PARAMS[:num_output2_data_buf_msgs] : 0;
output2_data_buf_msg_size =  $PARAMS[:output2_data_buf_tile_size] ?  $PARAMS[:output2_data_buf_tile_size] : msg_size;
output2_data_buf_size = num_output2_data_buf_msgs*output2_data_buf_msg_size

num_output2_park_buf_msgs =  $PARAMS[:num_output2_park_buf_msgs] ?  $PARAMS[:num_output2_park_buf_msgs] : 0;
output2_park_buf_msg_size =  $PARAMS[:output2_park_buf_tile_size] ?  $PARAMS[:output2_park_buf_tile_size] : msg_size;
output2_park_buf_size = num_output2_park_buf_msgs*output2_park_buf_msg_size

num_output3_data_buf_msgs =  $PARAMS[:num_output3_data_buf_msgs] ?  $PARAMS[:num_output3_data_buf_msgs] : 0;
output3_data_buf_msg_size =  $PARAMS[:output3_data_buf_tile_size] ?  $PARAMS[:output3_data_buf_tile_size] : msg_size;
output3_data_buf_size = num_output3_data_buf_msgs*output3_data_buf_msg_size

num_output3_park_buf_msgs =  $PARAMS[:num_output3_park_buf_msgs] ?  $PARAMS[:num_output3_park_buf_msgs] : 0;
output3_park_buf_msg_size =  $PARAMS[:output3_park_buf_tile_size] ?  $PARAMS[:output3_park_buf_tile_size] : msg_size;
output3_park_buf_size = num_output3_park_buf_msgs*output3_park_buf_msg_size

data_buffer_space_base = $PARAMS[:data_buffer_space_base] + $extra_tile_header_buffer_size
input0_data_base = $PARAMS[:input0_data_base] ? $PARAMS[:input0_data_base] : data_buffer_space_base;
test_data_offset = $PARAMS[:test_data_offset] ? $PARAMS[:test_data_offset] : (64 * 1024)
output0_data_base = $PARAMS[:output0_data_base] ? $PARAMS[:output0_data_base] : (data_buffer_space_base + (512 * 1024));
output1_data_base = $PARAMS[:output1_data_base] ? $PARAMS[:output1_data_base] : (output0_data_base + (128 * 1024));
output2_data_base = $PARAMS[:output2_data_base] ? $PARAMS[:output2_data_base] : (output1_data_base + (128 * 1024));
output3_data_base = $PARAMS[:output3_data_base] ? $PARAMS[:output3_data_base] : (output2_data_base + (128 * 1024));


input0_park_stream_id = $PARAMS[:input0_park_stream_id] ? $PARAMS[:input0_park_stream_id] : 1
output0_park_stream_id = $PARAMS[:output0_park_stream_id] ? $PARAMS[:output0_park_stream_id] : 2
output1_park_stream_id = $PARAMS[:output1_park_stream_id] ? $PARAMS[:output1_park_stream_id] : 3
output2_park_stream_id = $PARAMS[:output2_park_stream_id] ? $PARAMS[:output2_park_stream_id] : 6
output3_park_stream_id = $PARAMS[:output3_park_stream_id] ? $PARAMS[:output3_park_stream_id] : 7

input0_data_stream_id = $PARAMS[:input0_data_stream_id] ? $PARAMS[:input0_data_stream_id] : $PARAMS[:chip] == "grayskull" ? 8 : 4
output0_stream_id = $PARAMS[:output0_stream_id] ? $PARAMS[:output0_stream_id] : 24
output1_stream_id = $PARAMS[:output1_stream_id] ? $PARAMS[:output1_stream_id] : 25
output2_stream_id = $PARAMS[:output2_stream_id] ? $PARAMS[:output2_stream_id] : 26
output3_stream_id = $PARAMS[:output3_stream_id] ? $PARAMS[:output3_stream_id] : 27

##

num_phases = 1

msg_info_buf_size = num_msgs*16

chip_id = 0
x = 0
y = 0

input0_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_park_stream_id}"
input0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_data_stream_id}"
output0_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_stream_id}"
output0_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_park_stream_id}"
output1_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output1_stream_id}"
output1_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output1_park_stream_id}"
output2_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output2_stream_id}"
output2_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output2_park_stream_id}"
output3_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output3_stream_id}"
output3_park_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output3_park_stream_id}"

start_seed_0 = 0xa0000000
start_seed_1 = 0xb0000000

for p in 1..num_phases

  phase = :"phase_#{p}"

  $unary_multi_out_test_graph[phase] = {}

  buf_addr = input0_data_base

  $unary_multi_out_test_graph[phase][input0_data_stream] = {
    :input_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => input0_data_buf_size,
    :buf_base_addr => input0_data_base,
    :msg_info_buf_addr => buf_addr + input0_data_buf_size,
    :dest => [],
    :src => [input0_park_stream],
    :remote_source => true,  
    :receiver_endpoint => true,  
    :num_msgs => num_input0_data_buf_msgs,
    :msg_size => input0_data_buf_msg_size,
    :next_phase_src_change => true,
    :data_buf_no_flow_ctrl => true,
    :reg_update_vc => 1
  }

  buf_addr += test_data_offset

  $unary_multi_out_test_graph[phase][input0_park_stream] = {
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => input0_park_buf_size,
    :buf_base_addr => input0_data_base + test_data_offset,
    :msg_info_buf_addr => buf_addr + input0_park_buf_size,
    :source_endpoint => true,   
    :remote_receiver => true,
    :dest => [input0_data_stream],
    :dest_data_buf_no_flow_ctrl => true,
    :num_msgs => num_input0_park_buf_msgs,
    :msg_size => input0_park_buf_msg_size,
#    :preload_data => (0..num_msgs-1).map { |i| (start_seed_0 + (i << 16)) },
    :outgoing_data_noc => 0,
    :vc => 0,
    :group_priority => 0,
    :park_input => true
  }  

  buf_addr = output0_data_base

  $unary_multi_out_test_graph[phase][output0_stream] = {
    :output_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output0_data_buf_size,
    :buf_base_addr => output0_data_base,
    :msg_info_buf_addr => buf_addr + output0_data_buf_size,
    :vc => 0,
    :dest => [output0_park_stream],
    :num_msgs => num_output0_data_buf_msgs,
    :msg_size => output0_data_buf_msg_size,
    :legacy_pack => true,
    :source_endpoint => true,
    :remote_receiver => true,
    :outgoing_data_noc => 1,
#    :preload_data => (0..(output0_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => true,
    :next_phase_src_change => true
  }

  buf_addr += test_data_offset
  
  $unary_multi_out_test_graph[phase][output0_park_stream] = {
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output0_park_buf_size,
    :buf_base_addr => output0_data_base + test_data_offset,
    :msg_info_buf_addr => buf_addr + output0_park_buf_size,
    :dest => [],
    :src => [output0_stream],
    :remote_source => true,   
    :num_msgs => num_output0_park_buf_msgs,
    :msg_size => output0_park_buf_msg_size,
    :next_phase_src_change => true,
    :data_buf_no_flow_ctrl => true,
    :reg_update_vc => 1
  }

  buf_addr = output1_data_base

  $unary_multi_out_test_graph[phase][output1_stream] = {
    :output_index => 1,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output1_data_buf_size,
    :buf_base_addr => output1_data_base,
    :msg_info_buf_addr => buf_addr + output1_data_buf_size,
    :vc => 0,
    :dest => [output1_park_stream],
    :num_msgs => num_output1_data_buf_msgs,
    :msg_size => output1_data_buf_msg_size,
    :legacy_pack => true,
    :source_endpoint => true,
    :remote_receiver => true,
    :outgoing_data_noc => 1,
#    :preload_data => (0..(output1_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => true,
    :next_phase_src_change => true
  }

  buf_addr += test_data_offset
  
  $unary_multi_out_test_graph[phase][output1_park_stream] = {
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output1_park_buf_size,
    :buf_base_addr => output1_data_base + test_data_offset,
    :msg_info_buf_addr => buf_addr + output1_park_buf_size,
    :dest => [],
    :src => [output1_stream],
    :remote_source => true,   
    :num_msgs => num_output1_park_buf_msgs,
    :msg_size => output1_park_buf_msg_size,
    :next_phase_src_change => true,
    :data_buf_no_flow_ctrl => true,
    :reg_update_vc => 1
  }

  buf_addr = output2_data_base
  
  $unary_multi_out_test_graph[phase][output2_stream] = {
    :output_index => 2,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output2_data_buf_size,
    :buf_base_addr => output2_data_base,
    :msg_info_buf_addr => buf_addr + output2_data_buf_size,
    :vc => 0,
    :dest => [output2_park_stream],
    :num_msgs => num_output2_data_buf_msgs,
    :msg_size => output2_data_buf_msg_size,
    :legacy_pack => true,
    :source_endpoint => true,
    :remote_receiver => true,
    :outgoing_data_noc => 1,
#    :preload_data => (1..(output2_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => true,
    :next_phase_src_change => true
  }

  buf_addr += test_data_offset
  
  $unary_multi_out_test_graph[phase][output2_park_stream] = {
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output2_park_buf_size,
    :buf_base_addr => output2_data_base + test_data_offset,
    :msg_info_buf_addr => buf_addr + output2_park_buf_size,
    :dest => [],
    :src => [output2_stream],
    :remote_source => true,   
    :num_msgs => num_output2_park_buf_msgs,
    :msg_size => output2_park_buf_msg_size,
    :next_phase_src_change => true,
    :data_buf_no_flow_ctrl => true,
    :reg_update_vc => 1
  }

  buf_addr = output3_data_base

  $unary_multi_out_test_graph[phase][output3_stream] = {
    :output_index => 3,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output3_data_buf_size,
    :buf_base_addr => output3_data_base,
    :msg_info_buf_addr => buf_addr + output3_data_buf_size,
    :vc => 0,
    :dest => [output3_park_stream],
    :num_msgs => num_output3_data_buf_msgs,
    :msg_size => output3_data_buf_msg_size,
    :legacy_pack => true,
    :source_endpoint => true,
    :remote_receiver => true,
    :outgoing_data_noc => 1,
#    :preload_data => (1..(output3_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => true,
    :next_phase_src_change => true
  }

  buf_addr += test_data_offset
  
  $unary_multi_out_test_graph[phase][output3_park_stream] = {
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output3_park_buf_size,
    :buf_base_addr => output3_data_base + test_data_offset,
    :msg_info_buf_addr => buf_addr + output3_park_buf_size,
    :dest => [],
    :src => [output3_stream],
    :remote_source => true,   
    :num_msgs => num_output3_park_buf_msgs,
    :msg_size => output3_park_buf_msg_size,
    :next_phase_src_change => true,
    :data_buf_no_flow_ctrl => true,
    :reg_update_vc => 1
  }
end



