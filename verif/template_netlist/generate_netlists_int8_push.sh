#!/bin/bash

# Define common paths
TEMPLATE_DIR="verif/op_tests/netlists/templates/wormhole_b0"
CONSTRAINTS_DIR="verif/op_tests/constraints/vcs"
OUTPUT_DIR_BASE="build/test/int8"
USE_HASH="0"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/binary/netlist_binary_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/binary/binary_op_test_configs.int8.push.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/binary-int8-push" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/matmul/netlist_matmul_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul_op_test_configs.int8.push.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-int8-push" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/matmul/netlist_matmul_op_with_bias.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul_op_test_configs.int8.bias.push.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-int8-bias-push" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/matmul/netlist_matmul_op_with_z_accumulate.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul_op_test_configs.int8.accumulate-z.push.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-int8-accumulate-z-push" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/matmul/netlist_matmul_ident_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul_ident_op_test_configs.int8.push.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-ident-int8-push" \
    --use-hash "$USE_HASH"
