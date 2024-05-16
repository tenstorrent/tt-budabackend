# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

require "#{$PARAMS[:blob_gen_root]}/#{$PARAMS[:chip]}_params.rb"

####

def NOC_ID_MASKED(noc_id) 
  return ((1 << NOC_ID_WIDTH) - 1) & noc_id
end

def ETH_NOC_ID()
  return NOC_ID_MASKED(-1)
end

def IS_ETH_NOC_ID(noc_id)
  return ETH_NOC_ID() == NOC_ID_MASKED(noc_id)
end

def STREAM_CFG(field, val)
  return val << field
end

def DUMMY_PHASE(phase_num)
  epoch_num = phase_num >> $phase_shift
  return (0x1F << $epoch_shift) | (epoch_num & $wrapped_phase_mask)
end

def STREAM_REMOTE_SRC(src_x, src_y, src_stream_id, dest_index) 
  return ((src_x << STREAM_REMOTE_SRC_X) | (src_y << STREAM_REMOTE_SRC_Y) | (src_stream_id << REMOTE_SRC_STREAM_ID) | (dest_index << STREAM_REMOTE_SRC_DEST_INDEX))
end

def STREAM_REMOTE_DEST(dest_x, dest_y, dest_stream_id)
  return ((dest_x << STREAM_REMOTE_DEST_X) | (dest_y << STREAM_REMOTE_DEST_Y) | (dest_stream_id << STREAM_REMOTE_DEST_STREAM_ID))
end

def STREAM_LOCAL_DEST(dest_stream_id, clear_num)
  return ((clear_num << STREAM_LOCAL_DEST_MSG_CLEAR_NUM) | (dest_stream_id << STREAM_LOCAL_DEST_STREAM_ID))
end

def STREAM_MCAST_DEST_V1(mcast_en, dest_end_x, dest_end_y, arb_group_size, src_in_order_fwd, linked, vc, no_path_res, stream_mcast_xy)
  if (mcast_en != 0) && (vc == 1)
    throw ("found phase with mcast_en=#{mcast_en} and vc=#{vc} ->  VC 4 is used for stream multicast, VC 5 is repurposed for unicast write to DRAM")
  end
  return ((mcast_en << STREAM_MCAST_EN) |
          (dest_end_x << STREAM_MCAST_END_X) |
          (dest_end_y << STREAM_MCAST_END_Y) |
          (arb_group_size << MSG_ARB_GROUP_SIZE) |
          (src_in_order_fwd << MSG_SRC_IN_ORDER_FWD) |
          (linked << STREAM_MCAST_LINKED) |
          (no_path_res << STREAM_MCAST_NO_PATH_RES) |
          (stream_mcast_xy << STREAM_MCAST_XY) |
          (vc << STREAM_MCAST_VC))
end

def STREAM_MCAST_DEST_V2(mcast_en, dest_end_x, dest_end_y, linked, vc, no_path_res, stream_mcast_xy)
  if (mcast_en != 0) && (vc == 1)
    throw ("found phase with mcast_en=#{mcast_en} and vc=#{vc} ->  VC 4 is used for stream multicast, VC 5 is repurposed for unicast write to DRAM")
  end
  return ((mcast_en << STREAM_MCAST_EN) |
          (dest_end_x << STREAM_MCAST_END_X) |
          (dest_end_y << STREAM_MCAST_END_Y) |
          (linked << STREAM_MCAST_LINKED) |
          (no_path_res << STREAM_MCAST_NO_PATH_RES) |
          (stream_mcast_xy << STREAM_MCAST_XY) |
          (vc << STREAM_MCAST_VC))
end

def STREAM_GATHER(chip, arb_group_size, src_in_order_fwd, local_stream_clear_num, msg_group_stream_clear_type)
  if chip == "blackhole"
    return ((src_in_order_fwd << MSG_SRC_IN_ORDER_FWD) |
          (arb_group_size << MSG_ARB_GROUP_SIZE) |
          (msg_group_stream_clear_type << MSG_GROUP_STREAM_CLEAR_TYPE) |
          (local_stream_clear_num << MSG_LOCAL_STREAM_CLEAR_NUM))
  else
    return ((arb_group_size << MSG_ARB_GROUP_SIZE) |
          (src_in_order_fwd << MSG_SRC_IN_ORDER_FWD))
  end
end

def STREAM_GATHER_CLEAR(local_stream_clear_num, msg_group_stream_clear_type) 
  return ((local_stream_clear_num << MSG_LOCAL_STREAM_CLEAR_NUM) |
          (msg_group_stream_clear_type << MSG_GROUP_STREAM_CLEAR_TYPE))
end

def STREAM_PERF_CONFIG(clock_gating_en, clock_gating_hyst, partial_send_words_thr)
  return (((clock_gating_en & ((1 << CLOCK_GATING_EN_WIDTH)-1)) << CLOCK_GATING_EN) |
          ((clock_gating_hyst & ((1 << CLOCK_GATING_HYST_WIDTH)-1)) << CLOCK_GATING_HYST) |
          ((partial_send_words_thr & ((1 << PARTIAL_SEND_WORDS_THR_WIDTH)-1)) << PARTIAL_SEND_WORDS_THR))
end

def STREAM_MSG_HEADER_FORMAT(offset, bits, last_tile_offset)
  return ((offset << MSG_HEADER_WORD_CNT_OFFSET) | (bits << MSG_HEADER_WORD_CNT_BITS) | (last_tile_offset << MSG_HEADER_INFINITE_PHASE_LAST_TILE_OFFSET))
end

def STREAM_REMOTE_BUF_SPACE(words, msgs)
  return ((words << REMOTE_DEST_BUF_SIZE_WORDS) | (msgs << REMOTE_DEST_BUF_SIZE_MSGS))
end
  
def STREAM_SOURCE_ENDPOINT_NEW_MSG(chip, addr, size, last_tile)
  if chip == "blackhole"
    return ((addr << SOURCE_ENDPOINT_NEW_MSG_ADDR) | (size << SOURCE_ENDPOINT_NEW_MSG_SIZE) | (last_tile << SOURCE_ENDPOINT_NEW_MSG_LAST_TILE))
  else
    return ((addr << SOURCE_ENDPOINT_NEW_MSG_ADDR) | (size << SOURCE_ENDPOINT_NEW_MSG_SIZE))
  end
end
  
def STREAM_SOURCE_ENDPOINT_MEM_BUF_NEW_MSGS_DATA(chip, msgs, words, last_tile)
  if chip == "blackhole"
    return ((msgs << SOURCE_ENDPOINT_NEW_MSGS_NUM) | (words << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE) |(last_tile << SOURCE_ENDPOINT_NEW_MSGS_LAST_TILE))
  else
    return ((msgs <<  SOURCE_ENDPOINT_NEW_MSGS_NUM) | (words << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE))
  end
end

def STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE(dest_num, words_free_inc)
  ((dest_num << REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM) | (words_free_inc << REMOTE_DEST_BUF_WORDS_FREE_INC))
end

def NOC1_X_ID(id)
  if ($PARAMS[:noc_translation_id_enabled] == 1 && id >= 16)
    return id
  else
    return ($PARAMS[:noc_x_size]-1-id)
  end
end


def NOC1_Y_ID(id)
  if ($PARAMS[:noc_translation_id_enabled] == 1 && id >= 16)
    return id
  else
    return ($PARAMS[:noc_y_size]-1-id)
  end
end

def modify_DRAM_NOC_addr(dram_addr, noc_id)
  addr = dram_addr & ((1 << NOC_ADDR_LOCAL_WIDTH) - 1)
  noc_x = (dram_addr >> NOC_ADDR_LOCAL_WIDTH) & ((1 << NOC_ID_WIDTH) - 1)
  noc_y = (dram_addr >> (NOC_ADDR_LOCAL_WIDTH+NOC_ID_WIDTH)) & ((1 << NOC_ID_WIDTH) - 1)
  rest = dram_addr >> (NOC_ADDR_LOCAL_WIDTH+NOC_ID_WIDTH+NOC_ID_WIDTH)

  if noc_id == 1
    noc_x = NOC1_X_ID(noc_x)
    noc_y = NOC1_Y_ID(noc_y)
  end

  return (rest << (NOC_ADDR_LOCAL_WIDTH+NOC_ID_WIDTH+NOC_ID_WIDTH)) | (noc_y << (NOC_ADDR_LOCAL_WIDTH+NOC_ID_WIDTH)) | (noc_x << (NOC_ADDR_LOCAL_WIDTH)) | (addr)
end

def append_mmio_pipe_coordinates(dram_buf_addr, dram_header_addr)
  shift = Math.log2($PARAMS[:tensix_mem_size]).ceil
  coords = (dram_buf_addr >> shift) << shift

  return (coords | dram_header_addr)
end

####

def blob_cfg_dw(reg_index, reg_val)
  return (reg_val << 8) | reg_index
end


def modify_blob_dw(blob_dw, reg_index, mask, val)
  if ((blob_dw & 0xFF) == reg_index)
    new_blob_dw = blob_dw >> 8
    new_blob_dw = (new_blob_dw & mask) | val
    new_blob_dw = blob_cfg_dw(reg_index, new_blob_dw)
  else
    new_blob_dw = blob_dw
  end

  return new_blob_dw
end

def set_auto_cfg(blob_dw, val)
  if $PARAMS[:chip] == "blackhole"
    return modify_blob_dw(blob_dw, STREAM_ONETIME_MISC_CFG_REG_INDEX, ~(1 << PHASE_AUTO_CONFIG), val << PHASE_AUTO_CONFIG)
  else
    return modify_blob_dw(blob_dw, STREAM_MISC_CFG_REG_INDEX, ~(1 << PHASE_AUTO_CONFIG), val << PHASE_AUTO_CONFIG)
  end
end

def blob_header_dw(next_phase_num_cfg_reg_writes, curr_phase_num_msgs, phase_num_incr)
  return (next_phase_num_cfg_reg_writes << 24) | (curr_phase_num_msgs << 12) | phase_num_incr
end

def wrap_phase_num(phase_num)
  epoch_num = phase_num >> $phase_shift
  wrapped_phase_num = phase_num & $wrapped_phase_mask
  return (((epoch_num % $epoch_max) << $epoch_shift) | wrapped_phase_num)
end

def intermediates_blob_loop_dw(phase_start_addr)
  return blob_cfg_dw(STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, phase_start_addr)
end


def get_phase_blob(chip_id, x, y, stream_id, phase, prev_phase, next_phase, dest_phase, dram_blob, src_old_base, src_new_base, next_phase_is_dummy_phase)

  first_phase = prev_phase == nil
  src_change = (first_phase ? true : prev_phase[:next_phase_src_change])
  dest_change = (first_phase ? true : (prev_phase[:next_phase_dest_change] || phase[:no_dest_handshake]))

  scatter_to_dram = phase[:dram_output] && !phase[:legacy_pack]

  dram_read_phase_resets_pointers = phase[:dram_input] && src_change && phase[:ptrs_not_zero]
  phase_resets_pointers = src_change

  # FIXME imatosevic - do we need non-contiguous blobs? 
  # write_overlay_cfg_register(auto_config_en, stream_id, cfg_addr + offset_dw*4, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, 0x<%= next_stream_cfg[:cfg_addr].to_s(16) %>); offset_dw++;

  cfg_dw_list = []

  # if !prev_phase
  #   cfg_dw_list << blob_cfg_dw(STREAM_SCRATCH_REG_INDEX + 0, 0xaabbcc)
  #   cfg_dw_list << blob_cfg_dw(STREAM_SCRATCH_REG_INDEX + 1, stream_id)
  # end

  if prev_phase && (!phase[:scatter_order_size] || phase[:scatter_order_size] <= 1)
    phase_num_inc = phase[:phase_num] - prev_phase[:phase_num]
    phase_wrapped = (phase[:phase_num] & ~$wrapped_phase_mask) != (prev_phase[:phase_num] & ~$wrapped_phase_mask)
    if (phase_num_inc >= 4096 || phase_wrapped)
      cfg_dw_list << blob_cfg_dw(STREAM_CURR_PHASE_REG_INDEX, wrap_phase_num(phase[:phase_num]))
    end
  else
    cfg_dw_list << blob_cfg_dw(STREAM_CURR_PHASE_REG_INDEX, wrap_phase_num(phase[:phase_num]))
  end

  dram_num_msgs = 0
  if (phase[:dram_io] || phase[:dram_streaming])
    dram_blob.each do |dram_blob_buffer_idx, dram_blob_buffer|
      dram_num_msgs += dram_blob_buffer[:num_msgs]
    end
    dram_num_msgs = dram_num_msgs*phase[:num_unroll_iter]
    if (dram_num_msgs > MAX_TILES_MSG_INFO_BUF_PER_PHASE || phase[:dram_input_no_push] || scatter_to_dram || dram_read_phase_resets_pointers || phase[:next_phase_src_change])
      dram_num_msgs = phase[:num_msgs]
    end
  end

  if src_change
    
	  cfg_dw_list << blob_cfg_dw(STREAM_BUF_START_REG_INDEX, (phase[:buf_addr] - src_old_base + src_new_base)/MEM_WORD_BYTES)
	  cfg_dw_list << blob_cfg_dw(STREAM_BUF_SIZE_REG_INDEX, phase[:buf_size]/MEM_WORD_BYTES)
	  cfg_dw_list << blob_cfg_dw(STREAM_MSG_INFO_PTR_REG_INDEX, phase[:msg_info_buf_addr]/MEM_WORD_BYTES)
    if !phase[:resend]
	    cfg_dw_list << blob_cfg_dw(STREAM_MSG_INFO_WR_PTR_REG_INDEX, phase[:msg_info_buf_addr]/MEM_WORD_BYTES)
    end
    
    if (phase[:remote_source] || (phase[:eth_receiver] && phase[:source_endpoint]))
      src_chip, src_y, src_x, src_stream_id = ParseStreamString(phase[:src][0].to_s)

      if !phase[:eth_receiver] && src_chip != chip_id
        fail "src_chip #{src_chip} != chip_id #{chip_id}"
      end
      
      stream_src_x = phase[:eth_receiver] ? ETH_NOC_ID() : (phase[:remote_src_update_noc] == 1 ? NOC1_X_ID(src_x) : src_x)
      stream_src_y = phase[:eth_receiver] ? ETH_NOC_ID() : (phase[:remote_src_update_noc] == 1 ? NOC1_Y_ID(src_y) : src_y)

	    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_REG_INDEX,
                                 STREAM_REMOTE_SRC(stream_src_x,
													                         stream_src_y,
	 												                         src_stream_id,
	 												                         phase[:src_dest_index] ? phase[:src_dest_index] : 0))
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_PHASE_REG_INDEX, wrap_phase_num(phase[:phase_num]))
    end
      
    if phase[:local_sources_connected]
      if first_phase
        if ($PARAMS[:noc_version] > 1)
          cfg_dw_list << blob_cfg_dw(STREAM_GATHER_REG_INDEX,
                                    STREAM_GATHER($PARAMS[:chip], phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                  phase[:src_in_order_fwd] ? 1 : 0,
                                                  phase[:local_stream_clear_num] ? phase[:local_stream_clear_num] : 1,
                                                  phase[:msg_group_stream_clear_type] ? phase[:msg_group_stream_clear_type] : 0))
        end
        cfg_dw_list << blob_cfg_dw(STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX, phase[:src_in_order_fwd_num_msgs] ? phase[:src_in_order_fwd_num_msgs] : 0)
        if $PARAMS[:chip] != "blackhole"
          # The STREAM_GATHER_CLEAR is part of STREAM_GATHER in blackhole
          cfg_dw_list << blob_cfg_dw($PARAMS[:noc_version] > 1 ? STREAM_GATHER_CLEAR_REG_INDEX : STREAM_MCAST_GATHER_CLEAR_REG_INDEX,
                                    STREAM_GATHER_CLEAR(phase[:local_stream_clear_num] ? phase[:local_stream_clear_num] : 1,
                                                        phase[:msg_group_stream_clear_type] ? phase[:msg_group_stream_clear_type] : 0))
        end
      end

      no_sender_below_stream_40 = true
	    local_src_mask = []
      for s in 0..(NOC_NUM_STREAMS-1)
        local_src_mask[s] = false
      end
	    phase[:src].each do |source_label|
	      src_chip, src_y, src_x, src_stream_id = ParseStreamString(source_label.to_s)
        fail "src_chip #{src_chip} != chip_id #{chip_id}" if src_chip != chip_id
	      local_src_mask[src_stream_id] = true
        if src_stream_id < 40
          no_sender_below_stream_40 = false
        end
	    end
      num_local_src_mask_regs = NOC_NUM_STREAMS / STREAM_REG_CFG_DATA_WIDTH
      if (NOC_NUM_STREAMS % STREAM_REG_CFG_DATA_WIDTH) != 0
        num_local_src_mask_regs += 1
      end
      for k in 0..(num_local_src_mask_regs-1)
        val = 0
        for s in (k*STREAM_REG_CFG_DATA_WIDTH)..((k+1)*STREAM_REG_CFG_DATA_WIDTH-1)
          if s < NOC_NUM_STREAMS
            if local_src_mask[s]
              val = val | (1 << (s - (k*STREAM_REG_CFG_DATA_WIDTH)))
            end
          end
        end
        if (first_phase || !no_sender_below_stream_40 || k > 0)
          cfg_dw_list << blob_cfg_dw(STREAM_LOCAL_SRC_MASK_REG_INDEX + k, val)
        end
      end
    end
  end

  if src_change || phase[:dram_input_no_push]
    if (phase[:dram_io] || phase[:dram_streaming])
      if (phase[:dram_input] && phase[:source_endpoint])
        # Used as scratch register to track tiles pushed into the stream on the dram read side.
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_PHASE_REG_INDEX, ((dram_read_phase_resets_pointers ? 1 << 16 : 0) | (phase[:resend] ? 0 : dram_num_msgs)))
      end
    end
  end
    
  if dest_change
    if (phase[:remote_receiver] || (phase[:eth_sender] && phase[:receiver_endpoint]))
      
      dest_chip, dest_y, dest_x, dest_stream_id = ParseStreamString(phase[:dest][0].to_s)
      dest_chip2, dest_y2, dest_x2, dest_stream_id = phase[:dest].size == 2 ? ParseStreamString(phase[:dest][1].to_s) :
                                                                              [nil,nil,nil,dest_stream_id]
      if !phase[:eth_sender] && dest_chip != chip_id
        raise "dest_chip #{dest_chip} != chip_id #{chip_id}"
      end                                                   
      if !phase[:eth_sender] && dest_chip2 != chip_id
        "dest_chip #{dest_chip2} != chip_id #{chip_id}"
      end

      if phase[:eth_sender]
        remote_dest_x_coord = ETH_NOC_ID()
        remote_dest_y_coord = ETH_NOC_ID()
      elsif phase[:outgoing_data_noc] == 1
        remote_dest_x_coord = (phase[:dest].size == 2 ? NOC1_X_ID(dest_x2) : NOC1_X_ID(dest_x))
        remote_dest_y_coord = (phase[:dest].size == 2 ? NOC1_Y_ID(dest_y2) : NOC1_Y_ID(dest_y))
      else
        remote_dest_x_coord = dest_x
        remote_dest_y_coord = dest_y
      end
      
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_BUF_START_REG_INDEX, dest_phase[:buf_addr]/MEM_WORD_BYTES)
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX, dest_phase[:buf_size]/MEM_WORD_BYTES)

      if (phase[:remote_receiver])
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_REG_INDEX,
        STREAM_REMOTE_DEST(remote_dest_x_coord, remote_dest_y_coord, dest_stream_id))

        if (phase[:no_dest_handshake])
          cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_WR_PTR_REG_INDEX, (phase[:saved_dest_wr_ptr] % dest_phase[:buf_size])/MEM_WORD_BYTES)
          cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX, dest_phase[:msg_info_buf_addr]/MEM_WORD_BYTES + phase[:saved_num_msgs_already_sent])
        else
          cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX, dest_phase[:msg_info_buf_addr]/MEM_WORD_BYTES)
        end
      end
      if $PARAMS[:chip] == "blackhole"
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_TRAFFIC_REG_INDEX, phase[:group_priority] ? phase[:group_priority] : 0)
      else
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX, phase[:group_priority] ? phase[:group_priority] : 0)
      end

      if first_phase
        if phase[:dest].size == 2
          dest_noc_x = (phase[:eth_sender] ? ETH_NOC_ID() : (phase[:outgoing_data_noc] == 1 ? NOC1_X_ID(dest_x) : dest_x2))
          dest_noc_y = (phase[:eth_sender] ? ETH_NOC_ID() : (phase[:outgoing_data_noc] == 1 ? NOC1_Y_ID(dest_y) : dest_y2))
          if ($PARAMS[:noc_version] > 1)
            cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                      STREAM_MCAST_DEST_V2(1,
                                                      dest_noc_x,
                                                      dest_noc_y,
                                                      phase[:linked] ? 1 : 0,
                                                      phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                      phase[:no_path_res] ? 1 : 0,
                                                      phase[:mcast_xy]? phase[:mcast_xy] : 0))
          else
            cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                      STREAM_MCAST_DEST_V1(1,
                                                      dest_noc_x,
                                                      dest_noc_y,
                                                      phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                      phase[:src_in_order_fwd] ? 1 : 0,
                                                      phase[:linked] ? 1 : 0,
                                                      phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                      phase[:no_path_res] ? 1 : 0,
                                                      phase[:mcast_xy]? phase[:mcast_xy] : 0))
          end
          cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_NUM_REG_INDEX, phase[:num_mcast_dests])
        else
          if ($PARAMS[:noc_version] > 1)
            cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                      STREAM_MCAST_DEST_V2(0,
                                                      0,
                                                      0,
                                                      phase[:linked] ? 1 : 0,
                                                      phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                      phase[:no_path_res] ? 1 : 0,
                                                      phase[:mcast_xy]? phase[:mcast_xy] : 0))
          else
            cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                      STREAM_MCAST_DEST_V1(0,
                                                      0,
                                                      0,
                                                      phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                      phase[:src_in_order_fwd] ? 1 : 0,
                                                      phase[:linked] ? 1 : 0,
                                                      phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                      phase[:no_path_res] ? 1 : 0,
                                                      phase[:mcast_xy]? phase[:mcast_xy] : 0))
          end
          cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_NUM_REG_INDEX, 1)
        end
      end
    elsif phase[:local_receiver] && !phase[:local_receiver_tile_clearing]
      if ($PARAMS[:noc_version] > 1)
        dest_chip, dest_y, dest_x, dest_stream_id = ParseStreamString(phase[:dest][0].to_s)
        cfg_dw_list << blob_cfg_dw(STREAM_LOCAL_DEST_REG_INDEX, STREAM_LOCAL_DEST(dest_stream_id, 
                                                                                  phase[:local_stream_clear_num] ? phase[:local_stream_clear_num] : 1))
      end
    else
      if (first_phase && $PARAMS[:noc_version] == 1)
         cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                   STREAM_MCAST_DEST_V1(0,
                                                   0,
                                                   0,
                                                   phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                   phase[:src_in_order_fwd] ? 1 : 0,
                                                   phase[:linked] ? 1 : 0,
                                                   phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                   phase[:no_path_res] ? 1 : 0,
                                                   phase[:mcast_xy]? phase[:mcast_xy] : 0))
      end            
    end

    if (phase[:dram_io] || phase[:dram_streaming])
      if (phase[:dram_output] && phase[:receiver_endpoint])
        # Used as scratch register to track tiles popped from the stream on the dram write side.
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_REG_INDEX, ((phase_resets_pointers ? 1 << 16 : 0) | dram_num_msgs))
      end
    end
  else
    if phase[:remote_receiver]
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX, dest_phase[:msg_info_buf_addr]/MEM_WORD_BYTES)
    end
  end

  if phase[:intermediate]
    phase[:phase_auto_config] = true
  elsif next_phase == nil
    phase[:phase_auto_config] = false
  end

  if $PARAMS[:chip] == "blackhole"
    cfg_dw_list << blob_cfg_dw(STREAM_MISC_CFG_REG_INDEX, 
                               STREAM_CFG(INCOMING_DATA_NOC, phase[:eth_receiver] ? 0 : (phase[:incoming_data_noc] ? phase[:incoming_data_noc] : 0)) | 
                               STREAM_CFG(OUTGOING_DATA_NOC, phase[:eth_sender] ? 0 : (phase[:outgoing_data_noc] ? phase[:outgoing_data_noc] : 0)) | 
                               STREAM_CFG(REMOTE_SRC_UPDATE_NOC, phase[:eth_receiver] ? 0 : (phase[:remote_src_update_noc] ? phase[:remote_src_update_noc] : 0)) | 
                               STREAM_CFG(LOCAL_SOURCES_CONNECTED, phase[:local_sources_connected] ? 1 : 0) | 
                               STREAM_CFG(SOURCE_ENDPOINT, phase[:source_endpoint] ? 1 : 0) | 
                               STREAM_CFG(REMOTE_SOURCE, phase[:remote_source] ? 1 : 0) | 
                               STREAM_CFG(RECEIVER_ENDPOINT, phase[:receiver_endpoint] ? 1 : 0) | 
                               STREAM_CFG(LOCAL_RECEIVER, phase[:local_receiver] ? 1 : 0) | 
                               STREAM_CFG(REMOTE_RECEIVER, phase[:remote_receiver] ? 1 : 0) | 
                               STREAM_CFG(NEXT_PHASE_SRC_CHANGE, phase[:next_phase_src_change] || next_phase_is_dummy_phase ? 1 : 0) | 
                               STREAM_CFG(NEXT_PHASE_DEST_CHANGE, phase[:next_phase_dest_change] || next_phase_is_dummy_phase ? 1 : 0) | 
                               STREAM_CFG(DATA_BUF_NO_FLOW_CTRL, phase[:data_buf_no_flow_ctrl] ? 1 : 0) |
                               STREAM_CFG(DEST_DATA_BUF_NO_FLOW_CTRL, phase[:dest_data_buf_no_flow_ctrl] ? 1 : 0) |
                               STREAM_CFG(REMOTE_SRC_IS_MCAST, phase[:remote_src_is_mcast] ? 1 : 0) |
                               STREAM_CFG(NO_PREV_PHASE_OUTGOING_DATA_FLUSH, phase[:no_prev_phase_outgoing_data_flush] ? 1 : 0))
      
    cfg_dw_list << blob_cfg_dw(STREAM_ONETIME_MISC_CFG_REG_INDEX,
                               STREAM_CFG(PHASE_AUTO_CONFIG, phase[:phase_auto_config] || next_phase_is_dummy_phase ? 1 : 0) | 
                               STREAM_CFG(PHASE_AUTO_ADVANCE, phase[:phase_auto_advance] ? 1 : 0) |
                               STREAM_CFG(REG_UPDATE_VC_REG, phase[:reg_update_vc] ? phase[:reg_update_vc] : 3))
      
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_TRAFFIC_REG_INDEX,
                               STREAM_CFG(UNICAST_VC_REG, phase[:vc] ? phase[:vc] : 2))
  else
    cfg_dw_list << blob_cfg_dw(STREAM_MISC_CFG_REG_INDEX, 
                              STREAM_CFG(INCOMING_DATA_NOC, phase[:eth_receiver] ? 0 : (phase[:incoming_data_noc] ? phase[:incoming_data_noc] : 0)) | 
                              STREAM_CFG(OUTGOING_DATA_NOC, phase[:eth_sender] ? 0 : (phase[:outgoing_data_noc] ? phase[:outgoing_data_noc] : 0)) | 
                              STREAM_CFG(REMOTE_SRC_UPDATE_NOC, phase[:eth_receiver] ? 0 : (phase[:remote_src_update_noc] ? phase[:remote_src_update_noc] : 0)) | 
                              STREAM_CFG(LOCAL_SOURCES_CONNECTED, phase[:local_sources_connected] ? 1 : 0) | 
                              STREAM_CFG(SOURCE_ENDPOINT, phase[:source_endpoint] ? 1 : 0) | 
                              STREAM_CFG(REMOTE_SOURCE, phase[:remote_source] ? 1 : 0) | 
                              STREAM_CFG(RECEIVER_ENDPOINT, phase[:receiver_endpoint] ? 1 : 0) | 
                              STREAM_CFG(LOCAL_RECEIVER, phase[:local_receiver] ? 1 : 0) | 
                              STREAM_CFG(REMOTE_RECEIVER, phase[:remote_receiver] ? 1 : 0) | 
                              STREAM_CFG(PHASE_AUTO_CONFIG, phase[:phase_auto_config] || next_phase_is_dummy_phase ? 1 : 0) | 
                              STREAM_CFG(PHASE_AUTO_ADVANCE, phase[:phase_auto_advance] ? 1 : 0) |
                              STREAM_CFG(DATA_AUTO_SEND, phase[:data_auto_send] ? 1 : 0) | 
                              STREAM_CFG(NEXT_PHASE_SRC_CHANGE, phase[:next_phase_src_change] || next_phase_is_dummy_phase ? 1 : 0) | 
                              STREAM_CFG(NEXT_PHASE_DEST_CHANGE, phase[:next_phase_dest_change] || next_phase_is_dummy_phase ? 1 : 0) | 
                              STREAM_CFG(DATA_BUF_NO_FLOW_CTRL, phase[:data_buf_no_flow_ctrl] ? 1 : 0) |
                              STREAM_CFG(DEST_DATA_BUF_NO_FLOW_CTRL, phase[:dest_data_buf_no_flow_ctrl] ? 1 : 0) |
                              STREAM_CFG(REMOTE_SRC_IS_MCAST, phase[:remote_src_is_mcast] ? 1 : 0) |
                              STREAM_CFG(NO_PREV_PHASE_OUTGOING_DATA_FLUSH, phase[:no_prev_phase_outgoing_data_flush] ? 1 : 0) |
                              STREAM_CFG(UNICAST_VC_REG, phase[:vc] ? phase[:vc] : 2) |
                              STREAM_CFG(REG_UPDATE_VC_REG, phase[:reg_update_vc] ? phase[:reg_update_vc] : 3))
  end
  
  if phase[:buf_space_available_ack_thr]
    cfg_dw_list << blob_cfg_dw(STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, phase[:buf_space_available_ack_thr])
  elsif first_phase
    cfg_dw_list << blob_cfg_dw(STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, 0)
  end

  if phase[:source_endpoint]
    # Used as scratch register to track tiles pushed into the stream on the packer side.
    if ((phase[:dram_input] && !phase[:dram_io] && !phase[:dram_streaming]) || phase[:legacy_pack])
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_PHASE_REG_INDEX, phase[:resend] ? 0 : phase[:num_msgs])
      if phase_resets_pointers
        cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_REG_INDEX, 1)
      end
    end
  end
  
  if phase[:receiver_endpoint] || phase[:local_receiver_tile_clearing]
    # Used as scratch register to track tiles popped from the stream on the unpacker side.
    if (!phase[:dram_output] || (!phase[:dram_io] && !phase[:dram_streaming]))
      cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_REG_INDEX, phase[:num_msgs])
      if phase_resets_pointers
        if $PARAMS[:chip] == "blackhole"
          cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_MSG_INFO_BUF_SIZE_REG_INDEX, 1)
        else
          cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX, 1)
        end
      end
    end
  end

  if phase[:resend]
    all_msg_size = phase[:num_msgs] * phase[:msg_size]
    if phase[:buf_size] > all_msg_size
      cfg_dw_list << blob_cfg_dw(STREAM_WR_PTR_REG_INDEX, (all_msg_size/MEM_WORD_BYTES))
    else
      if $PARAMS[:chip] == "blackhole"
        # Writing a 0 means pushing a full buffer, set the wrap bit.
        cfg_dw_list << blob_cfg_dw(STREAM_WR_PTR_REG_INDEX, (0 | 1 << MEM_WORD_ADDR_WIDTH))
      else
        cfg_dw_list << blob_cfg_dw(STREAM_WR_PTR_REG_INDEX, 0)
      end
    end
    cfg_dw_list << blob_cfg_dw(STREAM_MSG_INFO_WR_PTR_REG_INDEX, phase[:msg_info_buf_addr]/MEM_WORD_BYTES + phase[:num_msgs])
  end

  # if !next_phase
  #   cfg_dw_list << blob_cfg_dw(STREAM_SCRATCH_REG_INDEX + 2, 0xddeeff)
  #   cfg_dw_list << blob_cfg_dw(STREAM_SCRATCH_REG_INDEX + 3, stream_id)
  # end

  # FIXME imatosevic - see where this should come from
  # 	NOC_STREAM_WRITE_REG(0, STREAM_PERF_CONFIG_REG_INDEX, STREAM_PERF_CONFIG(1, allow_max_perf ? 1 : rnd32_range(1,8), <% if first_stream_cfg[:partial_send_thr] %><%= first_stream_cfg[:partial_send_thr] %><% else %>allow_max_perf ? 16 : rnd32_range(8,30)<% end %>));

  return cfg_dw_list

end


def get_dummy_phase_blob(chip_id, x, y, stream_id, phase, dummy_phase_addr, dest_dummy_phase_addr, is_sender, is_receiver)

  cfg_dw_list = []

  cfg_dw_list << blob_cfg_dw(STREAM_CURR_PHASE_REG_INDEX, DUMMY_PHASE(phase[:phase_num]))

	cfg_dw_list << blob_cfg_dw(STREAM_BUF_START_REG_INDEX, dummy_phase_addr/MEM_WORD_BYTES)
	cfg_dw_list << blob_cfg_dw(STREAM_BUF_SIZE_REG_INDEX, 1)
	cfg_dw_list << blob_cfg_dw(STREAM_MSG_INFO_PTR_REG_INDEX, dummy_phase_addr/MEM_WORD_BYTES)
	cfg_dw_list << blob_cfg_dw(STREAM_MSG_INFO_WR_PTR_REG_INDEX, dummy_phase_addr/MEM_WORD_BYTES)
    
  if is_receiver
    src_chip, src_y, src_x, src_stream_id = ParseStreamString(phase[:src][0].to_s)

    if !phase[:eth_receiver] && src_chip != chip_id
      fail "src_chip #{src_chip} != chip_id #{chip_id}"
    end
      
    stream_src_x = phase[:eth_receiver] ? ETH_NOC_ID() : (phase[:remote_src_update_noc] == 1 ? NOC1_X_ID(src_x) : src_x)
    stream_src_y = phase[:eth_receiver] ? ETH_NOC_ID() : (phase[:remote_src_update_noc] == 1 ? NOC1_Y_ID(src_y) : src_y)

	  cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_REG_INDEX,
                               STREAM_REMOTE_SRC(stream_src_x,
												                         stream_src_y,
	 											                         src_stream_id,
	 											                         phase[:src_dest_index] ? phase[:src_dest_index] : 0))
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_SRC_PHASE_REG_INDEX, DUMMY_PHASE(phase[:phase_num]))
  end
    
  if is_sender
    dest_chip, dest_y, dest_x, dest_stream_id = ParseStreamString(phase[:dest][0].to_s)
    dest_chip2, dest_y2, dest_x2, dest_stream_id = phase[:dest].size == 2 ? ParseStreamString(phase[:dest][1].to_s) :
                                                                              [nil,nil,nil,dest_stream_id]
    if !phase[:eth_sender] && dest_chip != chip_id
      raise "dest_chip #{dest_chip} != chip_id #{chip_id}"
    end                                                   
    if !phase[:eth_sender] && dest_chip2 != chip_id
      "dest_chip #{dest_chip2} != chip_id #{chip_id}"
    end

    if phase[:eth_sender]
      remote_dest_x_coord = ETH_NOC_ID()
      remote_dest_y_coord = ETH_NOC_ID()
    elsif phase[:outgoing_data_noc] == 1
      remote_dest_x_coord = (phase[:dest].size == 2 ? NOC1_X_ID(dest_x2) : NOC1_X_ID(dest_x))
      remote_dest_y_coord = (phase[:dest].size == 2 ? NOC1_Y_ID(dest_y2) : NOC1_Y_ID(dest_y))
    else
      remote_dest_x_coord = dest_x
      remote_dest_y_coord = dest_y
    end

    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_REG_INDEX,
                               STREAM_REMOTE_DEST(remote_dest_x_coord, remote_dest_y_coord, dest_stream_id))
      
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_BUF_START_REG_INDEX, dest_dummy_phase_addr/MEM_WORD_BYTES)
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX, 1)
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX, dest_dummy_phase_addr/MEM_WORD_BYTES)

    if phase[:dest].size == 2
      dest_noc_x = (phase[:eth_sender] ? ETH_NOC_ID() : (phase[:outgoing_data_noc] == 1 ? NOC1_X_ID(dest_x) : dest_x2))
      dest_noc_y = (phase[:eth_sender] ? ETH_NOC_ID() : (phase[:outgoing_data_noc] == 1 ? NOC1_Y_ID(dest_y) : dest_y2))
      if ($PARAMS[:noc_version] > 1)
        cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                   STREAM_MCAST_DEST_V2(1,
                                                   dest_noc_x,
                                                   dest_noc_y,
                                                   phase[:linked] ? 1 : 0,
                                                   phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                   phase[:no_path_res] ? 1 : 0,
                                                   phase[:mcast_xy]? phase[:mcast_xy] : 0))
      else
 	      cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                   STREAM_MCAST_DEST_V1(1,
                                                   dest_noc_x,
                                                   dest_noc_y,
                                                   phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                   phase[:src_in_order_fwd] ? 1 : 0,
                                                   phase[:linked] ? 1 : 0,
                                                   phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                   phase[:no_path_res] ? 1 : 0,
                                                   phase[:mcast_xy]? phase[:mcast_xy] : 0))
      end
      cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_NUM_REG_INDEX, phase[:num_mcast_dests])
    else
      if ($PARAMS[:noc_version] > 1)
        cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                   STREAM_MCAST_DEST_V2(0,
                                                   0,
                                                   0,
                                                   phase[:linked] ? 1 : 0,
                                                   phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                   phase[:no_path_res] ? 1 : 0,
                                                   phase[:mcast_xy]? phase[:mcast_xy] : 0))
      else
 	      cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_REG_INDEX,
                                   STREAM_MCAST_DEST_V1(0,
                                                   0,
                                                   0,
                                                   phase[:arb_group_size] ? phase[:arb_group_size] : 1,
                                                   phase[:src_in_order_fwd] ? 1 : 0,
                                                   phase[:linked] ? 1 : 0,
                                                   phase[:vc] ? (phase[:vc] & 0x1) : 0,
                                                   phase[:no_path_res] ? 1 : 0,
                                                   phase[:mcast_xy]? phase[:mcast_xy] : 0))
      end
      cfg_dw_list << blob_cfg_dw(STREAM_MCAST_DEST_NUM_REG_INDEX, 1)
    end
  end

  if $PARAMS[:chip] == "blackhole"
    cfg_dw_list << blob_cfg_dw(STREAM_MISC_CFG_REG_INDEX, 
                               STREAM_CFG(INCOMING_DATA_NOC, phase[:eth_receiver] ? 0 : (phase[:incoming_data_noc] ? phase[:incoming_data_noc] : 0)) | 
                               STREAM_CFG(OUTGOING_DATA_NOC, phase[:eth_sender] ? 0 : (phase[:outgoing_data_noc] ? phase[:outgoing_data_noc] : 0)) | 
                               STREAM_CFG(REMOTE_SRC_UPDATE_NOC, phase[:eth_receiver] ? 0 : (phase[:remote_src_update_noc] ? phase[:remote_src_update_noc] : 0)) | 
                               STREAM_CFG(LOCAL_SOURCES_CONNECTED, 0) | 
                               STREAM_CFG(SOURCE_ENDPOINT, is_sender ? 1 : 0) | 
                               STREAM_CFG(REMOTE_SOURCE, is_receiver ? 1 : 0) | 
                               STREAM_CFG(RECEIVER_ENDPOINT, 0) | 
                               STREAM_CFG(LOCAL_RECEIVER, 0) | 
                               STREAM_CFG(REMOTE_RECEIVER, is_sender ? 1 : 0) | 
                               STREAM_CFG(TOKEN_MODE, (phase[:token_mode] ? 1 : 0)) | 
                               STREAM_CFG(COPY_MODE, (phase[:copy_mode] ? 1 : 0)) | 
                               STREAM_CFG(NEXT_PHASE_SRC_CHANGE, 1) | 
                               STREAM_CFG(NEXT_PHASE_DEST_CHANGE, 1) | 
                               STREAM_CFG(DATA_BUF_NO_FLOW_CTRL, phase[:data_buf_no_flow_ctrl] ? 1 : 0) |
                               STREAM_CFG(DEST_DATA_BUF_NO_FLOW_CTRL, phase[:dest_data_buf_no_flow_ctrl] ? 1 : 0) |
                               STREAM_CFG(MSG_INFO_BUF_FLOW_CTRL, (phase[:msg_info_buf_flow_ctrl] ? 1 : 0)) |
                               STREAM_CFG(DEST_MSG_INFO_BUF_FLOW_CTRL, (phase[:dest_msg_info_buf_flow_ctrl] ? 1 : 0)) |
                               STREAM_CFG(REMOTE_SRC_IS_MCAST, phase[:remote_src_is_mcast] ? 1 : 0) |
                               STREAM_CFG(NO_PREV_PHASE_OUTGOING_DATA_FLUSH, phase[:no_prev_phase_outgoing_data_flush] ? 1 : 0) |
                               STREAM_CFG(SRC_FULL_CREDIT_FLUSH_EN, (phase[:src_full_credit_flush_en] ? 1 : 0)) |
                               STREAM_CFG(DST_FULL_CREDIT_FLUSH_EN, (phase[:dst_full_credit_flush_en] ? 1 : 0)) |
                               STREAM_CFG(INFINITE_PHASE_EN, (phase[:infinite_phase_en] ? 1 : 0)) |
                               STREAM_CFG(OOO_PHASE_EXECUTION_EN, (phase[:ooo_phase_execution_en] ? 1 : 0))) |
      
    cfg_dw_list << blob_cfg_dw(STREAM_ONETIME_MISC_CFG_REG_INDEX,
                               STREAM_CFG(PHASE_AUTO_CONFIG, 1) | 
                               STREAM_CFG(PHASE_AUTO_ADVANCE, phase[:phase_auto_advance] ? 1 : 0) |
                               STREAM_CFG(REG_UPDATE_VC_REG, phase[:reg_update_vc] ? phase[:reg_update_vc] : 3) |
                               STREAM_CFG(GLOBAL_OFFSET_TABLE_RD_SRC_INDEX, phase[:global_offset_table_rd_src_index] ? phase[:global_offset_table_rd_src_index] : 0) |
                               STREAM_CFG(GLOBAL_OFFSET_TABLE_RD_DEST_INDEX, phase[:global_offset_table_rd_dest_index] ? phase[:global_offset_table_rd_dest_index] : 0))
      
    cfg_dw_list << blob_cfg_dw(STREAM_REMOTE_DEST_TRAFFIC_REG_INDEX,
                               STREAM_CFG(NOC_PRIORITY, phase[:noc_priority] ? phase[:noc_priority] : 0) |
                               STREAM_CFG(UNICAST_VC_REG, phase[:vc] ? phase[:vc] : 2))
  else
    cfg_dw_list << blob_cfg_dw(STREAM_MISC_CFG_REG_INDEX, 
                              STREAM_CFG(INCOMING_DATA_NOC, phase[:eth_receiver] ? 0 : (phase[:incoming_data_noc] ? phase[:incoming_data_noc] : 0)) | 
                              STREAM_CFG(OUTGOING_DATA_NOC, phase[:eth_sender] ? 0 : (phase[:outgoing_data_noc] ? phase[:outgoing_data_noc] : 0)) | 
                              STREAM_CFG(REMOTE_SRC_UPDATE_NOC, phase[:eth_receiver] ? 0 : (phase[:remote_src_update_noc] ? phase[:remote_src_update_noc] : 0)) | 
                              STREAM_CFG(LOCAL_SOURCES_CONNECTED, 0) | 
                              STREAM_CFG(SOURCE_ENDPOINT, is_sender ? 1 : 0) | 
                              STREAM_CFG(REMOTE_SOURCE, is_receiver ? 1 : 0) | 
                              STREAM_CFG(RECEIVER_ENDPOINT, 0) | 
                              STREAM_CFG(LOCAL_RECEIVER, 0) | 
                              STREAM_CFG(REMOTE_RECEIVER, is_sender ? 1 : 0) | 
                              STREAM_CFG(PHASE_AUTO_CONFIG, 1) | 
                              STREAM_CFG(PHASE_AUTO_ADVANCE, phase[:phase_auto_advance] ? 1 : 0) | 
                              STREAM_CFG(DATA_AUTO_SEND, phase[:data_auto_send] ? 1 : 0) | 
                              STREAM_CFG(NEXT_PHASE_SRC_CHANGE, 1) | 
                              STREAM_CFG(NEXT_PHASE_DEST_CHANGE, 1) | 
                              STREAM_CFG(DATA_BUF_NO_FLOW_CTRL, phase[:data_buf_no_flow_ctrl] ? 1 : 0) |
                              STREAM_CFG(DEST_DATA_BUF_NO_FLOW_CTRL, phase[:dest_data_buf_no_flow_ctrl] ? 1 : 0) |
                              STREAM_CFG(REMOTE_SRC_IS_MCAST, phase[:remote_src_is_mcast] ? 1 : 0) |
                              STREAM_CFG(NO_PREV_PHASE_OUTGOING_DATA_FLUSH, phase[:no_prev_phase_outgoing_data_flush] ? 1 : 0) |
                              STREAM_CFG(UNICAST_VC_REG, phase[:vc] ? phase[:vc] : 2) |
                              STREAM_CFG(REG_UPDATE_VC_REG, phase[:reg_update_vc] ? phase[:reg_update_vc] : 3))
  end

  return cfg_dw_list

end

