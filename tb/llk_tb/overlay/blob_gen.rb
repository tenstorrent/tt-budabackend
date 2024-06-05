# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

require "pp"
require "fileutils"
require "optparse"
require "set"
require "yaml"
require 'erb'

#require "stackprof"
#StackProf.run(mode: :cpu, out: 'stackprof-output.dump') do

$PARAMS = {
  :chip => "grayskull",
  :noc_version => 1,
  :noc_x_size => 1,
  :noc_y_size => 1,
  :noc_x_logical_size => "",
  :noc_y_logical_size => "",
  :blob_gen_root => File.dirname(__FILE__),
  :blob_out_dir => File.dirname(__FILE__) + "/out",
  :root => nil,
  :graph_name => "unary_test",
  :graph_input_file => nil,
  :graph_yaml => 0,
  :overlay_blob_size => (64 * 1024) - 128,
  :overlay_blob_size_eth => (32 * 1024) - 128,
  :blob_section_start => (140 * 1024) + 128,
  :data_buffer_space_base => (204 * 1024),
  :tensix_mem_size => (1024 * 1024),
  :blob_section_start_eth => 0,
  :data_buffer_space_base_eth => 0,
  :eth_cores => "",
  :worker_cores => "",
  :noc_translation_id_enabled => 0,
  :next_epoch_dram_addr => 0,
  :nrisc_no_fw_load => 0,
  :verbose => 0,
  :verbose_debug => 0, # Note that setting this to 1 will provide full debug info, but won't produce working hex!
  :chip_ids => "",
  :alignment => 32,
}

print("This script is DEPRECATED. Please use src/blobgen2.")
print("ruby ")
print($PROGRAM_NAME)
print(" ")
for i in 0..ARGV.length-1
  print(ARGV[i])
  print(" ")
  if ARGV[i].start_with? '--'
    key = "#{ARGV[i]}"[2..-1]
    val = "#{ARGV[i+1]}"
    int_val = Integer(val) rescue false
    if int_val
      $PARAMS[:"#{key}"] = int_val
    else
      $PARAMS[:"#{key}"] = val
    end
  end
end
print("\n")

require "#{$PARAMS[:blob_gen_root]}/graphs/graph_list.rb"
require "#{$PARAMS[:blob_gen_root]}/stream_regs.rb"

# Verbose output.
$verbose = $PARAMS[:verbose]
# Full debug will allow you to see each step of output with string markings in hex arrays, but won't produce 
# working hex files for cores!
$verbose_debug = $PARAMS[:verbose_debug]

$phase_mask = 0xFFFFFFFF
$phase_shift = 32
$wrapped_phase_mask = 0x7FFF
$epoch_shift = 15
$epoch_max = 31
$dram_io_is_scatter_loop = 0x8000000000000000

# unused_data_buffers_space_bytes from pipegen2_constants.h
$unused_buf_space_bytes = 64

# EPOCH_MAX_INPUTS from epoch.h
$epoch_max_inputs = 24
# EPOCH_MAX_OUTPUTS_ETH from epoch.h
$epoch_max_outputs = 24
# EPOCH_MAX_OUTPUT_FORKS from epoch.h
$epoch_max_output_forks = 16
# EPOCH_MAX_NUM_TILE_SIZES from epoch.h
$epoch_max_num_tile_sizes = 8
# PERF_NUM_THREADS from l1_address_map.h
$perf_num_threads = 5
# NOC_NUM_STREAMS from noc_overlay_parameters.h
$noc_num_streams = 64

# general_max_num_tiles_per_phase from pipegen2_constants.h
$max_msgs_per_phase = 2048 
# tile_header_size_bytes from pipegen2_constants.h
$tile_header_size_bytes = 16 
$msg_info_buf_size = $max_msgs_per_phase * $tile_header_size_bytes

# Helper function which aligns value up to be divisible by --alignment parameter.
def AlignStructSizeInBytes(length_in_bytes)
  return (length_in_bytes + $PARAMS[:alignment] - 1) / $PARAMS[:alignment] * $PARAMS[:alignment]
end

# Unaligned epoch_t struct size. One such struct exists per core.
$epoch_t_struct_size = 454 + 8 * $epoch_max_num_tile_sizes + 4 * ($epoch_max_inputs + $epoch_max_outputs + $noc_num_streams) + 18 * $perf_num_threads
# Aligned epoch_t struct size.
$epoch_t_struct_padded_size = AlignStructSizeInBytes($epoch_t_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$epoch_t_struct_padding = ($epoch_t_struct_padded_size - $epoch_t_struct_size) / 4

# Unaligned epoch_stream_info_t struct size. One such struct exist per used stream on core.
$epoch_stream_info_t_struct_size = 144 + 1 * $epoch_max_output_forks
# Analigned epoch_stream_info_t struct size.
$epoch_stream_info_t_struct_padded_size = AlignStructSizeInBytes($epoch_stream_info_t_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$epoch_stream_info_t_struct_padding = ($epoch_stream_info_t_struct_padded_size - $epoch_stream_info_t_struct_size) / 4

# Unaligned scatter_pipe_state_t struct size. Exists only if `is_scatter_pack` and `scatter_order_size > 1`.
$scatter_pipe_state_t_struct_size = 16
# Unaligned scatter_pipe_blob_t struct size.
$scatter_pipe_blob_t_struct_size = 8
# Note that we don't need to align scatter_pipe_state_t and scatter_pipe_blob_t structs, 
# the whole array is instead padded to achieve alignment.
def GetScatterPipeStateAndBlobListPaddedSize(blob_scatter)
  length = 0

  if blob_scatter.size() > 0
    blob_scatter.each do |scatter_idx, blob_scatter_elem|
      length = length + $scatter_pipe_state_t_struct_size
      for unroll_idx in 0..(blob_scatter_elem[:offsets].length()-1)
        length = length + $scatter_pipe_blob_t_struct_size
      end
    end
  end

  return AlignStructSizeInBytes(length)
end

# Unaligned epoch_stream_dram_io_info_t struct size. This and structs below exist only if stream has NCRISC config assigned.
$epoch_stream_dram_io_info_t_struct_size = 32
# Aligned epoch_stream_dram_io_info_t struct size.
$epoch_stream_dram_io_info_t_struct_padded_size = AlignStructSizeInBytes($epoch_stream_dram_io_info_t_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$epoch_stream_dram_io_info_t_struct_padding = ($epoch_stream_dram_io_info_t_struct_padded_size - $epoch_stream_dram_io_info_t_struct_size) / 4

# Unaligned dram_io_state_t::dram_to_l1 struct size. Exists one per NCRSIC config.
$dram_io_state_t_dram_to_l1_struct_size = 32
# Aligned dram_io_state_t::dram_to_l1 struct size.
$dram_io_state_t_dram_to_l1_struct_padded_size = AlignStructSizeInBytes($dram_io_state_t_dram_to_l1_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$dram_io_state_t_dram_to_l1_struct_padding = ($dram_io_state_t_dram_to_l1_struct_padded_size - $dram_io_state_t_dram_to_l1_struct_size) / 4
# Unaligned dram_io_state_t::l1_to_dram struct size.
$dram_io_state_t_l1_to_dram_struct_size = 32
# Aligned dram_io_state_t::l1_to_dram struct size.
$dram_io_state_t_l1_to_dram_struct_padded_size = AlignStructSizeInBytes($dram_io_state_t_l1_to_dram_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$dram_io_state_t_l1_to_dram_struct_padding = ($dram_io_state_t_l1_to_dram_struct_padded_size - $dram_io_state_t_l1_to_dram_struct_size) / 4
# Unaligned rest of dram_io_state_t struct size.
$dram_io_state_t_rest_of_struct_size = 32
# Aligned rest of dram_io_state_t struct size.
$dram_io_state_t_rest_of_struct_padded_size = AlignStructSizeInBytes($dram_io_state_t_rest_of_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$dram_io_state_t_rest_of_struct_padding = ($dram_io_state_t_rest_of_struct_padded_size - $dram_io_state_t_rest_of_struct_size) / 4
# Aligned dram_io_state_t struct size.
$dram_io_state_t_struct_padded_size = $dram_io_state_t_dram_to_l1_struct_padded_size + $dram_io_state_t_l1_to_dram_struct_padded_size + $dram_io_state_t_rest_of_struct_padded_size

# Unaligned dram_io_scatter_state_t struct size. Exists only if NCRISC config has dram_scatter_offsets.
$dram_io_scatter_state_t_struct_size = 32
# Aligned dram_io_scatter_state_t struct size.
$dram_io_scatter_state_t_struct_padded_size = AlignStructSizeInBytes($dram_io_scatter_state_t_struct_size)
# Number of zeros to pad to achieve alignment. One zero is 4B.
$dram_io_scatter_state_t_struct_padding = ($dram_io_scatter_state_t_struct_padded_size - $dram_io_scatter_state_t_struct_size) / 4
# dram_io_scatter_state_t.scatter_offsets is tt_uint64_t tt_l1_ptr * , thus 8B.
$dram_io_scatter_offsets_size = 8

# Note that we don't need to align each dram_scatter_offset item,
# the whole array is instead padded to achieve alignment.
def GetDramScatterOffsetsListPaddedSize(dram_scatter_offsets_length)
  return AlignStructSizeInBytes(dram_scatter_offsets_length * $dram_io_scatter_offsets_size)
end

# These structs hold maps of tile_size to tile_header_buf_addr
# For non ethernet cores, maps are generated per chip.
# They will hold an entry for each tile size on all cores and all stream phases on this chip.
$tile_size_tile_header_buf_addr_map_per_chip = {}
# For ethernet cores, maps are generated per chip per core.
# They will hold an entry for each tile size for all stream phases on this core.
$tile_size_tile_header_buf_addr_map_eth_per_core = {}


ETH_CORES = Set.new($PARAMS[:eth_cores].split(','))
WORKER_CORE_OVERRIDE_ENABLED = $PARAMS[:worker_cores] != ""
WORKER_CORES = Set.new($PARAMS[:worker_cores].split(','))
chip_ids  = Set.new(String($PARAMS[:chip_ids]).split(',').map(&:to_i))

grid_sizes_x = Array.new(String($PARAMS[:noc_x_logical_size]).split(','))
grid_sizes_y = Array.new(String($PARAMS[:noc_y_logical_size]).split(','))

chip_ids_to_grid_size_x = Hash.new
chip_ids_to_grid_size_y = Hash.new

def SegmentsOverlap(segment1_start, segment1_end, segment2_start, segment2_end) 
  if (segment1_end - segment1_start == 0) or (segment2_end - segment2_start == 0)
    # If size of any segment is 0, overlaping doesn't make sense.
    return false
  end
  
  if (segment1_end > segment2_start) and (segment2_end > segment1_start)
    return true
  else
    return false
  end
end

def ValidateDataAndTileHeaderBuffersAreNonOverlapping(phase_info)
  phase_info.each do |yx_label, streams|
    chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }

    ## Make sure we don't overlap with any of the tile headers on the core
    msg_info_buf_sizes = Set[]
    streams.each do |stream_id, phases|
      if streams[stream_id][:moves_raw_data]
        next
      end
      phases.each do |phase, phase_info|
        msg_info_start_addr = phase_info[:msg_info_buf_addr]
        msg_info_end_addr = msg_info_start_addr + $msg_info_buf_size
      
        msg_info_buf_sizes.add({:start => msg_info_start_addr, :end => msg_info_end_addr})
      end
    end

    msg_info_buf_sizes.each do |msg_info_buf_start_end|
      streams.each do |stream_id, phases|
        if streams[stream_id][:moves_raw_data]
          next
        end

        msg_info_buf_start = msg_info_buf_start_end[:start]
        msg_info_buf_end = msg_info_buf_start_end[:end]

        phases.each do |phase, phase_info|
          data_buf_start_addr = phase_info[:buf_base_addr]
          data_buf_end_addr = data_buf_start_addr + phase_info[:buf_full_size_bytes]
          
          if SegmentsOverlap(msg_info_buf_start, msg_info_buf_end, data_buf_start_addr, data_buf_end_addr)
            abort("Error! Data buffer and tile header buffer overlap! chip_id=#{chip_id}, y=#{y}, x=#{x}, stream_id=#{stream_id}, phase=#{phase}, address(start)=#{data_buf_start_addr}, address(end)=#{data_buf_end_addr} overlaps with a tile header buffer address(start)=#{msg_info_buf_start}, address(end)=#{msg_info_buf_end}. This likely indicates the number of tiles sizes for the core was miscounted in pipegen.\n")
          end

        end
      end
    end
  end
end

def IsEthernetYX(y, x) 
  # Might need to move away from string hashing if it becomes a perf issue
  return ETH_CORES.include?("#{y}-#{x}")
end

def IsWorkerYX(y, x) 
  # Might need to move away from string hashing if it becomes a perf issue
  return WORKER_CORES.include?("#{y}-#{x}")
end

def GetBlobHexFileName(graph_name, chip_id, y, x)
  return "#{$PARAMS[:blob_out_dir]}/#{graph_name}_#{chip_id}_#{y}_#{x}.hex"
end

def NextHigherWorkerXWraparound(chip_id, y, x, chip_ids_to_grid_size_x)
  x = (x + 1) % chip_ids_to_grid_size_x[chip_id]
  while (IsEthernetYX(y, x))
    x = (x + 1) % chip_ids_to_grid_size_x[chip_id]
  end
  return x
end

def NextHigherWorkerYWraparound(chip_id, y, x, chip_ids_to_grid_size_y)
  y = (y + 1) % chip_ids_to_grid_size_y[chip_id]
  while (IsEthernetYX(y, x))
    y = (y + 1) % chip_ids_to_grid_size_y[chip_id]
  end
  return y

end

stream_reg_index_width = 8
stream_reg_cfg_data_width = 24

def GetPrevPhase(phase_info, sorted_phase_nums, num_phases, cur_phase, chip_id, x, y, stream_id)
  if (phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"] && phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id])
    for p in (num_phases-1).downto(0) do
      prev_phase = sorted_phase_nums[p]
      if (prev_phase < cur_phase)
        return [prev_phase, phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id][prev_phase]]
      end
    end
  end
  return [nil, nil]
end

def GetNextPhase(phase_info, sorted_phase_nums, num_phases, cur_phase, chip_id, x, y, stream_id)
  if (phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"] && phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id])
    for p in 0..num_phases-1 do
      next_phase = sorted_phase_nums[p]
      if (next_phase > cur_phase)
        return [next_phase, phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id][next_phase]]
      end
    end
  end
  return [nil, nil]
end

def GetLastActivePhase(phase_info, sorted_phase_nums, num_phases, chip_id, x, y, stream_id)
  if sorted_phase_nums.length() == 0
    abort("Error! No last phase found for x=#{x}, y=#{y}, stream_id=#{stream_id}")
  end
  last_phase = sorted_phase_nums[num_phases-1]
  return [last_phase, phase_info[:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id][last_phase]]
end

def GetMaxPhaseNum(autocfg)
  if (!autocfg[:max_phase_num])
	  max_phase_num = -1
	  autocfg.each do |phase_label, dont_care|
		  if (!phase_label.to_s.start_with?("phase_"))
		    next
		  end

		  phase = phase_label.to_s.scan(/phase_(\d\d*)/).last.map {|str| str.to_i }.last

		  if (phase > max_phase_num)
		    max_phase_num = phase
		  end
	  end
	  autocfg[:max_phase_num] = max_phase_num
  end

  return autocfg[:max_phase_num]
end

def GetEpochNum(autocfg)
  if (!autocfg[:epoch_num])
	  GetMaxPhaseNum(autocfg)
	  autocfg[:epoch_num] = autocfg[:max_phase_num] >> $phase_shift
  end

  return autocfg[:epoch_num]
end


def GetFirstMsgDW(seed_dw, msg_size)
  return (seed_dw & 0xFFFF0000) + (msg_size >> 4)
end

def GetNextPreloadDW(prev_dw)
#  return prev_dw + 1
  return 0 #preload zeros
end

def ParseStreamString(stream_string)
  return stream_string.scan(/chip_(\d+)__y_(\d+)__x_(\d+)__stream_id_(\d+)/).last.map {|str| str.to_i }
end

def GetStreamFlags(stream_id, phase, dram_blob, dram_buffer_index)
  is_fork_stream_id = false
  if (phase[:fork_stream_ids])
    phase[:fork_stream_ids].each do |fork_stream_id|
      if fork_stream_id == stream_id
        is_fork_stream_id = true
      end
    end
  end

  is_brisc_pack = phase[:source_endpoint] && !phase[:legacy_pack] && !phase[:dram_input] && !phase[:dram_streaming] && !phase[:intermediate] && !phase[:park_input]

  result = 
    (phase[:park_input]      ? 0x1   : 0) |
    (phase[:park_output]     ? 0x80  : 0) |
    (phase[:intermediate]    ? 0x10  : 0) |
    (phase[:moves_raw_data]  ? 0x40  : 0) |
    (is_fork_stream_id       ? 0x100 : 0) |
    (is_brisc_pack           ? 0x400 : 0) |
    (phase[:dram_output_no_push] || phase[:dram_input_no_push] ? 0x800 : 0) |
    (phase[:ncrisc_clear]    ? 0x1000: 0)

  if dram_blob
    result = result |
      (dram_blob[dram_buffer_index][:dram_io]        ? 0x2   : 0) |
      (dram_blob[dram_buffer_index][:dram_input]     ? 0x4   : 0) |
      (dram_blob[dram_buffer_index][:dram_output]    ? 0x8   : 0) |
      (dram_blob[dram_buffer_index][:dram_streaming] ? 0x20  : 0) |
      (dram_blob[dram_buffer_index][:dram_ram]       ? 0x200 : 0) 
  end

  return result
end

def is_int(val)
  return val == val.to_i.to_s
end

def is_bool(val)
  return val == "true" || val == "false" || val == "True" || val == "False"
end

def is_hex(val)
  return val[0] == "0" && val[1] == "x" && val == "0x#{val.to_i(16).to_s(16)}"
end

def is_bracket(val)
  return val[0] == "["
end

def CustomParseYAML(filename)
  graph = {}
  indent = {}

  indent[0] = graph

  File.foreach(filename).with_index do |line, line_num|
    line[-1] = "" # remove newline char

    attrib = line.split(":")

    if attrib.length != 1 && attrib.length != 2
      abort("Error! This attribute appears invalid: #{line}")
    end

    level = attrib[0][/\A */].size

    for i in 0..(attrib.length-1) do
      val = attrib[i]
      if (i == 0)
        val = val[level..-1] # trim the indentation
      else
        val = val[1..-1] # trim the single space char
      end

      if is_int(val)
        val = val.to_i
      elsif is_bool(val)
        val = val == "true" || val == "True"
      elsif is_hex(val)
        val = val.to_i(16)
      elsif is_bracket(val)
        val.strip!
        val = val[1..-2]
        val = val.split(",")
        for k in 0..(val.length-1) do
          val[k].strip!

          if is_int(val[k])
            val[k] = val[k].to_i
          elsif is_bool(val[k])
            val[k] = val[k] == "true" || val[k] == "True"
          elsif is_hex(val[k])
            val[k] = val[k].to_i(16)
          else
            val[k] = :"#{val[k]}"
          end
        end
      else
        val = :"#{val}"
      end

      if (i == 0)
        key = val
      end
    end

    if attrib.length == 1
      indent[level][key] = {}
      indent[level+2] = indent[level][key]
    elsif attrib.length == 2
      indent[level][key] = val
    end
  end

  return graph
end

def GraphKeysToSymbol(graph)
  if graph.is_a? Array
    result = []
    graph.each do |item|
      result << GraphKeysToSymbol(item)
    end
  elsif graph.is_a? Hash
    result = {}
    graph.each do |key, val|
      new_val = GraphKeysToSymbol(val)
      if key.is_a? String
        new_key = :"#{key}"
      else
        new_key = key
      end
      result[new_key] = new_val
    end
  else
    result = graph
  end
  return result;
end

def GraphFromYAML(filename)
  
  # The yaml parser is too slow, only enable if the custom parser doesnt work properly
  # and you want a quick workaround
  #graph = YAML.load(File.read(filename))
  #return GraphKeysToSymbol(graph)
  
  return CustomParseYAML(filename)

end


def compile_pkernel(pkernel, graph_name, chip_id, y, x, out_name, params)
  if ($PARAMS[:root] == nil)
    return ""
  end

  pkernel_out = "#{$PARAMS[:blob_out_dir]}/#{graph_name}_#{chip_id}_#{y}_#{x}_pkernels/"

  # Template the files
  s = File.read($PARAMS[:root] + "/src/pkernels/kernels/" + pkernel + ".cc.erb")
  pparams = params
  t = ERB.new(s, nil, '-')
  FileUtils.mkdir_p(pkernel_out + out_name)
  cc_file = File.open(pkernel_out + out_name + "/" + out_name + ".cc", "w")
  cc_file.puts(t.result(binding))
  cc_file.close

  # Compile the files
  output_dir = File.expand_path(pkernel_out + out_name)
  buda_home = File.expand_path($PARAMS[:root])
  make_cmd = "make "
  if $verbose == 0
    make_cmd += "-s "
  end
  make_cmd += "-C #{$PARAMS[:root]}/src/pkernels/toolchain all "
  make_cmd += "-j8 "
  make_cmd += "VERBOSE_BUILD=#{($verbose > 0 ? 1 : 0)} "
  make_cmd += "ARCH_NAME=#{$PARAMS[:chip]} "
  make_cmd += "OUTPUT_DIR=#{output_dir} "
  make_cmd += "BUDA_HOME=#{buda_home} "
  make_cmd += "FIRMWARE_NAME=#{out_name} "
  if $verbose == 1
    puts make_cmd
  end
  system(make_cmd)

  # Get the hex file
  hex_file = File.read(pkernel_out + out_name + "/" + out_name + ".hex")
  if hex_file.include?("@")
    abort("Error! Compilation of #{out_name} failed! It appears that there are multiple @ sections.")
  end

  # Return hex file
  return hex_file
end


# Main function where epoch_t (from epoch.h) is being written to output stream which will end up in resulting .hex
def PopulateEpochTStruct(core_epoch_info, epoch_valid, yx_label, perf_blobs, epoch_info_stream_addr, global_info_blob)
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }
  epoch_t_dws = []
  # *** Writing epoch_t ***
  if $verbose_debug == 1
    epoch_t_dws << "epoch_t start"
  end
  epoch_t_dws << core_epoch_info[:num_inputs]
  epoch_t_dws << core_epoch_info[:num_outputs]
  epoch_t_dws << core_epoch_info[:stream_info].length
  epoch_t_dws << epoch_valid # {epoch_valid, all_streams_ready, all_streams_and_kernels_done, end_program}

  tile_size_tile_header_buf_addr_map = core_epoch_info[:overlay_valid] == 1? (IsEthernetYX(y,x) ? $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id][y][x] : $tile_size_tile_header_buf_addr_map_per_chip[chip_id]) : {}
  num_tile_sizes = tile_size_tile_header_buf_addr_map.size
  tile_sizes_dws = []
  tile_header_buf_addr_dws = []
  if core_epoch_info[:overlay_valid] == 1
    # Output the info on tile size and respected tile header buffer addresses sorted by tile size
    tile_size_tile_header_buf_addr_map.sort_by { |tile_size, _| tile_size }.each do |tile_size, tile_header_buf_addr|
      tile_sizes_dws << (tile_size / 16)
      tile_header_buf_addr_dws << tile_header_buf_addr
    end
  end
  t = ($epoch_max_num_tile_sizes - num_tile_sizes)
  while t > 0
    tile_sizes_dws << 0
    tile_header_buf_addr_dws << 0
    t -= 1;
  end
  
  epoch_t_dws << num_tile_sizes
  epoch_t_dws += tile_sizes_dws
  epoch_t_dws += tile_header_buf_addr_dws
    
  for i in 0..($epoch_max_inputs-1)
    if i < core_epoch_info[:num_inputs]
      stream_id = core_epoch_info[:input_streams][i]
      epoch_t_dws << epoch_info_stream_addr[stream_id]
    else
      epoch_t_dws << 0   
    end
  end
  for i in 0..($epoch_max_outputs-1)
    if i < core_epoch_info[:num_outputs]
      stream_id = core_epoch_info[:output_streams][i]
      epoch_t_dws << epoch_info_stream_addr[stream_id]
    else
      epoch_t_dws << 0   
    end
  end
  n = 0
  
  for stream_id in 0..($noc_num_streams-1)
    if core_epoch_info[:stream_to_info_index][stream_id]
      epoch_t_dws << epoch_info_stream_addr[stream_id]
      n += 1
    end
  end  
  while n < $noc_num_streams
    epoch_t_dws << 0
    n += 1
  end

  if core_epoch_info[:num_inputs] == 0 && core_epoch_info[:num_outputs] == 0
    core_epoch_info[:skip_kernels] = 1
  end

  epoch_t_dws << 0 # perf_dram_copy_req[0]
  epoch_t_dws << 0 # perf_dram_copy_req[1]
  epoch_t_dws << 0 # perf_dram_copy_req[2]
  epoch_t_dws << 0 # perf_dram_copy_req[3]
  epoch_t_dws << 0 # perf_dram_copy_req[4]
  epoch_t_dws << 0 # perf_dram_copy_ack[0]
  epoch_t_dws << 0 # perf_dram_copy_ack[1]
  epoch_t_dws << 0 # perf_dram_copy_ack[2]
  epoch_t_dws << 0 # perf_dram_copy_ack[3]
  epoch_t_dws << 0 # perf_dram_copy_ack[4]

  if perf_blobs && perf_blobs[yx_label]
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][0] & 0xFFFFFFFF) # perf_dram_addr[0] low
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][0] >> 32)        # perf_dram_addr[0] high
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][1] & 0xFFFFFFFF) # perf_dram_addr[1] low
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][1] >> 32)        # perf_dram_addr[1] high
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][2] & 0xFFFFFFFF) # perf_dram_addr[2] low
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][2] >> 32)        # perf_dram_addr[2] high
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][3] & 0xFFFFFFFF) # perf_dram_addr[3] low
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][3] >> 32)        # perf_dram_addr[3] high
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][4] & 0xFFFFFFFF) # perf_dram_addr[4] low
    epoch_t_dws << (perf_blobs[yx_label][:dram_perf_buf_noc_addr][4] >> 32)        # perf_dram_addr[4] high
    epoch_t_dws << ((perf_blobs[yx_label][:dram_perf_buf_max_req][1] << 16) | perf_blobs[yx_label][:dram_perf_buf_max_req][0]) # {perf_dram_size[1], perf_dram_size[0]}
    epoch_t_dws << ((perf_blobs[yx_label][:dram_perf_buf_max_req][3] << 16) | perf_blobs[yx_label][:dram_perf_buf_max_req][2]) # {perf_dram_size[3], perf_dram_size[2]}
    epoch_t_dws << ((0 << 16) | perf_blobs[yx_label][:dram_perf_buf_max_req][4]) # {UNUSED[0], perf_dram_size[4]}
    epoch_t_dws << 0 # {UNUSED[2], UNUSED[1]}
    epoch_t_dws << 0 # {UNUSED[4], UNUSED[3]}
  else
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
  end
  
  # Address in L1 of fallback buffer NCRISC will use in case core exceeds L0 limitations. Used just to pass it down to
  # NCRISC firmware. Exists only if some core on chip exceeds limitations and exists only for that core.
  if ((global_info_blob != nil) && 
      (global_info_blob[yx_label] != nil) && 
      (global_info_blob[yx_label][:ncrisc_fallback_buffer_l1_address] != nil))
    epoch_t_dws << global_info_blob[yx_label][:ncrisc_fallback_buffer_l1_address]
  else
    epoch_t_dws << 0
  end

  epoch_t_dws << ((core_epoch_info[:ublock_ct] ? core_epoch_info[:ublock_ct] << 16 : 0) | (core_epoch_info[:ublock_rt] ? core_epoch_info[:ublock_rt] : 0))
  epoch_t_dws << ((core_epoch_info[:mblock_n] ? core_epoch_info[:mblock_n] << 16 : 0) | (core_epoch_info[:mblock_m] ? core_epoch_info[:mblock_m] : 0))
  epoch_t_dws << ((core_epoch_info[:has_packer_mcast_opt] ? 1 << 24 : 0) | (core_epoch_info[:has_eth_stream_trans] ? 1 << 16 : 0) | (core_epoch_info[:mblock_k] ? core_epoch_info[:mblock_k] : 0))

  epoch_t_dws << ((core_epoch_info[:skip_kernels] << 16) | (core_epoch_info[:overlay_valid] & 0xFF))
  epoch_t_dws << core_epoch_info[:epoch_id]
  
  # Initialize  tile clear blob to 0.
  for i in 0..(15)
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
    epoch_t_dws << 0
  end

  if $verbose_debug == 1
    epoch_t_dws << "epoch_t padding start"
  end
  # Pad with zeros to achieve alignment.
  $epoch_t_struct_padding.times do
    epoch_t_dws << 0
  end

  if $verbose_debug == 1
    epoch_t_dws << "dummy phase header and data"
  end
  # dummy phase tile header and data needs to be at the end of epoch_t
  # end of writing epoch_t from epoch.h to output stream
  epoch_t_dws << 0x1 # dummy_phase_tile_header_and_data[0]
  epoch_t_dws << 0x0 # dummy_phase_tile_header_and_data[1]
  epoch_t_dws << 0x0 # dummy_phase_tile_header_and_data[2]
  epoch_t_dws << (IsEthernetYX(y,x) ? 0 : core_epoch_info[:overlay_blob_extra_size])

  if $verbose_debug == 1
    epoch_t_dws << "epoch_t end"
  end
  # *** End of writing epoch_t ***
  
  return epoch_t_dws
end

def PopulateInvalidEpochTStruct(chip_id, y, x)
  core_epoch_info = {
    :num_inputs => 0,
    :num_outputs => 0,
    :stream_info => [],
    :stream_to_info_index => {},
    :overlay_valid => 0,
    :skip_kernels => 1,
    :overlay_blob_extra_size => 0,
    :ublock_ct => 0,
    :ublock_rt => 0,
    :mblock_n => 0,
    :mblock_m => 0,
    :mblock_k => 0,
    :epoch_id => 0
  }

  return PopulateEpochTStruct(core_epoch_info, 1, "chip_#{chip_id}__y_#{y}__x_#{x}", nil, {}, nil)
end

def compute_epoch_info_addrs(epoch_info, yx_label, epoch_info_space_start)
  epoch_info_stream_addr = {}
  epoch_stream_info_total_size = 0

  for stream_id in 0..($noc_num_streams-1)
    if epoch_info[yx_label][:stream_to_info_index][stream_id]
      # epoch_stream_info_t struct starts right after epoch_t struct.
      epoch_info_stream_addr[stream_id] = epoch_info_space_start + $epoch_t_struct_padded_size + epoch_stream_info_total_size

      # Increase size by full size of epoch_stream_info_t.
      epoch_stream_info_total_size += $epoch_stream_info_t_struct_padded_size

      stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
      stream_info = epoch_info[yx_label][:stream_info][stream_info_index]
  
      # If stream is_scatter_pack and scatter_order_size > 1, we add list of scatter_pipe_state_t structs
      # followed by list of scatter_pipe_blob_t structs.
      if stream_info[:blob_scatter].size() > 0
        # Increase size by full size of those lists.
        epoch_stream_info_total_size += GetScatterPipeStateAndBlobListPaddedSize(stream_info[:blob_scatter])
      end

      # If stream has NCRISC configs assigned
      if stream_info[:dram_stream]
        # Increase size by full size of epoch_stream_dram_io_info_t.
        epoch_stream_info_total_size += $epoch_stream_dram_io_info_t_struct_padded_size

        for dram_buf_id in 0..(stream_info[:dram_stream_buf_list].length()-1)
          # For each NCRISC config we add one dram_io_scatter_state_t struct. Increase size by full size of dram_io_scatter_state_t.
          epoch_stream_info_total_size += $dram_io_state_t_struct_padded_size

          if (stream_info[:dram_stream_buf_list][dram_buf_id][:dram_scatter_offsets].length() > 0)
            # For each DRAM scatter offset, we add one dram_io_scatter_state_t struct. Increase size by full size of dram_io_scatter_state_t.
            epoch_stream_info_total_size += $dram_io_scatter_state_t_struct_padded_size

            # For each DRAM scatter offset, we write $dram_io_scatter_offsets_size bytes of data. Increase size by full list of offsets.
            epoch_stream_info_total_size += GetDramScatterOffsetsListPaddedSize(stream_info[:dram_stream_buf_list][dram_buf_id][:dram_scatter_offsets].length())
          end
        end
      end
    end
  end

  return [epoch_stream_info_total_size, epoch_info_stream_addr]
end

def WriteBlobHexFile(blob_hex_filename, blob_hex_sections)
  blob_hex_file = File.open(blob_hex_filename, "w")
  blob_hex_section_addrs = blob_hex_sections.keys.sort
  blob_hex_section_addrs.each do |sect_addr|
    addr_marker = (sect_addr >> 2).to_s(16).rjust(8, "0")
    blob_hex_file.puts("@#{addr_marker}")
    blob_hex_sections[sect_addr].each do |dw|
      # If verbose_debug is on, this will produce hex files with string stamps in them, which won't work!
      if dw.is_a? String
        if $verbose_debug == 1
          dw_str = dw
        else
          next
        end
      else
        # Pad to 8 characters. Also take only the last 8 characters in case the number is too big.
        dw_str = dw.to_s(16).rjust(8, "0")[-8, 8]
      end
      blob_hex_file.puts(dw_str)      
    end
  end
  blob_hex_file.close
  if $PARAMS[:ln_dir]
    FileUtils.ln_s(blob_hex_filename, "#{$PARAMS[:ln_dir]}/overlay_#{chip_id}_#{y}_#{x}.hex", :force => true)
  end
end

####

# Contains info about each phase for each stream in order [graph][node][stream][phase] (its a reorganization of $tt_overlay_graphs)

$tt_stream_seq = {} 
$tt_sorted_phases = {} 
$tt_graph_core_stream_phase_cfg = {}


graph_name = $PARAMS[:graph_name]

starting_time = Process.clock_gettime(Process::CLOCK_MONOTONIC)

if $PARAMS[:graph_yaml] == 1
  graph_file = ($PARAMS[:graph_input_file] != nil) ? $PARAMS[:graph_input_file] : "#{$PARAMS[:blob_gen_root]}/graphs/#{graph_name}.yaml"
  $tt_overlay_graphs[graph_name] = GraphFromYAML(graph_file)
end

autocfg = $tt_overlay_graphs[graph_name]
dram_blobs = autocfg[:dram_blob]
perf_blobs = autocfg[:dram_perf_dump_blob]
global_info_blob = autocfg[:global_info_blob]

if $verbose == 2
  puts "%%%%%%%%%%%%%%%%"
  pp autocfg
  puts "%%%%%%%%%%%%%%%%"
end
  
for phase in 0..(GetMaxPhaseNum(autocfg)&$phase_mask) do
  phase = phase | (GetEpochNum(autocfg) << $phase_shift)
  if !autocfg[:"phase_#{phase}"]
    next
  end
  autocfg[:"phase_#{phase}"].each do |stream, stream_cfg|
		chip_id, y, x, stream_id = ParseStreamString(stream.to_s)
		if !$tt_stream_seq[graph_name]
		  $tt_stream_seq[graph_name] = {}
		end
    if !$tt_stream_seq[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"]
		  $tt_stream_seq[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"] = {}
		end
		if !$tt_stream_seq[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id]
		  $tt_stream_seq[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id] = {}
		end    
		$tt_stream_seq[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id][phase] = stream_cfg
    if !$tt_sorted_phases[graph_name]
		  $tt_sorted_phases[graph_name] = {}
		end
    if !$tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"]
		  $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"] = {}
		end
		if !$tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id]
		  $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id] = []
		end
  end
end
  

# Add default settings to graph
phase_info = $tt_stream_seq[graph_name]

# sort streams by stream_id for each chip
# won't be needed when we invert blob.yaml so that we have phases by stream not streams by phase.
phase_info.each do |yx_label, streams|
  streams = streams.sort_by { |stream_id, _| stream_id }
  phase_info[yx_label] = streams.to_h
end

if $verbose == 2
  puts "%%%%%%%%%%%%%%%%"
  pp phase_info
  puts "%%%%%%%%%%%%%%%%"
end

phase_info.each do |yx_label, streams|
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }
  streams.each do |stream_id, phases|
    phase_nums = phases.keys.sort
    $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id] = phase_nums
  end
end

phase_info.each do |yx_label, streams|
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }
  streams.each do |stream_id, phases|
    phase_nums = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id]
    num_phases = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id].length
    for p in 0..(num_phases-1) do
      phase = phase_nums[p]
		  if (phases[phase])
		    # Add :src and :dest blank arrays if missing
		    if (!phases[phase][:src])
			    phases[phase][:src] = []
        else
          if (!phases[phase][:src].is_a?(Array))
            phases[phase][:src] = [phases[phase][:src]]
          end
        end

		    if (!phases[phase][:dest])
			    phases[phase][:dest] = []
		    end

		    if (!phases[phase][:num_iters_in_epoch])
          phases[phase][:num_iters_in_epoch] = 1
        end
        
        # Check whether we need to set autorun feature
		    if (phases[phase][:auto_run])
			    phases[phase][:phase_auto_config] = true
			    phases[phase][:phase_auto_advance] = true
			    phases[phase][:data_auto_send] = true
		    end

        if (!phases[phase][:msg_size])
          phases[phase][:msg_size] = 1024
        end

		    # Set the outgoing data noc
		    if (!phases[phase][:outgoing_data_noc])
			    phases[phase][:outgoing_data_noc] = 0
		    end

        # Set default next_phase change
        if (phases[phase][:next_phase_src_change] == nil)
          phases[phase][:next_phase_src_change] = true
        end
        if (phases[phase][:next_phase_dest_change] == nil)
          phases[phase][:next_phase_dest_change] = true
        end

        if (phases[phase][:vc] == nil)
          phases[phase][:vc] = 2 
        end
        if (phases[phase][:reg_update_vc] == nil)
          phases[phase][:reg_update_vc] = 3
        end

        if (phases[phase][:park_input] == nil)
          phases[phase][:park_input] = false
        end

        if (phases[phase][:park_output] == nil)
          phases[phase][:park_output] = false
        end

        if (phases[phase][:moves_raw_data] == nil)
          phases[phase][:moves_raw_data] = false
        end

        if (phases[phase][:dram_output_no_push] == nil)
          phases[phase][:dram_output_no_push] = false
        end
        if (phases[phase][:dram_input_no_push] == nil)
          phases[phase][:dram_input_no_push] = false
        end
        
        if (phases[phase][:c_dim_loop_num_rows] == nil)
          phases[phase][:c_dim_loop_num_rows] = 32
        end
        if (phases[phase][:tile_dim_r] == nil)
          phases[phase][:tile_dim_r] = 32
        end
        if (phases[phase][:tile_dim_c] == nil)
          phases[phase][:tile_dim_c] = 32
        end
        if (phases[phase][:hw_tilize] == nil)
          phases[phase][:hw_tilize] = false
        end
        if (phases[phase][:scatter_list_indicies_per_tile] == nil)
          phases[phase][:scatter_list_indicies_per_tile] = 0
        end
        if (phases[phase][:scatter_list_stream_id] == nil)
          phases[phase][:scatter_list_stream_id] = 0
        end
        if (phases[phase][:scatter_list_indicies_per_input] == nil)
          phases[phase][:scatter_list_indicies_per_input] = 0
        end
        if (phases[phase][:tilize_row_col_offset] == nil)
          phases[phase][:tilize_row_col_offset] = 0
        end
        if (phases[phase][:dram_embeddings_row_shift] == nil)
          phases[phase][:dram_embeddings_row_shift] = 0
        end
        if (phases[phase][:dram_embeddings_row_tiles] == nil)
          phases[phase][:dram_embeddings_row_tiles] = 0
        end

        if (phases[phase][:dram_writes_with_cmd_buf] == nil)
          phases[phase][:dram_writes_with_cmd_buf] = false
        end

        if (phases[phase][:producer_epoch_id] == nil)
          phases[phase][:producer_epoch_id] = 0
        end

        if (phases[phase][:src_flow_ctrl_in_fw] == nil)
          phases[phase][:src_flow_ctrl_in_fw] = false
        end

        if (phases[phase][:stride] == nil)
          phases[phase][:stride] = 0
        end

        if (phases[phase][:total_strides] == nil)
          phases[phase][:total_strides] = 1
        end

        if (phases[phase][:buf_full_size_bytes] == nil)
          phases[phase][:buf_full_size_bytes] = phases[phase][:buf_size]
        end

        if (phases[phase][:buf_base_addr] == nil)
          phases[phase][:buf_base_addr] = phases[phase][:buf_addr]
        end

        if (phases[phase][:pipe_scatter_output_loop_count] == nil)
          phases[phase][:pipe_scatter_output_loop_count] = 1
        end

        if (phases[phase][:num_scatter_inner_loop] == nil)
          phases[phase][:num_scatter_inner_loop] = 1
        end

		  end
	  end
  end
end

# Process graph
phase_info.each do |yx_label, streams|
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }
  if IsEthernetYX(y, x)
    if not $tile_size_tile_header_buf_addr_map_eth_per_core.key? chip_id
      $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id] = {}
    end
    if not $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id].key? y
      $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id][y] = {}
    end
    if not $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id][y].key? x
      $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id][y][x] = {}
    end
  else
    if not $tile_size_tile_header_buf_addr_map_per_chip.key? chip_id
      $tile_size_tile_header_buf_addr_map_per_chip[chip_id] = {}
    end
  end
  chip_ids.add(chip_id)

  i = 0
  chip_ids.each do |n|
    chip_ids_to_grid_size_x[n] = grid_sizes_x[i].to_i
    chip_ids_to_grid_size_y[n] = grid_sizes_y[i].to_i
    i = i + 1
  end

  streams.each do |stream_id, phases|
    phase_nums = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id]
    num_phases = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id].length
    for p in 0..(num_phases-1) do
      phase = phase_nums[p]
		  if (phases[phase])
        phases[phase][:phase_num] = phase

        # Determine all msg info sizes per core
        msg_size = phases[phase][:msg_size]
        msg_info_buf_addr = phases[phase][:msg_info_buf_addr]
        tile_size_tile_header_buf_addr_map = IsEthernetYX(y, x) ? $tile_size_tile_header_buf_addr_map_eth_per_core[chip_id][y][x] : $tile_size_tile_header_buf_addr_map_per_chip[chip_id]
        if not tile_size_tile_header_buf_addr_map.key? (msg_size)
          tile_size_tile_header_buf_addr_map[msg_size] = msg_info_buf_addr
        end
		  end
	  end
  end
end

if $verbose == 1
  pp "Tile sizes found: "
  pp $tile_size_tile_header_buf_addr_map_per_chip
end


######

FileUtils.mkdir_p "#{$PARAMS[:blob_out_dir]}"

# *** epoch_info dict definition and init ***
# Example
# {
#   :chip_0__y_1__x_2=>{
#     :num_inputs=>2,
#     :num_outputs=>1,
#     :input_streams=>{0=>9, 1=>8},
#     :output_streams=>{0=>24},
#     :stream_info=>[
#       {
#         :stream_id=>9,
#         ...
#         :dram_stream=>true,
#         :dram_stream_buf_list=>[
#           {
#             :dram_buf_noc_addr=>4848615424,
#             :dram_buf_size_bytes=>4160,
#             ...
#             :dram_scatter_offsets=>[4848615424]
#           },
#           ...
#         ],
#         :producer_epoch_id=>0,
#         ...
#         :blob_size=>48
#       },
#       {
#         :stream_id=>8,
#         ...
#       }
#     ],
#     :stream_to_info_index=>{9=>0, 8=>1, 24=>2},
#     :overlay_valid=>1,
#     :has_eth_stream_trans=>nil,
#     :has_packer_mcast_opt=>nil,
#     :overlay_blob_extra_size=>0,
#     :skip_kernels=>0,
#     :ublock_rt=>1,
#     :ublock_ct=>1,
#     :mblock_m=>1,
#     :mblock_n=>1,
#     :mblock_k=>1,
#     :full_blob_size=>256
#   },
#   :chip_0__y_1__x_1=>{
#     ...
#   }
# }

epoch_info = {}

# Iterate through phase_info which is a dict of form
# {
#   chip_and_core_1 : {
#       stream_id_1: {
#         first_phase_of_this_stream: {
#           phase_info_from_blob.yaml
#         },
#         second_phase_of_this_stream: {
#           ...
#         },
#         ...
#       },
#       stream_id_2: {
#         ...
#       }
#       ...
#   },
#   chip_and_core_2 : {
#     ...
#   }
#   ...
# }
phase_info.each do |yx_label, streams|
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d\d*)__x_(\d\d*)/).last.map {|str| str.to_i }
  epoch_info[yx_label] = {:num_inputs => 0, :num_outputs => 0, 
                          :input_streams => {}, :output_streams => {},
                          :stream_info => [], :stream_to_info_index => {},
                          :overlay_valid => 1, :has_eth_stream_trans => false, :has_packer_mcast_opt => false,
                          :overlay_blob_extra_size => 0,
                          :skip_kernels => 0}
  
  curr_blob_relative_offset = 0
  blob_hex_file = "#{$PARAMS[:blob_out_dir]}/#{graph_name}_#{chip_id}_#{y}_#{x}.hex"

  # Iterate through streams on core. Each stream has its `phases` map which is dict
  # containing info about each phase that stream goes through.
  streams.each do |stream_id, phases|
    yx_stream_label = (yx_label.to_s + "__stream_id_" + stream_id.to_s).to_sym
    phase_nums = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id]
    num_phases = $tt_sorted_phases[graph_name][:"chip_#{chip_id}__y_#{y}__x_#{x}"][stream_id].length
    last_phase = phases[phase_nums[num_phases-1]]
    curr_blob_start_offset = curr_blob_relative_offset
    scatter_prev_phase = {}

    for p in 0..(num_phases-1) do
      phase = phases[phase_nums[p]]

      if $verbose == 1
        pp ("     *** Stream #{stream_id}, phase #{phase_nums[p]} auto cfg offset = 0x#{curr_blob_relative_offset.to_s(16)}, buf_addr=0x#{phase[:buf_addr].to_s(16)}, buf_size=0x#{phase[:buf_size].to_s(16)}, msg_info_buf_addr=0x#{phase[:msg_info_buf_addr].to_s(16)}");
      end

      # Counts inputs and outputs to this core, stores it in epoch_t[yx_label].
      ["input", "output"].each do |key_type|
        if phase[:"#{key_type}_index"]
          index = phase[:"#{key_type}_index"]
          if epoch_info[yx_label][:"num_#{key_type}s"] < (index+1)
            epoch_info[yx_label][:"num_#{key_type}s"] = index+1
          end
          epoch_info[yx_label][:"#{key_type}_streams"][index] = stream_id
        end
      end
      if !epoch_info[yx_label][:stream_to_info_index][stream_id]
        epoch_info[yx_label][:stream_to_info_index][stream_id] = epoch_info[yx_label][:stream_info].length
        dram_stream = dram_blobs && dram_blobs.key?(yx_stream_label)
        flags = GetStreamFlags(stream_id, phase, dram_stream ? dram_blobs[yx_stream_label] : nil, 0)
        fork_stream_ids = phase[:fork_stream_ids]
        new_dram_stream_buf_list = []

        # If stream reads or writes to DRAM through NCRISC, fetch its NCRISC configs.
        if dram_stream
          dram_blobs[yx_stream_label].each do |dram_blob_idx, dram_blob|
            if dram_blob[:dram_buf_size_q_slots]
              q_slot_size_tiles = dram_blob[:dram_buf_size_tiles] / dram_blob[:dram_buf_size_q_slots]
            else
              q_slot_size_tiles = 1
            end
            if (dram_blob[:dram_scatter_offsets])
              if ((phase[:scatter_list_stream_id] && phase[:scatter_list_stream_id] != 0))
                dram_blob[:dram_scatter_offsets] = [dram_blob[:dram_scatter_offsets][0]]
              end
              dram_scatter_chunk_size_bytes = (dram_blob[:dram_scatter_chunk_size_tiles] ? dram_blob[:dram_scatter_chunk_size_tiles] : 0) * dram_blob[:msg_size]
              for k in 0..(dram_blob[:dram_scatter_offsets].size()-1) do
                if !phase[:hw_tilize] && ((dram_blob[:dram_scatter_offsets][k] & $dram_io_is_scatter_loop) == 0)
                  dram_blob[:dram_scatter_offsets][k] = modify_DRAM_NOC_addr(dram_blob[:dram_scatter_offsets][k], dram_blob[:dram_output] ? phase[:outgoing_data_noc] : phase[:incoming_data_noc])
                end
              end
            end
            # List of NCRISC configs for this core and stream i.e. epoch_t[yx_label][:stream_info].
            new_dram_stream_buf_list << {:dram_buf_noc_addr => modify_DRAM_NOC_addr(dram_blob[:dram_buf_noc_addr], dram_blob[:dram_output] ? phase[:outgoing_data_noc] : phase[:incoming_data_noc]),
                                         :dram_buf_size_bytes => dram_blob[:dram_buf_size_bytes],
                                         :dram_buf_size_tiles => dram_blob[:dram_buf_size_tiles],
                                         :dram_buf_size_q_slots => dram_blob[:dram_buf_size_q_slots],
                                         :dram_buf_read_chunk_size_tiles => dram_blob[:dram_buf_read_chunk_size_tiles],
                                         :num_scatter_inner_loop => phase[:num_scatter_inner_loop],
                                         :dram_scatter_chunk_size_tiles => dram_blob[:dram_scatter_chunk_size_tiles],
                                         :dram_scatter_chunk_size_bytes => dram_scatter_chunk_size_bytes,
                                         :dram_buf_write_chunk_size_tiles => dram_blob[:dram_buf_write_chunk_size_tiles],
                                         :reader_index => (dram_blob[:reader_index] ? dram_blob[:reader_index] : 0), 
                                         :total_readers => (dram_blob[:total_readers] ? dram_blob[:total_readers] : 1),
                                         :dram_buf_write_row_chunk_size_bytes => dram_blob[:dram_buf_write_row_chunk_size_bytes],
                                         :dram_padding => dram_blob[:dram_padding],
                                         :dram_input => dram_blob[:dram_input],
                                         :dram_output => dram_blob[:dram_output],
                                         :dram_streaming => dram_blob[:dram_streaming],
                                         :dram_streaming_dest => dram_blob[:dram_streaming_dest],
                                         :dram_q_slot_size_tiles => q_slot_size_tiles,
                                         :dram_q_slot_size_bytes => q_slot_size_tiles * dram_blob[:msg_size],
                                         :dram_scatter_offsets_full_size => dram_blob[:dram_scatter_offsets_full_size] ? dram_blob[:dram_scatter_offsets_full_size] : 0,
                                         :dram_scatter_offsets => dram_blob[:dram_scatter_offsets] ? dram_blob[:dram_scatter_offsets] : []}
          end
        end
        epoch_info[yx_label][:stream_info] << {:stream_id => stream_id, :start_phase => phase_nums[p], :moves_raw_data => phase[:moves_raw_data],
                                               :epoch_num_msgs => phase[:num_msgs], :msg_size_words => (phase[:msg_size] >> 4), :buf_full_size_bytes => phase[:buf_full_size_bytes],
                                               :buf_size_words => phase[:buf_size]/16, :num_iters_in_epoch => phase[:num_iters_in_epoch], :legacy_pack => phase[:legacy_pack],
                                               :num_scatter_inner_loop => phase[:num_scatter_inner_loop], :num_mblock_buffering => phase[:num_mblock_buffering],
                                               :pipe_scatter_output_loop_count => phase[:pipe_scatter_output_loop_count], :blob_scatter => {},
                                               :num_msgs_in_block => phase[:num_msgs_in_block], :scatter_order_size => phase[:scatter_order_size], :padding_scatter_order_size => phase[:padding_scatter_order_size],
                                               :outgoing_data_noc => phase[:outgoing_data_noc], :incoming_data_noc => phase[:incoming_data_noc],
                                               :msg_info_buf_start_addr => (phase[:msg_info_buf_addr] >> 4), :buf_base_addr => phase[:buf_base_addr], :buf_addr => phase[:buf_addr],
                                               :num_fork_streams => phase[:num_fork_streams], :space_shared_with_operand => phase[:space_shared_with_operand],
                                               :flags => flags, :fork_stream_ids => fork_stream_ids, :dram_stream => dram_stream, :dram_stream_buf_list => new_dram_stream_buf_list,
                                               :producer_epoch_id => phase[:producer_epoch_id], :stride => phase[:stride], :total_strides => phase[:total_strides],
                                               :stride_offset_size_bytes => phase[:stride_offset_size_bytes], :skip_col_bytes => phase[:skip_col_bytes],
                                               :skip_col_tile_row_bytes => phase[:skip_col_tile_row_bytes], :skip_col_row_bytes => phase[:skip_col_row_bytes],
                                               :skip_zcol_bytes => phase[:skip_zcol_bytes], :skip_col_zrow_bytes => phase[:skip_col_zrow_bytes],
                                               :r_dim_size => phase[:r_dim_size], :c_dim_size => phase[:c_dim_size], :is_scatter_pack => phase[:is_scatter_pack],
                                               :num_unroll_iter => phase[:num_unroll_iter], :dram_output_no_push => phase[:dram_output_no_push], :dram_input_no_push => phase[:dram_input_no_push],
                                               :scatter_list_indicies_per_tile => phase[:scatter_list_indicies_per_tile], :scatter_list_stream_id => phase[:scatter_list_stream_id],
                                               :scatter_list_indicies_per_input => phase[:scatter_list_indicies_per_input], :dram_embeddings_row_shift => phase[:dram_embeddings_row_shift],
                                               :hw_tilize => phase[:hw_tilize], :tilize_row_col_offset => phase[:tilize_row_col_offset],
                                               :dram_embeddings_row_tiles => phase[:dram_embeddings_row_tiles], :dram_writes_with_cmd_buf => phase[:dram_writes_with_cmd_buf], 
                                               :zr_dim_size => phase[:zr_dim_size], :zc_dim_size => phase[:zc_dim_size], :batch_dim_size => phase[:batch_dim_size],
                                               :tile_dim_r => phase[:tile_dim_r], :tile_dim_c => phase[:tile_dim_c], :c_dim_loop_num_rows => phase[:c_dim_loop_num_rows],
                                               :aliased_stream_id => phase[:space_shared_with_stream], :aliased_stream_type => 0
                                              }

        # Does some validity checks.
        if phase[:overlay_blob_extra_size] && phase[:overlay_blob_extra_size] != 0
          if (epoch_info[yx_label][:overlay_blob_extra_size] && epoch_info[yx_label][:overlay_blob_extra_size] != 0 && epoch_info[yx_label][:overlay_blob_extra_size] != phase[:overlay_blob_extra_size])
            abort("Error! found different overlay_blob_extra_size for #{yx_label}")
          end
          epoch_info[yx_label][:overlay_blob_extra_size] = phase[:overlay_blob_extra_size]
        end
        if epoch_info[yx_label][:stream_info][-1][:aliased_stream_id]
          epoch_info[yx_label][:stream_info][-1][:aliased_stream_type] = phase[:remote_receiver] ? 1 : 2 # 2==packer, 1==unpacker
          print("aliased_stream_id: #{epoch_info[yx_label][:stream_info][-1][:aliased_stream_id]}, aliased_stream_type: #{epoch_info[yx_label][:stream_info][-1][:aliased_stream_type]}\n")
          # epoch_info[yx_label][:stream_info][-1][:aliased_stream_id] = shared_stream
        else
          epoch_info[yx_label][:stream_info][-1][:aliased_stream_id] = 0
        end
        if phase[:ublock_rt] && phase[:ublock_rt] != 0
          if (epoch_info[yx_label][:ublock_rt] && epoch_info[yx_label][:ublock_rt] != 0 && epoch_info[yx_label][:ublock_rt] != phase[:ublock_rt])
            abort("Error! found different ublock_rt between output/intermediate streams for #{yx_label}")
          end
          epoch_info[yx_label][:ublock_rt] = phase[:ublock_rt]
        end
        if phase[:ublock_ct] && phase[:ublock_ct] != 0
          if (epoch_info[yx_label][:ublock_ct] && epoch_info[yx_label][:ublock_ct] != 0 && epoch_info[yx_label][:ublock_ct] != phase[:ublock_ct])
            abort("Error! found different ublock_ct between output/intermediate streams for #{yx_label}")
          end
          epoch_info[yx_label][:ublock_ct] = phase[:ublock_ct]
        end
        if phase[:mblock_m] && phase[:mblock_m] != 0
          if (epoch_info[yx_label][:mblock_m] && epoch_info[yx_label][:mblock_m] != 0 && epoch_info[yx_label][:mblock_m] != phase[:mblock_m])
            abort("Error! found different mblock_m between output/intermediate streams for #{yx_label}")
          end
          epoch_info[yx_label][:mblock_m] = phase[:mblock_m]
        end
        if phase[:mblock_n] && phase[:mblock_n] != 0
          if (epoch_info[yx_label][:mblock_n] && epoch_info[yx_label][:mblock_n] != 0 && epoch_info[yx_label][:mblock_n] != phase[:mblock_n])
            abort("Error! found different mblock_n between output/intermediate streams for #{yx_label}")
          end
          epoch_info[yx_label][:mblock_n] = phase[:mblock_n]
        end
        if phase[:mblock_k] && phase[:mblock_k] != 0
          if (epoch_info[yx_label][:mblock_k] && epoch_info[yx_label][:mblock_k] != 0 && epoch_info[yx_label][:mblock_k] != phase[:mblock_k])
            abort("Error! found different mblock_k between output/intermediate streams for #{yx_label}")
          end
          epoch_info[yx_label][:mblock_k] = phase[:mblock_k]
        end
        epoch_curr_stream_info = epoch_info[yx_label][:stream_info][-1]
      else
        stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
        epoch_curr_stream_info = epoch_info[yx_label][:stream_info][stream_info_index]
        if epoch_curr_stream_info[:start_phase] > phase_nums[p]
          epoch_curr_stream_info[:start_phase] = phase_nums[p]
        end
        epoch_curr_stream_info[:epoch_num_msgs] += phase[:num_msgs]
      end
      if epoch_curr_stream_info[:dram_stream] && epoch_curr_stream_info[:dram_stream_buf_list][0][:dram_padding] && (phase[:buf_base_addr] != epoch_curr_stream_info[:buf_base_addr] || phase[:buf_addr] != epoch_curr_stream_info[:buf_addr])
        abort("Error! found phases with different buf_addr/buf_base_addr for padding stream: #{yx_stream_label}, phase #{phase_nums[p]}")
      end

      next_phase = (p == (num_phases-1)) ? nil : phases[phase_nums[p+1]]
      prev_phase = (p == 0) ? nil : phases[phase_nums[p-1]]
      if !next_phase
        epoch_curr_stream_info[:last_phase_in_iter] = phase_nums[p]
        epoch_curr_stream_info[:num_iter_tiles] = epoch_curr_stream_info[:epoch_num_msgs]
        epoch_curr_stream_info[:epoch_num_msgs] *= epoch_curr_stream_info[:num_iters_in_epoch]
      end
      if phase[:remote_receiver] || (phase[:eth_sender] && phase[:receiver_endpoint])
        dest_chip, dest_y, dest_x, dest_stream_id = ParseStreamString(phase[:dest][0].to_s)
        if (!phase[:remote_receiver])
          stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
          epoch_info[yx_label][:stream_info][stream_info_index][:eth_remote_fw_stream_id] = dest_stream_id
          if (!epoch_info[yx_label][:stream_info][stream_info_index][:aliased_stream_type])
            abort("aliased_stream_type not found for ethernet firmware stream. #{yx_label}")
          # elsif (epoch_info[yx_label][:stream_info][stream_info_index][:use_ethernet_fw_stream])
          elsif (phase[:use_ethernet_fw_stream])
            if $verbose == 1
              pp ("#{yx_label}__#{stream_id} use_ethernet_fw_stream.");
            end
            epoch_info[yx_label][:stream_info][stream_info_index][:aliased_stream_type] |= 0x4
          end
        end

        if ((phase[:eth_receiver] && phase[:source_endpoint]))
          src_chip, src_y, src_x, src_stream_id = ParseStreamString(phase[:src][0].to_s)
          stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
          epoch_info[yx_label][:stream_info][stream_info_index][:eth_remote_fw_stream_id] = src_stream_id
          if (!epoch_info[yx_label][:stream_info][stream_info_index][:aliased_stream_type])
            abort("aliased_stream_type not found for ethernet firmware stream. #{yx_label}")
          elsif (phase[:use_ethernet_fw_stream])
            if $verbose == 1
              pp ("#{yx_label}__#{stream_id} use_ethernet_fw_stream.");
            end
            epoch_info[yx_label][:stream_info][stream_info_index][:aliased_stream_type] |= 0x4
          end
        end
        if $tt_stream_seq[graph_name][:"chip_#{dest_chip}__y_#{dest_y}__x_#{dest_x}"][dest_stream_id][phase_nums[p]]
          dest_phase = $tt_stream_seq[graph_name][:"chip_#{dest_chip}__y_#{dest_y}__x_#{dest_x}"][dest_stream_id][phase_nums[p]]
        else
          dest_phase_nums = $tt_sorted_phases[graph_name][:"chip_#{dest_chip}__y_#{dest_y}__x_#{dest_x}"][dest_stream_id]
          dest_num_phases = $tt_sorted_phases[graph_name][:"chip_#{dest_chip}__y_#{dest_y}__x_#{dest_x}"][dest_stream_id].length
          dest_prev_phase, dest_prev_stream_cfg = GetPrevPhase(phase_info, dest_phase_nums, dest_num_phases, phase_nums[p], dest_chip, dest_x, dest_y, dest_stream_id)
          dest_phase = $tt_stream_seq[graph_name][:"chip_#{dest_chip}__y_#{dest_y}__x_#{dest_x}"][dest_stream_id][dest_prev_phase]
        end
        if $verbose == 1
          pp ("          *** Dest x=#{dest_x}, y=#{dest_y}, chip=#{dest_chip}, stream_id=#{stream_id}, dest_phase=#{dest_phase[:phase_num]}");
        end
      else
        dest_phase = nil
      end
      
      epoch_info[yx_label][:has_eth_stream_trans] = epoch_info[yx_label][:has_eth_stream_trans] || phase[:eth_sender] || phase[:eth_receiver]
      epoch_info[yx_label][:has_packer_mcast_opt] = epoch_info[yx_label][:has_packer_mcast_opt] || phase[:has_packer_mcast_opt]
      if phase[:space_shared_with_stream] && (phase[:source_endpoint] || phase[:receiver_endpoint])
        epoch_info[yx_label][:skip_kernels] = 1
      end

      is_fork_stream_id = false
      if (phase[:fork_stream_ids])
        phase[:fork_stream_ids].each do |fork_stream_id|
          if fork_stream_id == stream_id
            is_fork_stream_id = true
          end
        end
      end

      dram_blob = dram_stream ? dram_blobs[yx_stream_label] : nil
      if (dram_blob == nil && is_fork_stream_id) 
        other_yx_label = ("chip_" + chip_id.to_s + "__y_" + y.to_s + "__x_" + x.to_s).to_sym
        other_streams = phase_info[other_yx_label]
        other_streams.each do |other_stream_id, other_phases|
          other_yx_stream_label = (other_yx_label.to_s + "__stream_id_" + other_stream_id.to_s).to_sym
          other_phase_nums = $tt_sorted_phases[graph_name][other_yx_label][other_stream_id]
          other_phase = other_phases[other_phase_nums[0]]
          if (other_phase && other_phase[:fork_stream_ids] && dram_blobs && dram_blobs.key?(other_yx_stream_label))
            other_phase[:fork_stream_ids].each do |fork_stream_id|
              if fork_stream_id == stream_id
                dram_blob = dram_blobs[other_yx_stream_label]
                break
              end
            end
          end
          if dram_blob != nil
            break
          end
        end     
      end

      is_pipe_scatter = phase[:is_scatter_pack] && phase[:scatter_order_size] && phase[:scatter_order_size] > 1

      has_dummy_phase_sender = phase[:follow_by_sender_dummy_phase]
      has_dummy_phase_receiver = phase[:follow_by_receiver_dummy_phase]
      has_dummy_phase = has_dummy_phase_sender || has_dummy_phase_receiver
      if $verbose == 1 && has_dummy_phase
        pp ("#{yx_stream_label}, phase #{phase_nums[p]} has dummy phase")
      end

      num_mblock_buffering = phase[:num_mblock_buffering] ? phase[:num_mblock_buffering] : 1
      mblock_base_addr = phase[:buf_base_addr]
      mblock_size = phase[:buf_full_size_bytes] / num_mblock_buffering
      new_mblock_base_addr = mblock_base_addr
      for mblock_buffering_idx in 0..(num_mblock_buffering-1) do
        if last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"] == nil
          last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"] = []
        end
        begin
          phase[:"cfg_dw_list_#{mblock_buffering_idx}"] = get_phase_blob(chip_id, x, y, stream_id, phase, prev_phase, next_phase, dest_phase, 
                                                                         dram_blob, mblock_base_addr, new_mblock_base_addr, (has_dummy_phase || last_phase[:"cfg_dw_list_dummy_0"].length() > 0) && !next_phase)
        rescue => e
          puts ("#{yx_stream_label}, phase #{phase_nums[p]} error: \n\n#{e}\n\n")
          abort
        end
        if has_dummy_phase
          dummy_phase_addr = (IsEthernetYX(y,x) ? $PARAMS[:blob_section_start_eth] : $PARAMS[:blob_section_start]) + $epoch_t_struct_padded_size - 16
          if has_dummy_phase_receiver
            last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"] << get_dummy_phase_blob(chip_id, x, y, stream_id, phase, dummy_phase_addr, nil, false, has_dummy_phase_receiver)
            if $verbose == 1
              if mblock_buffering_idx == 0
                pp ("#{yx_stream_label}, phase #{phase_nums[p]} adding dummy receiver phase for with src #{phase[:src]}")
              end
            end
          end
          if has_dummy_phase_sender
            dest_dummy_phase_addr = (IsEthernetYX(dest_y,dest_x) ? $PARAMS[:blob_section_start_eth] : $PARAMS[:blob_section_start]) + $epoch_t_struct_padded_size - 16
            last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"] << get_dummy_phase_blob(chip_id, x, y, stream_id, phase, dummy_phase_addr, dest_dummy_phase_addr, has_dummy_phase_sender, false)
            if $verbose == 1
              if mblock_buffering_idx == 0
                pp ("#{yx_stream_label}, phase #{phase_nums[p]} adding dummy sender phase for with dest #{phase[:dest]}")
              end
            end
          end
        end
        new_mblock_base_addr = new_mblock_base_addr + mblock_size
      end

      if phase[:intermediate]
        # infinite loop for intermediates - last word needs to be added below when we compute absolute blob address
        # insert placeholder here so length calculations are correct
        phase[:cfg_dw_list_0] << 0xaabbccdd
      end
      if prev_phase
        if (is_pipe_scatter && prev_phase[:scatter_idx] != phase[:scatter_idx])
          if !epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]]
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]] = {}
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:offsets] = []
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:phase_num_cfg_regs] = []
          end
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:offsets] << curr_blob_relative_offset
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:phase_num_cfg_regs] << phase[:cfg_dw_list_0].length
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:num_unroll_iter] = phase[:num_unroll_iter]
        end
        phase[:phase_num_inc] = phase[:phase_num] - prev_phase[:phase_num]
        phase_wrapped = (phase[:phase_num] & ~$wrapped_phase_mask) != (prev_phase[:phase_num] & ~$wrapped_phase_mask)
        if (phase[:phase_num_inc] >= 4096 || phase_wrapped)
          phase[:phase_num_inc] = 0
        end
        if (is_pipe_scatter && prev_phase[:scatter_idx] != phase[:scatter_idx])
          prev_scatter_phase = scatter_prev_phase[phase[:scatter_idx]]
          if (prev_scatter_phase)
            prev_scatter_phase[:cfg_header_dw] = blob_header_dw(phase[:cfg_dw_list_0].length, prev_scatter_phase[:num_msgs], prev_scatter_phase[:phase_num_inc])
            scatter_prev_phase.delete(phase[:scatter_idx])
          end
        else
          prev_phase[:cfg_header_dw] = blob_header_dw(phase[:cfg_dw_list_0].length, prev_phase[:num_msgs], prev_phase[:phase_num_inc])
        end
      else
        phase[:phase_num_inc] = 0
        stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
        epoch_curr_stream_info[:blob_start_relative_offset] = curr_blob_relative_offset
        if is_pipe_scatter
          if !epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]]
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]] = {}
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:offsets] = []
            epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:phase_num_cfg_regs] = []
          end
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:offsets] << curr_blob_relative_offset
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:phase_num_cfg_regs] << phase[:cfg_dw_list_0].length
          epoch_curr_stream_info[:blob_scatter][phase[:scatter_idx]][:num_unroll_iter] = phase[:num_unroll_iter]
        end
        epoch_curr_stream_info[:start_phase_num_cfg_regs] = phase[:cfg_dw_list_0].length
      end
      if next_phase
        if (is_pipe_scatter && phase[:scatter_idx] != next_phase[:scatter_idx])
          scatter_prev_phase[phase[:scatter_idx]] = phase
        end
      else
        scatter_prev_phase.each do |scatter_idx, prev_scatter_phase|
          prev_scatter_phase[:cfg_header_dw] = blob_header_dw(0, prev_scatter_phase[:num_msgs], prev_scatter_phase[:phase_num_inc])
        end
        if (has_dummy_phase || last_phase[:"cfg_dw_list_dummy_0"].length() > 0)
         phase[:cfg_header_dw] = blob_header_dw(last_phase[:"cfg_dw_list_dummy_0"][0].length(), phase[:num_msgs], phase[:phase_num_inc])
        elsif phase[:intermediate]
         phase[:cfg_header_dw] = blob_header_dw(phase[:cfg_dw_list_0].length, phase[:num_msgs], phase[:phase_num_inc])
        else
         phase[:cfg_header_dw] = blob_header_dw(0, phase[:num_msgs], phase[:phase_num_inc])
        end
      end
      curr_blob_relative_offset += (4*(phase[:cfg_dw_list_0].length + 1))
      if (has_dummy_phase_receiver)
        curr_blob_relative_offset += (4*(last_phase[:"cfg_dw_list_dummy_0"][has_dummy_phase_sender ? -2 : -1].length + 1))
      end
      if (has_dummy_phase_sender)
        curr_blob_relative_offset += (4*(last_phase[:"cfg_dw_list_dummy_0"][-1].length + 1))
      end
      prev_phase = phase
    end
    epoch_curr_stream_info[:blob_size] = curr_blob_relative_offset - curr_blob_start_offset
    curr_blob_relative_offset += epoch_curr_stream_info[:blob_size]*(num_mblock_buffering-1)
  end
  # TODO Should this be changed as well? Should blobs be aligned or just successive?
  epoch_info[yx_label][:full_blob_size] = curr_blob_relative_offset
  if $verbose == 2
    puts "#{yx_label} => "
    pp epoch_info[yx_label][:stream_info]
  end
end

if $verbose == 1
  puts "printing epoch_info dict with cores as keys"
  pp epoch_info
end

used_cores = Set[]

# Main loop where epoch_stream_info_t, scatter_pipe_state_t, scatter_pipe_blob_t, epoch_stream_dram_io_info_t,
# dram_io_state_t, dram_io_scatter_state_t are written
phase_info.each do |yx_label, streams|
  
  chip_id, y, x = yx_label.to_s.scan(/chip_(\d+)__y_(\d+)__x_(\d+)/).last.map {|str| str.to_i }
  used_cores.add(yx_label)
  
  epoch_info_space_start = IsEthernetYX(y,x) ? $PARAMS[:blob_section_start_eth] : $PARAMS[:blob_section_start]

  blob_hex_sections = {}

  puts("2 graph_name #{graph_name} blob out dir #{$PARAMS[:blob_out_dir]}")
  blob_hex_filename = GetBlobHexFileName(graph_name, chip_id, y, x)#"#{$PARAMS[:blob_out_dir]}/#{graph_name}_#{chip_id}_#{y}_#{x}.hex"
  if $verbose == 1
    pp ("Outputting hex file #{blob_hex_filename}...")
  end

  epoch_stream_info_total_size, epoch_info_stream_addr = compute_epoch_info_addrs(epoch_info, yx_label, epoch_info_space_start)
  epoch_info_section_size = $epoch_t_struct_padded_size + epoch_stream_info_total_size

  if $verbose == 1
    puts "\n"
    puts "$epoch_t_struct_padded_size: #{$epoch_t_struct_padded_size}"
    puts "epoch_stream_info_total_size: #{epoch_stream_info_total_size}"
    puts "epoch_info_section_size: #{epoch_info_section_size}"
    puts "\n"

    puts "---"
    puts "epoch_t struct for core #{yx_label}"
    puts "start addr = 0x#{epoch_info_space_start.to_s(16)}"
    puts "end addr = 0x#{($epoch_t_struct_padded_size + epoch_info_space_start).to_s(16)}"
    puts "total size = #{$epoch_t_struct_padded_size}"
    puts "contains info from following dict: "
    pp epoch_info[yx_label].reject { |k, v| k == :stream_info }
    puts "\n"
  end

  stream_hex_bin_offset = 0

  stream_info_dws = []
  for stream_id in 0..($noc_num_streams-1)
    if epoch_info[yx_label][:stream_to_info_index][stream_id]
      stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
      stream_info = epoch_info[yx_label][:stream_info][stream_info_index]
      stream_buf_size_tiles = (stream_info[:buf_full_size_bytes] >> 4) / stream_info[:msg_size_words]
      stream_info[:blob_start_absolute_address] = (stream_info[:blob_start_relative_offset] + epoch_info_space_start + epoch_info_section_size)
      if stream_info[:blob_scatter].size() > 0
        stream_info[:blob_scatter].each do |scatter_idx, blob_scatter_elem|
          for unroll_idx in 0..(blob_scatter_elem[:offsets].length()-1)
            blob_scatter_elem[:offsets][unroll_idx] = (blob_scatter_elem[:offsets][unroll_idx] + epoch_info_space_start + epoch_info_section_size)
          end
        end
        blob_scatter_offsets_base = epoch_info_stream_addr[stream_id] + $epoch_stream_info_t_struct_padded_size
        blob_scatter_offsets_size = GetScatterPipeStateAndBlobListPaddedSize(stream_info[:blob_scatter])
      else
        blob_scatter_offsets_base = 0
        blob_scatter_offsets_size = 0
      end

      curr_stream_all_bin_hex_size = 0

      epoch_info[yx_label][:epoch_id] = (stream_info[:start_phase] >> $phase_shift)

      if $verbose == 1
        puts "---"
        puts "stream info for #{yx_label}, stream #{stream_id}"
        puts "start addr = 0x#{stream_info[:blob_start_absolute_address].to_s(16)}"
        pp stream_info
        puts "\n"
      end

      # *** Writing epoch_stream_info_t ***
      if $verbose_debug == 1
        stream_info_dws << "epoch_stream_info_t start for stream " + stream_id.to_s
      end
      stream_info_dws << ((stream_info[:producer_epoch_id] << 16) | stream_id)
      stream_info_dws << (((stream_info[:aliased_stream_id] & 0xFF) << 24) | ((stream_info[:aliased_stream_type] & 0xFF) << 16) | (stream_info[:start_phase] & 0xFFFF))
      stream_info_dws << stream_info[:epoch_num_msgs]
      stream_info_dws << stream_info[:msg_size_words]
      stream_info_dws << stream_buf_size_tiles
      stream_info_dws << stream_info[:buf_full_size_bytes]
      stream_info_dws << stream_info[:buf_base_addr]
      stream_info_dws << ((stream_info[:space_shared_with_operand] ? stream_info[:space_shared_with_operand] << 24 : 0) | (stream_info[:start_phase_num_cfg_regs] << 16) | 
                          (stream_info[:num_msgs_in_block] ? stream_info[:num_msgs_in_block] : 0))
      stream_info_dws << stream_info[:msg_info_buf_start_addr]
      stream_info_dws << stream_info[:blob_start_absolute_address]
      stream_info_dws << stream_info[:blob_size]
      stream_info_dws << (stream_info[:num_iters_in_epoch] * (stream_info[:is_scatter_pack] ? stream_info[:num_unroll_iter]*stream_info[:padding_scatter_order_size] : 1))
      stream_info_dws << (stream_info[:num_iters_in_epoch] * (stream_info[:is_scatter_pack] ? stream_info[:num_unroll_iter]*stream_info[:padding_scatter_order_size] : 1))
      stream_info_dws << stream_info[:num_scatter_inner_loop]
      stream_info_dws << ((stream_info[:num_mblock_buffering] ? stream_info[:num_mblock_buffering] << 16 : 1 << 16) | 
                          (stream_info[:eth_remote_fw_stream_id] ? stream_info[:eth_remote_fw_stream_id] << 8 : 0 << 8) | 
                          (stream_info[:legacy_pack] ? 1 : 0))
      stream_info_dws << stream_info[:flags]
      stream_info_dws << ((stream_info[:total_strides] << 16) | stream_info[:stride])
      stream_info_dws << (stream_info[:stride_offset_size_bytes] ? stream_info[:stride_offset_size_bytes] : 0)
      stream_info_dws << (stream_info[:skip_col_bytes] ? stream_info[:skip_col_bytes] : stream_info[:pipe_scatter_output_loop_count])
      stream_info_dws << (stream_info[:skip_col_tile_row_bytes] ? stream_info[:skip_col_tile_row_bytes] : blob_scatter_offsets_base)
      stream_info_dws << (stream_info[:skip_col_row_bytes] ? stream_info[:skip_col_row_bytes] : 0)
      stream_info_dws << (stream_info[:skip_zcol_bytes] ? stream_info[:skip_zcol_bytes] : 0)
      stream_info_dws << (stream_info[:skip_col_zrow_bytes] ? stream_info[:skip_col_zrow_bytes] : stream_info[:num_iter_tiles])
      stream_info_dws << (((stream_info[:r_dim_size] ? stream_info[:r_dim_size] : 0) << 16) | (stream_info[:c_dim_size] ? stream_info[:c_dim_size] : 0))
      stream_info_dws << (((stream_info[:zr_dim_size] ? stream_info[:zr_dim_size] : 0) << 16) | (stream_info[:zc_dim_size] ? stream_info[:zc_dim_size] : 0))

      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0
      stream_info_dws << 0

      num_dram_bufs = 0
      if stream_info[:dram_stream]
        num_dram_bufs = stream_info[:dram_stream_buf_list].length
      end
      untilize_copy_iters = 0
      log_2x_untilize_copy_iters = 0
      if stream_info[:moves_raw_data] && stream_info[:tile_dim_r]
        untilize_copy_iters = stream_info[:tile_dim_r] / 2
        log_2x_untilize_copy_iters = Math.log2(stream_info[:tile_dim_r]).round
      end
      stream_info_dws << ((num_dram_bufs << 16) | (log_2x_untilize_copy_iters << 8) | (untilize_copy_iters << 0))
      stream_info_dws << (stream_info[:dram_stream] ? epoch_info_stream_addr[stream_id] + $epoch_stream_info_t_struct_padded_size + blob_scatter_offsets_size : 0)
      stream_info_dws << ((stream_info[:num_fork_streams] ? stream_info[:num_fork_streams] : 0) | (stream_info[:padding_scatter_order_size] ? stream_info[:padding_scatter_order_size] << 16 : 0))
      if (stream_info[:fork_stream_ids] && stream_info[:fork_stream_ids].length > $epoch_max_output_forks)
        abort("Error! fork_stream_ids exceeds max fork allowed for #{yx_label}, stream_id=#{stream_id}")
      end
      if (stream_info[:fork_stream_ids] && stream_info[:fork_stream_ids].length > 0)
        fork_idxs = []
        count = 0
        fork_stream_ids_idx = 0
        for idx in 0..($epoch_max_output_forks-1)
          active_info_idx = 0
          if fork_stream_ids_idx < stream_info[:fork_stream_ids].length
            fork_stream_id = stream_info[:fork_stream_ids][fork_stream_ids_idx]
            for other_stream_id in 0..($noc_num_streams-1)
              if epoch_info[yx_label][:stream_to_info_index][other_stream_id]
                if other_stream_id == fork_stream_id
                  break
                end
                active_info_idx = active_info_idx + 1
              end
            end
            fork_stream_ids_idx = fork_stream_ids_idx + 1
          end
          fork_idxs << active_info_idx.to_s(16).rjust(2, "0")
          count = count + 1
          if (count == 4)
            stream_info_dws << fork_idxs.reverse.join().to_i(16)
            count = 0
            fork_idxs = []
          end
        end
      else
        stream_info_dws << 0
        stream_info_dws << 0
        stream_info_dws << 0
        stream_info_dws << 0
      end
      if $verbose_debug == 1
        stream_info_dws << "epoch_stream_info_t padding start"
      end
      # Pad to achieve alignment.
      $epoch_stream_info_t_struct_padding.times do
        stream_info_dws << 0
      end
      if $verbose_debug == 1
        stream_info_dws << "epoch_stream_info_t end "
      end
      # *** End of writing epoch_stream_info_t ***

      # If stream is_scatter_pack and scatter_order_size > 1, additional info is written.
      # Example
      # :blob_scatter=>
      #  {0=>
      #    {:offsets=>
      #      [168,
      #       480,
      #       780,
      #       ...
      #       4980,
      #       5280,
      #       5580,
      #       5880],
      #     :phase_num_cfg_regs=>
      #      [14,
      #       11,
      #       11,
      #       ...
      #       11,
      #       11],
      #     :num_unroll_iter=>20},
      #   1=>
      #    {:offsets=>
      #      [280,
      #       580,
      #       880,
      #       ...
      #       5980],
      #     :phase_num_cfg_regs=>
      #      [11,
      #       11,
      #       11,
      #       ...
      #       11],
      #     :num_unroll_iter=>20},
      #
      if stream_info[:blob_scatter].size() > 0
        # *** Writing scatter_pipe_blob_t and scatter_pipe_state_t ***
        if $verbose_debug == 1
          stream_info_dws << "scatter_pipe_blob_t and scatter_pipe_state_t start"
        end
        full_slots = 0
        scatter_pipe_state_t_array_base = blob_scatter_offsets_base + stream_info[:blob_scatter].size() * 16
        stream_info[:blob_scatter].each do |scatter_idx, blob_scatter_elem|
          stream_info_dws << 0
          stream_info_dws << blob_scatter_elem[:num_unroll_iter]
          stream_info_dws << 0
          stream_info_dws << scatter_pipe_state_t_array_base
          scatter_pipe_state_t_array_base = scatter_pipe_state_t_array_base + blob_scatter_elem[:offsets].length() * 8
          full_slots = full_slots + 16
        end
        stream_info[:blob_scatter].each do |scatter_idx, blob_scatter_elem|
          for unroll_idx in 0..(blob_scatter_elem[:offsets].length()-1)
            stream_info_dws << blob_scatter_elem[:offsets][unroll_idx]
            stream_info_dws << blob_scatter_elem[:phase_num_cfg_regs][unroll_idx]
            full_slots = full_slots + 8
          end
        end
        if $verbose_debug == 1
          stream_info_dws << "scatter_pipe_blob_t and scatter_pipe_state_t padding start"
        end
        for count in (full_slots/4)..(blob_scatter_offsets_size/4-1)
          stream_info_dws << 0
        end
        # *** End of writing scatter_pipe_blob_t and scatter_pipe_state_t ***
        if $verbose_debug == 1
          stream_info_dws << "scatter_pipe_blob_t and scatter_pipe_state_t end"
        end
      end

      # If stream has NCRISC config assigned, additional info is written.
      if stream_info[:dram_stream]        
        dram_buf = stream_info[:dram_stream_buf_list][0]
        if stream_info[:moves_raw_data]
          epoch_q_slots_remaining = stream_info[:epoch_num_msgs] / (dram_buf[:dram_q_slot_size_tiles] / stream_info[:total_strides])
        elsif dram_buf[:dram_scatter_offsets].length() > 0
          epoch_q_slots_remaining = 0
          epoch_num_msgs = stream_info[:epoch_num_msgs]
          num_scatter_offsets = 0
          if (stream_info[:scatter_list_stream_id] && stream_info[:scatter_list_stream_id] != 0)
            num_scatter_offsets = stream_info[:scatter_list_indicies_per_input]
          else
            num_scatter_offsets = dram_buf[:dram_scatter_offsets_full_size]
            if stream_info[:hw_tilize]
              num_scatter_offsets = stream_info[:c_dim_size] * num_scatter_offsets
            end
          end
          dram_q_slot_size_tiles = num_scatter_offsets * dram_buf[:dram_scatter_chunk_size_tiles] * (stream_info[:hw_tilize] ? 1 : dram_buf[:num_scatter_inner_loop])
          while (epoch_num_msgs > 0) do
            epoch_num_msgs -= dram_q_slot_size_tiles
            if (epoch_num_msgs < 0)
              abort("Error! Could not compute epoch_q_slots_remaining for #{yx_label}, stream_id=#{stream_id}.")
            end
            epoch_q_slots_remaining += 1
          end
        else
          epoch_q_slots_remaining = stream_info[:epoch_num_msgs] / (dram_buf[:dram_q_slot_size_tiles] / stream_info[:total_strides])
        end

        has_multi_readers = false
        for dram_buf_id in 0..(stream_info[:dram_stream_buf_list].length()-1)
          has_multi_readers = has_multi_readers || stream_info[:dram_stream_buf_list][dram_buf_id][:total_readers] > 1
        end

        # *** Writing epoch_stream_dram_io_info_t ***
        if $verbose_debug == 1
          stream_info_dws << "epoch_stream_dram_io_info_t start"
        end
        stream_info_dws << dram_buf[:dram_q_slot_size_bytes]
        stream_info_dws << (((has_multi_readers ? 1 : 0) << 24) | ((stream_info[:dram_output_no_push] ? 1 : 0) << 16) | (stream_info[:outgoing_data_noc] << 8) | stream_info[:incoming_data_noc])
        if (stream_info[:scatter_list_stream_id] && stream_info[:scatter_list_stream_id] != 0)
          stream_info_dws << ((stream_info[:scatter_list_indicies_per_tile] << 16) | stream_info[:scatter_list_stream_id])
          stream_info_dws << stream_info[:scatter_list_indicies_per_input]
        else
          stream_info_dws << ((stream_info[:c_dim_loop_num_rows] << 16) | stream_info[:scatter_list_stream_id])
          stream_info_dws << stream_info[:tilize_row_col_offset]
        end
        stream_info_dws << stream_info[:dram_embeddings_row_shift]
        stream_info_dws << epoch_q_slots_remaining
        start_dram_state_ptr = epoch_info_stream_addr[stream_id] + $epoch_stream_info_t_struct_padded_size + blob_scatter_offsets_size + $epoch_stream_dram_io_info_t_struct_padded_size
        curr_dram_state_ptr = start_dram_state_ptr
        stream_info_dws << (((stream_info[:hw_tilize] ? 1 : 0) << 24) | ((stream_info[:dram_writes_with_cmd_buf] ? 1 : 0) << 16) | stream_info[:dram_embeddings_row_tiles])
        stream_info_dws << curr_dram_state_ptr
        
        # Pad with zeros to achieve alignment.
        if $verbose_debug == 1
          stream_info_dws << "epoch_stream_dram_io_info_t padding start"
        end
        $epoch_stream_dram_io_info_t_struct_padding.times do
          stream_info_dws << 0
        end
        if $verbose_debug == 1
          stream_info_dws << "epoch_stream_dram_io_info_t end"
        end
        # *** End of writing epoch_stream_dram_io_info_t ***

        # Iterate per NCRISC config.
        for dram_buf_id in 0..(stream_info[:dram_stream_buf_list].length()-1)
          dram_buf = stream_info[:dram_stream_buf_list][dram_buf_id]
          dram_scatter_struct_ptr = dram_buf[:dram_scatter_offsets].length() > 0 ? curr_dram_state_ptr + $dram_io_state_t_struct_padded_size : 0
          dram_scatter_offset_ptr = curr_dram_state_ptr + $dram_io_state_t_struct_padded_size + $dram_io_scatter_state_t_struct_padded_size
          dram_scatter_offsets_size_bytes = GetDramScatterOffsetsListPaddedSize(dram_buf[:dram_scatter_offsets].length())
          if (dram_buf_id == stream_info[:dram_stream_buf_list].length()-1)
            # There is no next dram_io_state_t
            curr_dram_state_ptr = 0
          else
            curr_dram_state_ptr += $dram_io_state_t_struct_padded_size + (dram_buf[:dram_scatter_offsets].length() > 0 ? dram_scatter_offsets_size_bytes + $dram_io_scatter_state_t_struct_padded_size : 0)
          end
          if dram_buf[:dram_output]
            data_chunk_size_tiles = dram_buf[:dram_buf_write_chunk_size_tiles]
          else
            data_chunk_size_tiles = dram_buf[:dram_buf_read_chunk_size_tiles]
          end
          if stream_info[:moves_raw_data]
            data_chunk_size_bytes = dram_buf[:dram_buf_write_row_chunk_size_bytes]
          else
            data_chunk_size_bytes = data_chunk_size_tiles * stream_info[:msg_size_words] * 16
          end
          if stream_info[:moves_raw_data]
            dram_q_slot_size_tiles = stream_info[:batch_dim_size]
          elsif dram_buf[:dram_scatter_offsets].length() > 0
            num_scatter_offsets = 0
            if (stream_info[:scatter_list_stream_id] && stream_info[:scatter_list_stream_id] != 0)
              num_scatter_offsets = stream_info[:scatter_list_indicies_per_input]
            else
              num_scatter_offsets = dram_buf[:dram_scatter_offsets_full_size]
              if stream_info[:hw_tilize]
                num_scatter_offsets = stream_info[:c_dim_size] * num_scatter_offsets
              end
            end
            dram_q_slot_size_tiles = num_scatter_offsets * dram_buf[:dram_scatter_chunk_size_tiles] * (stream_info[:hw_tilize] ? 1 : dram_buf[:num_scatter_inner_loop])
          else
            dram_q_slot_size_tiles = dram_buf[:dram_q_slot_size_tiles]
          end

          dram_streaming_header_addr = 0
          if dram_buf[:dram_output] && dram_buf[:dram_streaming]
            dest_chip_id, dest_y, dest_x, dest_stream_id = ParseStreamString(dram_buf[:dram_streaming_dest].to_s)
            dest_yx_label = :"chip_#{dest_chip_id}__y_#{dest_y}__x_#{dest_x}"
            dest_stream_info_index = epoch_info[dest_yx_label][:stream_to_info_index][dest_stream_id]
            dest_stream_info = epoch_info[dest_yx_label][:stream_info][dest_stream_info_index]
            dest_epoch_info_space_start = IsEthernetYX(dest_y,dest_x) ? $PARAMS[:blob_section_start_eth] : $PARAMS[:blob_section_start]
            dest_epoch_stream_info_offset, dest_epoch_info_stream_addr = compute_epoch_info_addrs(epoch_info, dest_yx_label, dest_epoch_info_space_start)
            dest_blob_scatter_offsets_size = dest_stream_info[:blob_scatter].size() > 0 ? GetScatterPipeStateAndBlobListPaddedSize(dest_stream_info[:blob_scatter]) : 0
            dram_streaming_header_addr = dest_epoch_info_stream_addr[dest_stream_id] + $epoch_stream_info_t_struct_padded_size + dest_blob_scatter_offsets_size + $epoch_stream_dram_io_info_t_struct_padded_size
            dram_streaming_header_addr = append_mmio_pipe_coordinates(dram_buf[:dram_buf_noc_addr], dram_streaming_header_addr)
          end

          # *** Writing dram_io_state_t ***
          # *** Writing dram_io_state_t::dram_to_l1_t ***
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::dram_to_l1_t start"
          end
          stream_info_dws << 0  # rd_dram_rdptr
          stream_info_dws << 0  # rd_dram_wrptr
          stream_info_dws << 0  # rd_epoch_id_tag
          stream_info_dws << 0  # rd_stride
          stream_info_dws << 0  # rd_rd_wr_ptr_autoinc
          stream_info_dws << 0  
          stream_info_dws << (dram_buf[:dram_input] && dram_buf[:dram_streaming] ? (stream_info[:start_phase] >> $phase_shift) : 0)
          stream_info_dws << 0  # dram_streaming_tag
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::dram_to_l1_t padding start"
          end
          # Pad to achieve alignment.
          $dram_io_state_t_dram_to_l1_struct_padding.times do
            stream_info_dws << 0
          end
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::dram_to_l1_t end"
          end
          # *** End of writing dram_io_state_t::dram_to_l1_t ***
          
          # *** Writing dram_io_state_t::l1_to_dram_t ***
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::l1_to_dram_t start"
          end
          stream_info_dws << 0  # wr_dram_rdptr
          stream_info_dws << 0  # wr_dram_wrptr
          stream_info_dws << 0  # wr_epoch_id_tag
          stream_info_dws << 0  # wr_stride
          stream_info_dws << (dram_streaming_header_addr & 0xFFFFFFFF)
          stream_info_dws << (dram_streaming_header_addr >> 32)
          stream_info_dws << data_chunk_size_bytes
          stream_info_dws << (((dram_buf[:dram_padding] ? 1 : 0) << 16) | data_chunk_size_tiles)
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::l1_to_dram_t padding start"
          end
          # Pad to achieve alignment.
          $dram_io_state_t_l1_to_dram_struct_padding.times do
            stream_info_dws << 0
          end
          if $verbose_debug == 1
            stream_info_dws << "dram_io_state_t::l1_to_dram_t end"
          end
          # *** End of writing dram_io_state_t::l1_to_dram_t ***

          # *** Writing rest of dram_io_state_t ***
          if $verbose_debug == 1
            stream_info_dws << "rest of dram_io_state_t start"
          end
          stream_info_dws << dram_buf[:dram_buf_size_bytes]
          stream_info_dws << dram_buf[:dram_buf_size_q_slots]
          stream_info_dws << (dram_buf[:dram_buf_noc_addr] & 0xFFFFFFFF)
          stream_info_dws << (dram_buf[:dram_buf_noc_addr] >> 32)
          stream_info_dws << dram_q_slot_size_tiles
          stream_info_dws << ((dram_buf[:total_readers] << 8) | dram_buf[:reader_index]) # {stride_wrap, unused, total_readers, reader_index}
          stream_info_dws << dram_scatter_struct_ptr
          stream_info_dws << curr_dram_state_ptr
          if $verbose_debug == 1
            stream_info_dws << "rest of dram_io_state_t padding start"
          end
          # Pad to achieve alignment.
          $dram_io_state_t_rest_of_struct_padding.times do
            stream_info_dws << 0
          end
          if $verbose_debug == 1
            stream_info_dws << "rest of dram_io_state_t end"
          end
          # *** End of writing rest of dram_io_state_t ***

          if (dram_buf[:dram_scatter_offsets].length() > 0)
            # *** Writing dram_io_scatter_state_t ***
            if $verbose_debug == 1
              stream_info_dws << "dram_io_scatter_state_t start"
            end
            stream_info_dws << 0 # scatter_offsets_index
            stream_info_dws << dram_buf[:dram_scatter_offsets].length()
            stream_info_dws << dram_buf[:dram_scatter_chunk_size_bytes]
            stream_info_dws << dram_buf[:dram_q_slot_size_bytes]
            stream_info_dws << dram_buf[:dram_scatter_chunk_size_tiles]
            stream_info_dws << 0 # unused2
            stream_info_dws << 0 # unused3
            stream_info_dws << dram_scatter_offset_ptr
            if $verbose_debug == 1
              stream_info_dws << "dram_io_scatter_state_t padding start"
            end
            # Pad to achieve alignment.
            $dram_io_scatter_state_t_struct_padding.times do
              stream_info_dws << 0
            end
            if $verbose_debug == 1
              stream_info_dws << "dram_io_scatter_state_t end"
            end
            # *** End of writing dram_io_scatter_state_t ***
            
            # *** Writing dram scatter offsets ***
            if $verbose_debug == 1
              stream_info_dws << "dram scatter offsets start"
            end
            if stream_info[:hw_tilize]
              for dram_scatter_offsets_id in 0..(dram_buf[:dram_scatter_offsets].length()-1)
                stream_info_dws << dram_buf[:dram_scatter_offsets][dram_scatter_offsets_id]
              end
              if $verbose_debug == 1
                stream_info_dws << "dram scatter offsets padding start"
              end
              for dram_scatter_offsets_id in dram_buf[:dram_scatter_offsets].length()..(dram_scatter_offsets_size_bytes/4-1)
                stream_info_dws << 0
              end
            else
              for dram_scatter_offsets_id in 0..(dram_buf[:dram_scatter_offsets].length()-1)
                stream_info_dws << (dram_buf[:dram_scatter_offsets][dram_scatter_offsets_id] & 0xFFFFFFFF)
                stream_info_dws << (dram_buf[:dram_scatter_offsets][dram_scatter_offsets_id] >> 32)
              end
              if $verbose_debug == 1
                stream_info_dws << "dram scatter offsets padding start"
              end
              for dram_scatter_offsets_id in dram_buf[:dram_scatter_offsets].length()..(dram_scatter_offsets_size_bytes/8-1)
                stream_info_dws << 0
                stream_info_dws << 0
              end
            end
            if $verbose_debug == 1
              stream_info_dws << "dram scatter offsets end"
            end
            # *** End of writing dram scatter offsets ***
          end
        end
      end
      stream_hex_bin_offset += curr_stream_all_bin_hex_size
    end
  end

  # TODO Some intermediates stuff.
  streams.each do |stream_id, phases|
    stream_info_index = epoch_info[yx_label][:stream_to_info_index][stream_id]
    stream_info = epoch_info[yx_label][:stream_info][stream_info_index]
    phases.each do |phase_num, phase|
      if phase[:intermediate]      
        # infinite loop for intermediates - last word needs to be added here after we've computed the absolute blob address
        phase[:cfg_dw_list_0].pop
        phase[:cfg_dw_list_0] << intermediates_blob_loop_dw(stream_info[:blob_start_absolute_address])
        if $verbose == 1
          puts "#{yx_label}, stream=#{stream_id}, phase=#{phase_num} -> intermediate phase, looping blob back to address 0x#{stream_info[:blob_start_absolute_address].to_s(16)}"
        end
      end
    end
  end

  # The first line writes epoch_t, the second writes all other structures
  epoch_t_dws = PopulateEpochTStruct(epoch_info[yx_label], 1, yx_label, perf_blobs, epoch_info_stream_addr, global_info_blob)
  epoch_t_dws += stream_info_dws

  if $verbose_debug == 1
    puts "---"
    puts "epoch_t_dws content"
    pp epoch_t_dws
  end

  # Writing hex data in overlay blob.
  addr_marker = (epoch_info_space_start >> 2).to_s(16).rjust(8, "0")
  blob_hex_sections[epoch_info_space_start] = []
  if $verbose == 1
    pp "   Section 0x#{epoch_info_space_start.to_s(16)} -> epoch info"
  end
  epoch_t_dws.each do |dw|
    blob_hex_sections[epoch_info_space_start] << dw
  end
  
  blob_space_start = epoch_info_space_start + epoch_info_section_size
  curr_blob_addr = blob_space_start
  blob_hex_sections[blob_space_start] = []
  if $verbose == 1
    pp "   Section 0x#{blob_space_start.to_s(16)} -> stream blob"
  end
  streams.each do |stream_id, phases|
    if $verbose_debug == 1
      blob_hex_sections[blob_space_start] << "Writing blob hex for stream " + stream_id.to_s
    end
    first_phase_key, first_phase = phases.first
    num_mblock_buffering = first_phase[:num_mblock_buffering] ? first_phase[:num_mblock_buffering] : 1
    for mblock_buffering_idx in 0..(num_mblock_buffering-1) do
      phases.each do |phase_num, phase|
        header_dw = phase[:cfg_header_dw]
        blob_hex_sections[blob_space_start] << header_dw
        if $verbose == 1
          expected_next_addr = curr_blob_addr + 4*(phase[:"cfg_dw_list_#{mblock_buffering_idx}"].length + 1)
          pp "        Addr 0x#{curr_blob_addr.to_s(16)} -> stream #{stream_id}, buffer_idx #{mblock_buffering_idx}, phase #{phase_num}, header_dw = #{header_dw.to_s(16)}, num regs = #{phase[:"cfg_dw_list_#{mblock_buffering_idx}"].length + 1}, expected next address = #{expected_next_addr.to_s(16)}"
        end
        curr_blob_addr = curr_blob_addr + 4
        phase[:"cfg_dw_list_#{mblock_buffering_idx}"].each do |cfg_dw|
          blob_hex_sections[blob_space_start] << cfg_dw
          curr_blob_addr = curr_blob_addr + 4
        end
        if phase[:preload_data]
          buf_addr = phase[:buf_addr]
          blob_hex_sections[buf_addr] = []
          if $verbose == 1
            pp "    Section 0x#{buf_addr.to_s(16)} -> stream #{stream_id}, phase #{phase_num} preload data"
          end
          phase[:preload_data].each do |seed_dw|
            dw_index = 0
            dw = seed_dw
            msg_size_dw = phase[:msg_size] / 4
            for dw_index in 0..(msg_size_dw-1)
              dw_m = (dw_index == 0) ? GetFirstMsgDW(dw, phase[:msg_size]) : dw
              blob_hex_sections[buf_addr] << dw_m            
              dw = GetNextPreloadDW(dw)
            end
          end      
        end
      end
      phase_nums = phases.keys.sort
      num_phases = phases.keys.length
      last_phase = phases[phase_nums[num_phases-1]]
      last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"].each_with_index do |blob_cfg, blob_idx|
        is_last_phase = blob_idx+1 >= last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"].length()
        auto_cfg_size = !is_last_phase ? last_phase[:"cfg_dw_list_dummy_#{mblock_buffering_idx}"][blob_idx+1].length() : 0
        header_dw = blob_header_dw(auto_cfg_size, 1, 0)
        blob_hex_sections[blob_space_start] << header_dw
        if $verbose == 1
          expected_next_addr = curr_blob_addr + 4*(blob_cfg.length + 1)
          pp "        Addr 0x#{curr_blob_addr.to_s(16)} -> stream #{stream_id}, buffer_idx #{mblock_buffering_idx}, phase dummy, header_dw = #{header_dw.to_s(16)}, num regs = #{blob_cfg.length + 1}, expected next address = #{expected_next_addr.to_s(16)}"
        end
        curr_blob_addr = curr_blob_addr + 4
        blob_cfg.each do |cfg_dw|
          blob_hex_sections[blob_space_start] << (is_last_phase ? set_auto_cfg(cfg_dw, 0) : cfg_dw)
          curr_blob_addr = curr_blob_addr + 4
        end
      end
    end
    if $verbose_debug == 1
      blob_hex_sections[blob_space_start] << "End of writing blob hex for stream " + stream_id.to_s
    end
  end
  overlay_blob_size = IsEthernetYX(y,x) ? $PARAMS[:overlay_blob_size_eth] : $PARAMS[:overlay_blob_size] + epoch_info[yx_label][:overlay_blob_extra_size]
  puts("Info: Overlay Blob size for (e:#{epoch_info[yx_label][:epoch_id]},c:#{chip_id},y:#{y},x:#{x}) is #{curr_blob_addr - epoch_info_space_start}")
  if curr_blob_addr - epoch_info_space_start > overlay_blob_size
    abort("Error! The overlay blob for #{yx_label} (e:#{epoch_info[yx_label][:epoch_id]},c:#{chip_id},y:#{y},x:#{x}) does not fit, the max size is #{overlay_blob_size}, however we tried to allocate #{curr_blob_addr - epoch_info_space_start}.")
  end
  WriteBlobHexFile(blob_hex_filename, blob_hex_sections)
end

ValidateDataAndTileHeaderBuffersAreNonOverlapping(phase_info)

ending_time = Process.clock_gettime(Process::CLOCK_MONOTONIC)
if $verbose == 1
  pp "Time Elapse for blob_gen.rb: #{ending_time-starting_time}s"
end

## Emit invalid epochs/blobs for unused cores
for chip_id in chip_ids
  for r in 0..chip_ids_to_grid_size_y[chip_id]
    for c in 0..chip_ids_to_grid_size_x[chip_id]
      if not used_cores.include?(:"chip_#{chip_id}__y_#{r}__x_#{c}")
        if not WORKER_CORE_OVERRIDE_ENABLED or (WORKER_CORE_OVERRIDE_ENABLED and (IsWorkerYX(r,c) or IsEthernetYX(r,c)))
          epoch_t_dws = PopulateInvalidEpochTStruct(chip_id, r, c)

          epoch_info_space_start = IsEthernetYX(r,c) ? $PARAMS[:blob_section_start_eth] : $PARAMS[:blob_section_start]

          blob_hex_sections = {}
          blob_hex_sections[epoch_info_space_start] = []
          epoch_t_dws.each do |dw|
            blob_hex_sections[epoch_info_space_start] << dw
          end
          
          blob_hex_filename = GetBlobHexFileName(graph_name, chip_id, r, c)#"#{$PARAMS[:blob_out_dir]}/#{graph_name}_#{chip_id}_#{y}_#{x}.hex"
          if $verbose == 1
            pp ("Outputting hex file #{blob_hex_filename} for UNUSED core...")
          end
          WriteBlobHexFile(blob_hex_filename, blob_hex_sections)
        end

      end
    end
  end
end

# Yaml dumping is slow, only enable for debug
#yaml_phase_info = File.open("#{$PARAMS[:blob_out_dir]}/#{graph_name}_phase_info.yaml", "w")
#yaml_phase_info.puts YAML::dump(phase_info) 
#yaml_phase_info.close

# Yaml dumping is slow, only enable for debug
#yaml_graph = File.open("#{$PARAMS[:blob_out_dir]}/#{graph_name}_graph.yaml", "w")
#yaml_graph.puts YAML::dump($tt_overlay_graphs[graph_name]) 
#yaml_graph.close

#end
