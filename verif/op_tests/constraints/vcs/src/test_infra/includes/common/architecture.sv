`ifndef _ARCHITECTURE__
`define _ARCHITECTURE__
`include "includes/common/types.sv"

virtual class architecture;
    rand bit      l1_acc_enable;
    rand bit      srnd_enable;
    rand e_math_fidelity math_fidelity;
    rand bit transpose_input_0_binary;
    rand bit same_exponent_out_intermed;
    rand bit sfpu_execution_en;
    rand bit tiny_tile_enabled;
    rand bit gradient_binary_multiply_constraint;
    rand bit force_emulator_grid_size;
    rand bit force_emulator_input_count;
    bit unary_high_precision = 1;
endclass;

class grayskull_architecture extends architecture;
    function new();
        super.new();
        unary_high_precision = 0;
    endfunction

    constraint rand_tiny_tile_enabled {
        tiny_tile_enabled == 0;
    }

    constraint rand_sfpu_execution_en {
        sfpu_execution_en == 0;
    }

    constraint rand_same_exponent_out_intermed {
        same_exponent_out_intermed == 0;
    }

    constraint rand_transpose_input_0_binary {
        transpose_input_0_binary == 0;
    }

    constraint rand_l1_acc_enable {
        l1_acc_enable == 0;
    }

    constraint rand_srnd_enable {
        srnd_enable == 0;
    }

    constraint math_fidelity_constraint {
        math_fidelity inside {LoFi, HiFi2, HiFi3};
    }

    constraint rand_gradient_binary_multiply {
        gradient_binary_multiply_constraint == 0;
    }

    constraint rand_force_emulator_grid_size {
        force_emulator_grid_size == 0;
    }

    constraint rand_force_emulator_input_count {
        force_emulator_input_count == 0;
    }
endclass;

class wormhole_b0_architecture extends architecture;
    constraint rand_tiny_tile_enabled {
        tiny_tile_enabled == 1;
    }

    constraint rand_sfpu_execution_en {
        sfpu_execution_en == 1;
    }

    constraint rand_same_exponent_out_intermed {
        same_exponent_out_intermed == 1;
    }

    constraint rand_transpose_input_0_binary {
        transpose_input_0_binary == 1;
    }

    constraint rand_l1_acc_enable {
        l1_acc_enable == 1;
    }

    constraint rand_srnd_enable {
        srnd_enable dist {0:=1, 1:=3};
    }

    constraint math_fidelity_constraint {
        math_fidelity inside {LoFi, HiFi2, HiFi3, HiFi4};
    }

    constraint rand_gradient_binary_multiply {
        gradient_binary_multiply_constraint == 1;
    }

    constraint rand_force_emulator_grid_size {
        force_emulator_grid_size == 0;
    }

    constraint rand_force_emulator_input_count {
        force_emulator_input_count == 0;
    }
endclass;

class blackhole_emulator_architecture extends architecture;
    constraint rand_tiny_tile_enabled {
        tiny_tile_enabled == 1;
    }

    constraint rand_sfpu_execution_en {
        sfpu_execution_en == 1;
    }

    constraint rand_same_exponent_out_intermed {
        same_exponent_out_intermed == 1;
    }

    constraint rand_transpose_input_0_binary {
        transpose_input_0_binary == 1;
    }

    constraint rand_l1_acc_enable {
        l1_acc_enable == 1;
    }

    constraint rand_srnd_enable {
        srnd_enable dist {0:=1, 1:=3};
    }

    constraint math_fidelity_constraint {
        math_fidelity inside {LoFi, HiFi2, HiFi3, HiFi4};
    }

    constraint rand_gradient_binary_multiply {
        gradient_binary_multiply_constraint == 1;
    }

    constraint rand_force_emulator_grid_size {
        force_emulator_grid_size == 1;
    }

    constraint rand_force_emulator_input_count {
        force_emulator_input_count == 1;
    }
endclass;

class blackhole_architecture extends architecture;
    constraint rand_tiny_tile_enabled {
        // Tiny tile is causing hangs on BH
        tiny_tile_enabled == 0;
    }

    constraint rand_sfpu_execution_en {
        sfpu_execution_en == 1;
    }

    constraint rand_same_exponent_out_intermed {
        same_exponent_out_intermed == 1;
    }

    constraint rand_transpose_input_0_binary {
        transpose_input_0_binary == 1;
    }

    constraint rand_l1_acc_enable {
        l1_acc_enable == 1;
    }

    constraint rand_srnd_enable {
        srnd_enable dist {0:=1, 1:=3};
    }

    constraint math_fidelity_constraint {
        math_fidelity inside {LoFi, HiFi2, HiFi3, HiFi4};
    }

    constraint rand_gradient_binary_multiply {
        gradient_binary_multiply_constraint == 1;
    }

    constraint rand_force_emulator_grid_size {
        force_emulator_grid_size == 0;
    }

    constraint rand_force_emulator_input_count {
        force_emulator_input_count == 0;
    }
endclass;

`endif