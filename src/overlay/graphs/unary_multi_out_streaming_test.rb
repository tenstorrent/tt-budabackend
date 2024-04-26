
$unary_multi_out_streaming_test_graph = {}

##

num_msgs = $PARAMS[:num_tiles] ? $PARAMS[:num_tiles] : 8
msg_size = $PARAMS[:tile_size] ? $PARAMS[:tile_size] :(1024 * 2) + 16 + 16

num_input0_data_buf_msgs =  $PARAMS[:num_input0_data_buf_msgs] ?  $PARAMS[:num_input0_data_buf_msgs] : num_msgs;
input0_data_buf_msg_size =  $PARAMS[:input0_data_buf_tile_size] ?  $PARAMS[:input0_data_buf_tile_size] : msg_size;
input0_data_buf_size = num_input0_data_buf_msgs*input0_data_buf_msg_size

num_output0_data_buf_msgs =  $PARAMS[:num_output0_data_buf_msgs] ?  $PARAMS[:num_output0_data_buf_msgs] : num_msgs/2;
output0_data_buf_msg_size =  $PARAMS[:output0_data_buf_tile_size] ?  $PARAMS[:output0_data_buf_tile_size] : msg_size;
output0_data_buf_size = num_output0_data_buf_msgs*output0_data_buf_msg_size

num_output1_data_buf_msgs =  $PARAMS[:num_output1_data_buf_msgs] ?  $PARAMS[:num_output1_data_buf_msgs] : num_msgs/2;
output1_data_buf_msg_size =  $PARAMS[:output1_data_buf_tile_size] ?  $PARAMS[:output1_data_buf_tile_size] : msg_size;
output1_data_buf_size = num_output1_data_buf_msgs*output1_data_buf_msg_size

num_output2_data_buf_msgs =  $PARAMS[:num_output2_data_buf_msgs] ?  $PARAMS[:num_output2_data_buf_msgs] : 0;
output2_data_buf_msg_size =  $PARAMS[:output2_data_buf_tile_size] ?  $PARAMS[:output2_data_buf_tile_size] : msg_size;
output2_data_buf_size = num_output2_data_buf_msgs*output2_data_buf_msg_size

num_output3_data_buf_msgs =  $PARAMS[:num_output3_data_buf_msgs] ?  $PARAMS[:num_output3_data_buf_msgs] : 0;
output3_data_buf_msg_size =  $PARAMS[:output3_data_buf_tile_size] ?  $PARAMS[:output3_data_buf_tile_size] : msg_size;
output3_data_buf_size = num_output3_data_buf_msgs*output3_data_buf_msg_size

data_buffer_space_base = $PARAMS[:data_buffer_space_base] + $extra_tile_header_buffer_size
input0_data_base = $PARAMS[:input0_data_base] ? $PARAMS[:input0_data_base] : data_buffer_space_base;
output0_data_base = $PARAMS[:output0_data_base] ? $PARAMS[:output0_data_base] : (data_buffer_space_base + (256 * 1024));
output1_data_base = $PARAMS[:output1_data_base] ? $PARAMS[:output1_data_base] : (output0_data_base + (128 * 1024));
output2_data_base = $PARAMS[:output2_data_base] ? $PARAMS[:output2_data_base] : (output1_data_base + (128 * 1024));
output3_data_base = $PARAMS[:output3_data_base] ? $PARAMS[:output3_data_base] : (output2_data_base + (128 * 1024));

output0_num_microblocks = $PARAMS[:output0_num_microblocks] ? $PARAMS[:output0_num_microblocks] : num_output0_data_buf_msgs;
output1_num_microblocks = $PARAMS[:output1_num_microblocks] ? $PARAMS[:output1_num_microblocks] : num_output1_data_buf_msgs;
output2_num_microblocks = $PARAMS[:output2_num_microblocks] ? $PARAMS[:output2_num_microblocks] : num_output2_data_buf_msgs;
output3_num_microblocks = $PARAMS[:output3_num_microblocks] ? $PARAMS[:output3_num_microblocks] : num_output3_data_buf_msgs;
output0_double_buffered = false
output1_double_buffered = false
output2_double_buffered = false
output3_double_buffered = false
output0_no_resend = true
output1_no_resend = true
output2_no_resend = true
output3_no_resend = true


input0_data_stream_id = $PARAMS[:input0_data_stream_id] ? $PARAMS[:input0_data_stream_id] : $PARAMS[:chip] == "grayskull" ? 8 : 4
output0_stream_id = $PARAMS[:output0_stream_id] ? $PARAMS[:output0_stream_id] : 24
output1_stream_id = $PARAMS[:output1_stream_id] ? $PARAMS[:output1_stream_id] : 25
output2_stream_id = $PARAMS[:output2_stream_id] ? $PARAMS[:output2_stream_id] : 26
output3_stream_id = $PARAMS[:output3_stream_id] ? $PARAMS[:output3_stream_id] : 27

##

num_phases = 1

chip_id = 0
x = 0
y = 0

input0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_data_stream_id}"
output0_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_stream_id}"
output1_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output1_stream_id}"
output2_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output2_stream_id}"
output3_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output3_stream_id}"

start_seed_0 = 0xa0000000
start_seed_1 = 0xb0000000

for p in 1..num_phases

  phase = :"phase_#{p}"

  $unary_multi_out_streaming_test_graph[phase] = {}

  $unary_multi_out_streaming_test_graph[phase][input0_data_stream] = {
    :input_index => 0,
    :auto_run => true,
    :buf_addr => input0_data_base,
    :buf_size => input0_data_buf_size,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :dest => [],
    :legacy_pack => true,
    :source_endpoint => true,  
    :receiver_endpoint => $PARAMS[:chip] == "grayskull"? false : true ,
    :local_receiver => $PARAMS[:chip] == "grayskull"? true : false,
    :local_receiver_tile_clearing => $PARAMS[:chip] == "grayskull"? true : false,  
    :num_msgs => num_input0_data_buf_msgs,
    :msg_size => input0_data_buf_msg_size,
    :next_phase_src_change => true,
    :reg_update_vc => 1
  }
end

num_phases = output0_double_buffered ? 2*output0_num_microblocks : output0_num_microblocks
buf_addr = output0_data_base

for p in 1..num_phases

  num_msgs_per_phase = num_output0_data_buf_msgs / num_phases
  buf_size = num_msgs_per_phase * output0_data_buf_msg_size
  num_microblocks_in_buf = output0_data_buf_size / buf_size

  phase = :"phase_#{p}"

  if !$unary_multi_out_streaming_test_graph[phase]
    $unary_multi_out_streaming_test_graph[phase] = {}
  end

  resend = output0_no_resend ? false : (((p-1) % num_microblocks_in_buf) == 0 ? false : true)

  $unary_multi_out_streaming_test_graph[phase][output0_stream] = {
    :output_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => buf_size,
    :buf_base_addr => output0_data_base,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :vc => 0,
    :dest => [],
    :num_msgs_in_block => output0_no_resend ? num_msgs_per_phase : num_msgs_per_phase * num_microblocks_in_buf,
    :num_msgs => num_msgs_per_phase,
    :msg_size => output0_data_buf_msg_size,
    :buf_full_size_bytes => num_microblocks_in_buf*buf_size,
    :resend => resend,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 1,
#    :preload_data => (0..(output0_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }
  buf_addr += buf_size
  if (buf_addr >= output0_data_base + output0_data_buf_size)
    buf_addr -= output0_data_buf_size
  end

end

num_phases = output1_double_buffered ? 2*output1_num_microblocks : output1_num_microblocks
buf_addr = output1_data_base

for p in 1..num_phases

  phase = :"phase_#{p}"

  if !$unary_multi_out_streaming_test_graph[phase]
    $unary_multi_out_streaming_test_graph[phase] = {}
  end

  num_msgs_per_phase = num_output1_data_buf_msgs / num_phases
  buf_size = num_msgs_per_phase * output1_data_buf_msg_size
  num_microblocks_in_buf = output1_data_buf_size / buf_size
  resend = output1_no_resend ? false : (((p-1) % num_microblocks_in_buf) == 0 ? false : true)

  $unary_multi_out_streaming_test_graph[phase][output1_stream] = {
    :output_index => 1,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => buf_size,
    :buf_base_addr => output1_data_base,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :vc => 0,
    :dest => [],
    :num_msgs_in_block => output1_no_resend ? num_msgs_per_phase : num_msgs_per_phase * num_microblocks_in_buf,
    :num_msgs => num_msgs_per_phase,
    :msg_size => output1_data_buf_msg_size,
    :buf_full_size_bytes => num_microblocks_in_buf*buf_size,
    :resend => resend,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 1,
#    :preload_data => (0..(output1_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }

  buf_addr += buf_size
  if (buf_addr >= output1_data_base + output1_data_buf_size)
    buf_addr -= output1_data_buf_size
  end
end

num_phases = output2_double_buffered ? 2*output2_num_microblocks : output2_num_microblocks
buf_addr = output2_data_base

for p in 1..num_phases

  phase = :"phase_#{p}"

  if !$unary_multi_out_streaming_test_graph[phase]
    $unary_multi_out_streaming_test_graph[phase] = {}
  end

  num_msgs_per_phase = num_output2_data_buf_msgs / num_phases
  buf_size = num_msgs_per_phase * output2_data_buf_msg_size
  num_microblocks_in_buf = output2_data_buf_size / buf_size
  resend = output2_no_resend ? false : (((p-1) % num_microblocks_in_buf) == 0 ? false : true)

  $unary_multi_out_streaming_test_graph[phase][output2_stream] = {
    :output_index => 2,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => buf_size,
    :buf_base_addr => output2_data_base,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :vc => 0,
    :dest => [],
    :num_msgs_in_block => output2_no_resend ? num_msgs_per_phase : num_msgs_per_phase * num_microblocks_in_buf,
    :num_msgs => num_msgs_per_phase,
    :msg_size => output2_data_buf_msg_size,
    :buf_full_size_bytes => num_microblocks_in_buf*buf_size,
    :resend => resend,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 1,
#    :preload_data => (1..(output2_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }

  buf_addr += buf_size
  if (buf_addr >= output2_data_base + output2_data_buf_size)
    buf_addr -= output2_data_buf_size
  end
end

num_phases = output3_double_buffered ? 2*output3_num_microblocks : output3_num_microblocks
buf_addr = output3_data_base

for p in 1..num_phases

  phase = :"phase_#{p}"

  if !$unary_multi_out_streaming_test_graph[phase]
    $unary_multi_out_streaming_test_graph[phase] = {}
  end

  num_msgs_per_phase = num_output3_data_buf_msgs / num_phases
  buf_size = num_msgs_per_phase * output3_data_buf_msg_size
  num_microblocks_in_buf = output3_data_buf_size / buf_size
  resend = output3_no_resend ? false : (((p-1) % num_microblocks_in_buf) == 0 ? false : true)

  $unary_multi_out_streaming_test_graph[phase][output3_stream] = {
    :output_index => 3,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => buf_size,
    :buf_base_addr => output3_data_base,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :num_msgs_in_block => output3_no_resend ? num_msgs_per_phase : num_msgs_per_phase * num_microblocks_in_buf,
    :num_msgs => num_msgs_per_phase,
    :msg_size => output3_data_buf_msg_size,
    :buf_full_size_bytes => num_microblocks_in_buf*buf_size,
    :resend => resend,
    :vc => 0,
    :dest => [],
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 1,
#    :preload_data => (1..(output3_data_buf_size/msg_size)-1).map { |i| (start_seed_1 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }

  buf_addr += buf_size
  if (buf_addr >= output3_data_base + output3_data_buf_size)
    buf_addr -= output3_data_buf_size
  end
end




