// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/buda_soc_descriptor.h"
#include "netlist/netlist.hpp"
#include "runtime_types.hpp"
#include "runtime_config.hpp"

using buda_soc_description = buda_SocDescriptor;

struct tt_runtime_queue_ptrs_wrap
{

    using Field = tt_queue_header_field;

    std::unordered_map<std::string, tt_queue_setting_info> pending_queue_settings;
    std::vector<std::pair<tt_instruction_info, tt_queue_varinst_update_field_mask>> pending_varinst_queue_updates;
    std::unordered_map<Field, std::unordered_map<std::string, std::string>> header_field_queue_to_var_map;
    std::unordered_map<Field, std::unordered_map<std::string, std::unordered_set<std::string>>> header_field_vars_to_queue_map;

    std::vector<Field> supported_fields = {Field::GlobalRdptr, Field::GlobalWrptr, Field::LocalRdptr, Field::ZeroSetting};

    tt_runtime_queue_ptrs_wrap() {
        clear(); // And initialize maps so is_* function is simplified.
    }

    void clear();

    void update_pending(std::string q_name, bool read_only);
    void erase_pending(std::string q_name);

    bool has_pending() { return pending_queue_settings.size(); }
    bool has_pending_varinst() { return pending_varinst_queue_updates.size(); }

    // Maps given queue and variable name of given field type together.
    void map_queue_field_var(Field field_type, string q_name, string var_name);
    // Returns variable name of given type that is bound to given queue name.
    std::string get_queue_field_var(Field field_type, std::string q_name);
    // Returns true if variable of given name is bound to a queue field of given type.
    bool is_field_type_var(Field field_type, string var_name) const { return header_field_vars_to_queue_map.at(field_type).find(var_name) != header_field_vars_to_queue_map.at(field_type).end(); }

    // Returns set of queues that variable of given name and field type are mapped to.
    const std::unordered_set<std::string>& get_queue_set(Field field_name, std::string var_name) const;

    // Returns list of field types a var is bound to.
    const std::vector<tt_queue_header_field> get_var_field_types(const std::string &var_name);

};

struct buffer_range_t{
    // Stores the allocation info for a single buffer in a queue
    std::string queue_name;
    std::string loc = "DEVICE";
    int device_id;
    int src_device_id; //Used to get the address space for queues on host
    uint32_t channel;
    uint32_t start_addr;
    uint32_t end_addr;
    
    buffer_range_t(std::string qn, int did, int sid, uint32_t ch, uint32_t sa, uint32_t ea, std::string l = "DEVICE")
        : queue_name(std::move(qn)), loc(std::move(l)), device_id(did), src_device_id(sid),
        channel(ch), start_addr(sa), end_addr(ea) {}

};

enum class DualViewRamType : uint8_t
{
    ReadOnly = 0x0,
    WriteOnly = 0x1,
    Unknown = 0x2,
};

struct dual_view_ram_info_t{
    DualViewRamType type;
    std::unordered_set<std::string> alt_views;
};

ostream& operator<<(ostream& out, const buffer_range_t& address_range);
class tt_runtime_workload : public netlist_workload_data
{

    private:
    std::unordered_map<std::string, tt_runtime_queue_ptrs_wrap> program_qptrs_wrap;
    std::vector<void*> allocated_tensor_memories;
    std::set<string> allocated_dynamic_queues;
    std::set<string> pending_deallocate_dynamic_queues;
    std::unordered_map<std::string, std::pair<DualViewRamType, std::string>> dual_view_rams_to_type_alt_view_map;
    std::unordered_map<std::string, dual_view_ram_info_t> dual_view_rams_to_info_map;
    std::unordered_map<std::string, std::pair<std::string, std::string>> single_graph_to_dual_view_ram_reader_writer_map;

    std::unordered_map<std::string, buffer_range_t> buffer_ranges_map;
    std::unordered_map<std::string, int> program_loops_on_device_map;
    std::unordered_map<std::string, std::vector<buffer_range_t>> queues_to_buf_range;
    tt_runtime_config m_config = {};
    // Functions to verify if host/DRAM queues overlap
    void generate_queue_to_buf_range_map();
    // Determine if queues in two seperate groups overlap
    void find_buffer_overlap_across_queues(const std::unordered_set<string>& queue_names_left, const std::unordered_set<string>& queue_names_right);
    // Determine if queues in the smae group overlap
    void find_buffer_overlap_across_queues(const std::unordered_set<string>& queue_names, bool host = false);
    void check_dynamic_queue_overlap();
    public:
    tt_runtime_workload() {};
    tt_runtime_workload(string netlist, const tt_runtime_config& config = {}) : netlist_workload_data(netlist, config) {};
    ~tt_runtime_workload();

    //! Program helpers
    netlist_program &get_program(string prog_name);
    netlist_program &reset_program(string prog_name);
    void check_program(string prog_name);

    //! Misc helpers
    tt_dram_io_desc get_io_desc_for_queue(string queue_name);
    tt_runtime_queue_ptrs_wrap &get_qptrs_wrap(string prog_name);
    std::vector<std::vector<int>> collect_used_tensix_cores();
    std::vector<int> compute_max_grid(const tt::ARCH &arch, buda_soc_description *default_full_soc_desc_ptr);
    std::vector<int> compute_min_possible_grid(const tt::ARCH &arch);
    void allocate_queue_untilized_memory(string queue_name);
    buffer_range_t get_buffer_range(string queue_name, int buf_id);
    bool check_buffer_overlap(buffer_range_t lhs, buffer_range_t rhs, bool host = false) const;
    std::string get_op_name(const string& graph_name, const tt_xy_pair& logical_core_xy);

    //! Queue state management
    void bind_queue_field_vars(std::vector<tt_queue_setting_info> &queue_settings, string prog_name);
    void unbind_queue_field_vars(string queue_name, string prog_name);
    void add_pending_update_queues(string prog_name, string var_name);
    void add_pending_varinst_update_queues(string prog_name, tt_instruction_info instrn, tt_queue_varinst_update_field_mask update_type_mask);

    void allocate_queue(string queue_name, string prog_name);
    void deallocate_queue(string queue_name, string prog_name, bool immediate);
    std::set<string> collect_overlapped_queues(string queue_name);
    void check_queues_do_not_overlap();
    bool is_cross_chip_e2e_queue(string queue_name);

    //! Dynamic memory management
    std::shared_ptr<std::vector<uint32_t>> allocate_untilized_memory(int num_elements);
    std::shared_ptr<std::vector<uint32_t>> allocate_untilized_memory(std::string q_name, int num_entries);
    std::shared_ptr<std::vector<uint32_t>> allocate_tilized_memory(const std::string& q_name, uint32_t num_entries);
    void deallocate_memory(void* ptr);
    std::vector<tt_queue_setting_info> collect_temporal_epoch_instance_queue_settings(netlist_program &program, int target_device);
    std::set<string> collect_output_e2e_queues(string graph_name);
    std::set<string> collect_pending_dealloc_queues(set<int> &target_devices);
    std::set<int> collect_output_e2e_devices(string graph_name);

    //! Dual View Ram helpers
    std::string get_base_queue_name(const tt_queue_info &queue_info);
    void identify_all_dual_view_rams();
    const std::unordered_map<std::string, dual_view_ram_info_t> &get_dual_view_rams_map() const;
    const std::pair<std::string, std::string> &get_single_graph_dual_view_ram_reader_writer(std::string graph_name) const;
    //! Debug analysis helpers
    void print_cross_chip_e2e_queues();
};
