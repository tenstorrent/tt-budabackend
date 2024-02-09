`ifndef __MAIN_PROGRAM_SV__
`define __MAIN_PROGRAM_SV__

`include "global_constraints.sv"
`include "test_config.sv"

`include "common/macros.sv"
`include "common/tensor_constraints.sv"
`include "common/node_constraints.sv"
`include "common/input_constraints.sv"
`include "common/operation_constraints.sv"
`include "common/queue_constraints.sv"
`include "common/netlist_constraints.sv"

`include "operations/matmul_op.sv"
`include "operations/unary_op.sv"
`include "operations/binary_op.sv"
`include "operations/splice_op.sv"
`include "operations/reduce_op.sv"
`include "operations/matmul_ident_op.sv"
`include "operations/binary_sfpu_op.sv"
`include "operations/depthwise_op.sv"

class main_program;
    string out_filename;
    string op_name;
    integer ntb_random_seed=0;
    integer num_rand_loops=1;
    integer out_filehandle;
    test_config test_cfg;
    bit int8_enabled = 0;
    bit quantize_enabled = 0;
    bit feeder_drainer = 0;
    bit bfp_enabled = 1;
    integer test_id = 0;
    netlist_constraints netlist;
    operation_constraints operation;

    function new();

    endfunction

    function void parse_arguments();
        $value$plusargs("num_rand_loops=%d", num_rand_loops);
        $value$plusargs("ntb_random_seed=%d", ntb_random_seed);
        $value$plusargs("op=%s", op_name);
        $value$plusargs("fd=%d", feeder_drainer);
        $value$plusargs("int8=%d", int8_enabled);
        $value$plusargs("quantize=%d", quantize_enabled);
        $value$plusargs("bfp=%d", bfp_enabled);
        if (!$value$plusargs("out=%s", out_filename)) begin
            $fatal("Out file name not specified!");
        end else begin
            $display("INFO: Writing rand vars to %s", out_filename);
        end
        $display("op name = %0s", op_name);
    endfunction

    function netlist_constraints create_single_op_netlist(operation_constraints op);
        input_queue_constraints q_inputs[];
        output_queue_constraints q_output0;
        netlist_constraints netlist = new();

        q_inputs = new[op.get_max_inputs()];
        foreach (q_inputs[i])
        begin
            q_inputs[i] = new($sformatf("input_%0d", i));
        end

        op.set_inputs(q_inputs);

        q_output0 = output_queue_constraints::new("output0", op);
        netlist.initialize(q_inputs, {q_output0}, {op});
        return netlist;
    endfunction

    function netlist_constraints create_single_op_feeder_drainer_netlist(operation_constraints op);
        input_queue_constraints q_inputs[];
        feeder_drainer_op feeders[];
        feeder_drainer_op drainer;
        output_queue_constraints q_output0;
        netlist_constraints netlist;
        feeder_drainer_op resized_feeders[];
        node_constraints op_inputs[];
        integer num_feeders = 0;

        q_inputs = new[op.get_max_inputs()];
        foreach (q_inputs[i])
        begin
            q_inputs[i] = new($sformatf("input_%0d", i));
        end

        feeders = new[op.get_max_inputs()];
        op_inputs = new[op.get_max_inputs()];
        foreach (feeders[i])
        begin
            if (op.feeder_enabled(i)) begin
                feeders[num_feeders] = new($sformatf("feeder_%0d", i));
                feeders[num_feeders].set_inputs({q_inputs[i]});
                op_inputs[i] = feeders[num_feeders];
                num_feeders++;
            end else begin
                op_inputs[i] = q_inputs[i];
            end
        end

        resized_feeders = new[num_feeders];
        foreach (resized_feeders[i])
        begin
            resized_feeders[i] = feeders[i];
        end

        op.set_inputs(op_inputs);

        // Op output is going to drainer op, and it cannot be gradient_op
        op.gradient_op_enabled = 0;

        drainer = new("drainer");
        drainer.set_inputs({op});

        q_output0 = new ("output0", drainer);

        netlist = new();

        netlist.initialize(q_inputs, {q_output0}, {op, resized_feeders, drainer});

        return netlist;
    endfunction

    function operation_constraints create_operation();
        if (op_name == "matmul") begin
            operation = matmul_op::new("op");
        end else if (op_name == "unary") begin
            operation = unary_op::new("op");
        end else if (op_name == "matmul_ident") begin
            operation = matmul_ident_op::new("op");
        end else if (op_name == "binary") begin
            operation = binary_op::new("op");
        end else if (op_name == "splice") begin
            operation = splice_op::new("op");
        end else if (op_name == "reduce") begin
            operation = reduce_op::new("op");
        end else if (op_name == "binary_sfpu") begin
            operation = binary_sfpu_op::new("op");
        end else if (op_name == "depthwise") begin
            operation = depthwise_op::new("op");
        end else begin
            $fatal(0, "ERROR: Unknown op_name %0s", op_name);
        end
        operation.bfp_enabled = bfp_enabled;
        return operation;
    endfunction

    function void create_netlist(bit is_feeder_drainer);
        if (is_feeder_drainer) begin
            netlist = create_single_op_feeder_drainer_netlist(create_operation());
        end else begin
            netlist = create_single_op_netlist(create_operation());
        end
    endfunction

    function void generate_configs(bit is_feeder_drainer = 0, bit is_int8_enabled = 0, bit is_default = 1);
        create_netlist(is_feeder_drainer);
        $display("Feeder drainer %0d\tOperation name = %0s, is_default = %0d",is_feeder_drainer, op_name, is_default);
        for (int i =0; i <num_rand_loops; i = i + 1) begin
            $display("INFO: Creating test %3d", i);
            $fwrite(out_filehandle, "#Test id=%0d\n", test_id++);
            $fwrite(out_filehandle, "- device: %s\n", `DEVICE);
            if (is_default) begin
                if (netlist.randomize()) begin
                    $display("INFO: Randomization successful");
                end else begin
                    $display("ERROR: Randomization failed");
                    return;
                end
            end else begin
                randomize_netlist(is_int8_enabled);
            end
            netlist.write_to_file(out_filehandle);
        end
    endfunction

    function void run();
        out_filehandle = $fopen(out_filename, "w");
        if (is_generate_all_configs()) begin
            generate_all_configs();
        end else begin
            generate_configs(feeder_drainer, int8_enabled, 0 /* override default */);
        end
        $fclose(out_filehandle);
    endfunction

    function void randomize_netlist(bit is_int8_enabled);
        if (is_int8_enabled) begin
            netlist.randomize() with { netlist.op[0].dest_data_format == int32;};
        end else begin
            netlist.randomize() with { netlist.op[0].dest_data_format != int32;};
        end
    endfunction

    function bit is_generate_all_configs();
        return op_name == "run_all";
    endfunction

    function void generate_all_configs();
        string operation_names[] = '{"matmul", "unary", "matmul_ident", "binary", "splice", "reduce", "binary_sfpu", "depthwise"}; // Inline initialization

        foreach (operation_names[i]) begin
            op_name = operation_names[i];
            generate_configs(0); // (feeder drainer = 0)
            generate_configs(1); // (feeder drainer = 1)
        end

    endfunction
endclass

program generator();
    initial begin
        main_program main = new();
        main.parse_arguments();
        main.run();
    end
endprogram
`endif