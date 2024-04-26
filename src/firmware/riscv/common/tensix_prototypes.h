// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
inline void ex_pop_rdptr();
inline void ex_pop_wi(uint data_index);
inline void ex_sync_instrn(volatile uint * instrn_buffer, volatile uint * mailbox);
inline void ex_sync_kernel(volatile uint * mailbox);
inline void ex_setc16(uint addr, uint val, volatile uint * instrn_buf);
inline void ex_rdcfg(uint gpr, uint cfg_addr,  volatile uint * instrn_buf);
inline void ex_wrcfg(uint gpr, uint cfg_addr,  volatile uint * instrn_buf);
inline void ex_rmw_cfg(uint cfg_addr32, uint cfg_shamt, uint32_t cfg_mask, uint gpr_index,  volatile uint * regfile, volatile uint * cfg_regs);
inline uint ex_rmw_cfg_imm(uint cfg_addr32, uint cfg_shamt, uint32_t cfg_mask,uint wr_val, uint val);
inline void ex_setadcxy(uint *values, volatile uint * instrn_buf);
inline void ex_setadczw(uint *values, volatile uint * instrn_buf);
inline void unhalt_tensix ();
inline void execute_instruction (unsigned int instruction);
inline void thcon_flush_dma(uint arg);
inline void thcon_load_ind(uint base_addr_index, uint dst_data_index, uint offset_index, uint autoinc, uint size);
inline void thcon_store_ind(uint base_index, uint src_data_index, uint offset_index, uint mode_32b_16B, bool l0_l1_sel, uint tile_mode);
inline void thcon_incr_get_ptr(uint mem_addr_index, uint data_reg_index, uint incr_val, uint wrap_val, bool rd_wr, bool l0_l1_sel);
inline void thcon_reg_to_flops(uint mode_32b_16B, uint reg_index, uint flop_index, uint target_select=0, uint byte_offset=0);
inline void thcon_cas(uint mem_addr_index, uint cmp_val, uint swap_val, bool l0_l1_sel);
inline void thcon_at_swap(uint mem_addr_index, uint src_data_index, uint mask_16b, bool l0_l1_sel);
inline void thcon_write_16b_reg(uint addr /* 16b quants */, uint val);
inline void set_l1_32b(uint addr, uint val);
inline void thcon_write_32b_reg(uint addr /*32b quants*/, uint val);
inline void thcon_write_16B_reg(uint addr, uint *val);
inline void thcon_set_packer_section_conf(uint rowstart_size, uint exp_size);
inline void thcon_set_packer_l1_dest_addr(uint l1_dest_addr);
inline void thcon_set_packer_misc_conf(uint disable_zcomp, uint in_data_format, uint out_data_format, uint dest_digest_offset);
inline void thcon_set_unpacker_misc_conf(uint out_data_format);
inline void thcon_set_descriptor(uint reg_index);
inline void thcon_write_descriptor_to_reg(uint reg_index, uint tile_id, uint tile_type, uint x_dim, uint y_dim, uint z_dim, uint w_dim, uint digest_type, uint digest_size );
inline void thcon_write_descriptor_to_l1(uint addr, uint tile_id, uint tile_type, uint x_dim, uint y_dim, uint z_dim, uint w_dim, uint digest_type, uint digest_size );
