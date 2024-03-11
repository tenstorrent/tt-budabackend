
$unary_untilize_streaming_test_graph = {}

##

num_msgs = $PARAMS[:num_tiles] ? $PARAMS[:num_tiles] : 8
msg_size = $PARAMS[:tile_size] ? $PARAMS[:tile_size] :(1024 * 2) + 16 + 16

num_input0_data_buf_msgs =  $PARAMS[:num_input0_data_buf_msgs] ?  $PARAMS[:num_input0_data_buf_msgs] : num_msgs;
input0_data_buf_msg_size =  $PARAMS[:input0_data_buf_tile_size] ?  $PARAMS[:input0_data_buf_tile_size] : msg_size;
input0_data_buf_size = num_input0_data_buf_msgs*input0_data_buf_msg_size

num_output0_data_buf_msgs =  $PARAMS[:num_output0_data_buf_msgs] ?  $PARAMS[:num_output0_data_buf_msgs] : num_msgs;
num_output0_park_buf_msgs =  $PARAMS[:num_output0_park_buf_msgs] ?  $PARAMS[:num_output0_park_buf_msgs] : num_msgs;
output0_data_buf_msg_size =  $PARAMS[:output0_data_buf_tile_size] ?  $PARAMS[:output0_data_buf_tile_size] : msg_size;

output0_data_buf_size = $PARAMS[:output0_data_buf_size] ?  ($PARAMS[:output0_data_buf_size]*output0_data_buf_msg_size) : (num_output0_data_buf_msgs*output0_data_buf_msg_size)


data_buffer_space_base = $PARAMS[:data_buffer_space_base] + $extra_tile_header_buffer_size
input0_data_base = $PARAMS[:input0_data_base] ? $PARAMS[:input0_data_base] : data_buffer_space_base;
output0_data_base = $PARAMS[:output0_data_base] ? $PARAMS[:output0_data_base] : (data_buffer_space_base + (256 * 1024));

input0_data_stream_id = $PARAMS[:input0_data_stream_id] ? $PARAMS[:input0_data_stream_id] : $PARAMS[:chip] == "grayskull" ? 8 : 4

output0_data_stream_id = $PARAMS[:output0_data_stream_id] ? $PARAMS[:output0_data_stream_id] : 24

##

num_phases = 1

msg_info_buf_size = num_msgs*16

chip_id = 0
x = 0
y = 0

input0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input0_data_stream_id}"
output0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_data_stream_id}"

start_seed_0 = 0xa0000000
start_seed_1 = 0xb0000000

for p in 1..num_phases

  phase = :"phase_#{p}"

  $unary_untilize_streaming_test_graph[phase] = {}

  buf_addr = input0_data_base
  $unary_untilize_streaming_test_graph[phase][input0_data_stream] = {
    :input_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => input0_data_buf_size,
    :buf_base_addr => input0_data_base,
    :msg_info_buf_addr => buf_addr + input0_data_buf_size,
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

  buf_addr = output0_data_base

  $unary_untilize_streaming_test_graph[phase][output0_data_stream] = {
    :output_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => output0_data_buf_size,
    :buf_base_addr => output0_data_base,
    :msg_info_buf_addr => buf_addr + output0_data_buf_size,
    :vc => 0,
    :dest => [],
    :num_msgs => num_output0_data_buf_msgs,
    :msg_size => output0_data_buf_msg_size,
    :legacy_pack => true,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 1,
#    :preload_data => (0..(output0_data_buf_size/msg_size)-1).map { |i| (start_seed_2 + (i << 16)) },
    :dest_data_buf_no_flow_ctrl => true,
    :next_phase_src_change => true,
   # :next_phase_dest_change => true,
    :park_output => true,
    :moves_raw_data => true
  }

end



