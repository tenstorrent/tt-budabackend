
$nary_streaming_test_graph = {}

##
num_inputs = $PARAMS[:num_inputs] ? $PARAMS[:num_inputs] : 1;
num_msgs = $PARAMS[:num_tiles] ? $PARAMS[:num_tiles] : 8
msg_size = $PARAMS[:tile_size] ? $PARAMS[:tile_size] :(1024 * 2) + 16 + 16

num_output0_data_buf_msgs =  $PARAMS[:num_output0_data_buf_msgs] ?  $PARAMS[:num_output0_data_buf_msgs] : num_msgs;
output0_data_buf_msg_size =  $PARAMS[:output0_data_buf_tile_size] ?  $PARAMS[:output0_data_buf_tile_size] : msg_size;

output0_data_buf_size = $PARAMS[:output0_data_buf_size] ?  ($PARAMS[:output0_data_buf_size]*output0_data_buf_msg_size) : (num_output0_data_buf_msgs*output0_data_buf_msg_size)

output0_num_microblocks = $PARAMS[:output0_num_microblocks] ? $PARAMS[:output0_num_microblocks] : num_output0_data_buf_msgs;
output0_double_buffered = false
output0_no_resend = true

data_buffer_space_base = $PARAMS[:data_buffer_space_base] + $extra_tile_header_buffer_size
input_offset = 0
input_stream_id_offset = $PARAMS[:chip] == "grayskull" ? 8 : 4
chip_id = 0
x = 0
y = 0
num_input_data_buf_msgs=[]
input_data_buf_msg_size=[]
input_data_buf_size=[]
input_data_base=[]
input_stream_id=[]
input_data_stream=[]
for i_tmp in 1..num_inputs
  i = i_tmp-1
  num_input_data_buf_msgs[i] =  $PARAMS[:"num_input#{i}_data_buf_msgs"] ?  $PARAMS[:"num_input#{i}_data_buf_msgs"] : num_msgs;
  input_data_buf_msg_size[i] =  $PARAMS[:"input#{i}_data_buf_tile_size"] ?  $PARAMS[:"input#{i}_data_buf_tile_size"] : msg_size;
  input_data_buf_size[i] = $PARAMS[:"input#{i}_data_buf_size"] ?  ($PARAMS[:"input#{i}_data_buf_size"]*input_data_buf_msg_size[i]) :  num_input_data_buf_msgs[i]*input_data_buf_msg_size[i]
  input_data_base[i] = $PARAMS[:"input#{i}_data_base"] ? $PARAMS[:"input#{i}_data_base"] : data_buffer_space_base + input_offset;
  input_stream_id[i] = $PARAMS[:"input#{i}_data_stream_id"] ? $PARAMS[:"input#{i}_data_stream_id"] : input_stream_id_offset;
  input_data_stream[i] = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{input_stream_id[i]}"

  input_offset = input_offset + (128 * 1024)
  input_stream_id_offset = input_stream_id_offset + 1
  if $PARAMS[:chip] != "grayskull" && input_stream_id_offset == 6
    input_stream_id_offset = 10
  end
end
output0_data_base = $PARAMS[:output0_data_base] ? $PARAMS[:output0_data_base] : (data_buffer_space_base + input_offset);
output0_data_stream_id = $PARAMS[:output0_data_stream_id] ? $PARAMS[:output0_data_stream_id] : 24

##

num_phases = 1


output0_data_stream = :"chip_#{chip_id}__y_#{y}__x_#{x}__stream_id_#{output0_data_stream_id}"

start_seed_0 = 0xa0000000
start_seed_1 = 0xb0000000
start_seed_2 = 0xc0000000

for p in 1..num_phases

  phase = :"phase_#{p}"

  $nary_streaming_test_graph[phase] = {}

  for i_tmp in 1..num_inputs
    i = i_tmp-1
    buf_addr = input_data_base[i]
    $nary_streaming_test_graph[phase][input_data_stream[i]] = {
        :input_index => i,
        :auto_run => true,
        :buf_addr => buf_addr,
        :buf_size => input_data_buf_size[i],
        :msg_info_buf_addr => $msg_info_buf_addr,
        :dest => [],
        :legacy_pack => true,
        :source_endpoint => true,  
        :local_receiver => true,
        :local_receiver_tile_clearing => true,   
        :num_msgs => num_input_data_buf_msgs[i],
        :msg_size => input_data_buf_msg_size[i],
        :next_phase_src_change => true,
        :reg_update_vc => 1
    }
  end
end

num_phases = output0_double_buffered ? 2*output0_num_microblocks : output0_num_microblocks
num_msgs_per_phase = num_output0_data_buf_msgs / num_phases
buf_addr = output0_data_base
buf_size = num_msgs_per_phase * output0_data_buf_msg_size
num_microblocks_in_buf = output0_data_buf_size / buf_size

for p in 1..num_phases

  phase = :"phase_#{p}"

  if !$nary_streaming_test_graph[phase]
    $nary_streaming_test_graph[phase] = {}
  end

  resend = output0_no_resend ? false : (((p-1) % num_microblocks_in_buf) == 0 ? false : true)

  $nary_streaming_test_graph[phase][output0_data_stream] = {
    :output_index => 0,
    :auto_run => true,
    :buf_addr => buf_addr,
    :buf_size => buf_size,
    :buf_base_addr => output0_data_base,
    :msg_info_buf_addr => $msg_info_buf_addr,
    :dest => [],
    :num_msgs_in_block => output0_no_resend ? num_msgs_per_phase : num_msgs_per_phase * num_microblocks_in_buf,
    :num_msgs => num_msgs_per_phase,
    :msg_size => output0_data_buf_msg_size,
    :buf_full_size_bytes => num_microblocks_in_buf*buf_size,
    :source_endpoint => true,
    :receiver_endpoint => true,
    :outgoing_data_noc => 0,
    :vc => 2,
#    :preload_data => (0..(output0_data_buf_size/msg_size)-1).map { |i| (start_seed_2 + (i << 16)) },
    :resend => resend,
    :dest_data_buf_no_flow_ctrl => false,
    :next_phase_src_change => true
  }

  buf_addr += buf_size
  if (buf_addr >= output0_data_base + output0_data_buf_size)
    buf_addr -= output0_data_buf_size
  end

end


