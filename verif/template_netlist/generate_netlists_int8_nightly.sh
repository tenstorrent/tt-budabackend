#!/bin/bash

# Define common paths
TEMPLATE_DIR="verif/op_tests/netlists/templates/wormhole_b0"
CONSTRAINTS_DIR="verif/op_tests/constraints/vcs"
OUTPUT_DIR_BASE="build/test/int8"
USE_HASH="0"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/splice/netlist_splice_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/splice/splice_op_test_configs.int8.hor.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/splice-int8-hor-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/splice/netlist_splice_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/splice/splice_op_test_configs.int8.ver.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/splice-int8-ver-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/reduce/netlist_reduce_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/reduce/reduce_op_test_configs.int8.dim-z.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/reduce-int8-dim-z-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/reduce/netlist_reduce_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/reduce/reduce_op_test_configs.int8.dim-r.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/reduce-int8-dim-r-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/reduce/netlist_reduce_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/reduce/reduce_op_test_configs.int8.dim-c.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/reduce-int8-dim-c-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/matmul/netlist_matmul_ident_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul_ident_op_test_configs.int8.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-ident-int8-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR//binary/netlist_binary_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/binary/binary_op_test_configs.int8.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/binary-int8-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/netlist_op_test.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/matmul/matmul.int8.quant.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/matmul-int8-quant-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/netlist_op_test.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/binary/binary_sfpu.int8.quant.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/binary-sfpu-int8-quant-nightly" \
    --use-hash "$USE_HASH"

verif/template_netlist/generate_netlists.py --template-yaml "$TEMPLATE_DIR/unary/netlist_unary_op.template.yaml" \
    --test-configs-yaml "$CONSTRAINTS_DIR/configs/wormhole_b0/unary/unary_op_test_configs.int8.nightly.yaml" \
    --output-dir "$OUTPUT_DIR_BASE/nop-int8-int32-nightly" \
    --use-hash "$USE_HASH"