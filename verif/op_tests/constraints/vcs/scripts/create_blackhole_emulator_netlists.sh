#!/bin/bash

template_yaml="verif/op_tests/netlists/templates/wormhole_b0/netlist_op_test.template.yaml"
configs_yaml_dir="verif/op_tests/constraints/vcs/test_configs/blackhole/emulator"
output_dir="build/test/blackhole_emulator_tests"

for config_file in "$configs_yaml_dir"/**/*.yaml; do
    last_dir=$(dirname "$config_file" | awk -F '/' '{print $NF}')
    if grep -q "single" <<< "$config_file"; then
        output_subdir="$output_dir/$last_dir/single"
    elif grep -q "drainer" <<< "$config_file"; then
        output_subdir="$output_dir/$last_dir/drainer"
    else 
        output_subdir="$output_dir/$last_dir/feeder_drainer"
    fi
    mkdir -p "$output_subdir"
    verif/template_netlist/generate_netlists.py --template-yaml $template_yaml --test-configs-yaml $config_file --output-dir $output_subdir --use-hash 0
done
