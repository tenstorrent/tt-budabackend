## Global device/test constraints​
- verif/op_tests/constraints/vcs/src/{grayskull|wormhole_b0}/global_constraints.sv

        per SOC constraints that apply to every op e.g. core grid size, L1 memory size, max block dims etc.  ​
- verif/op_tests/constraints/vcs/src/{grayskull|wormhole_b0}/test_config.sv

        expected pcc/pct/tolerance settings for different data format per op ​
## OP constraints
- verif/op_tests/constraints/vcs/src/{DEVICE}/{op}/{op}_{type}_constraints.sv​

        Randomizes data formats, block dims, grid size, grid location, input count, addresses, types, modes 

## Netlist templates
- verif/op_tests/netlists/templates/{grayskull|wormhole_b0}/{op}/netlist_{op}_{type}.template.yaml​

## Machine setup
- Find machine that has setup for VCS license e.g. yyzeon04​
- Setup VCS

        source /tools_soc/tt/Modules/init/profile.sh
        module load synopsys/vcs
        module load synopsys/verdi

## Steps to generate random netlists
- Go to verif/op_tests/constraints/vcs. Makefile has following arguments
    - DEVICE=grayskull|wormhole_b0​
    - HARVESTED_ROWS= number of harvested rows on the architecture
    - LOOPS= number of random params to generate (number of random netlists)​
    - C= path to the constraint file
    - TLIST= name of the random params file to generate
    - SEED= seed to use
    - ARGS = constraint specific arguments
- Examples for generating test config yaml

        make DEVICE=grayskull LOOPS=50 C=src/grayskull/unary/unary_op_constraints.sv TLIST=nightly​
    Generates configs/grayskull/unary/unary_op_test_configs.nightly.yaml which contains param values for 50 netlists. Name of the output file is always {op_name}._test_configs.{TLIST}.yaml

        make DEVICE=wormhole_b0 LOOPS=25 C=src/wormhole_b0/tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-vstack.nightly ARGS='+define+NUM_FORKS=4 +vstack=1’​
    Generates configs/wormhole_b0/tm/test_datacopy_fork_reblock_datacopy_test_configs.fork4-vstack.nightly.yaml which contains param values for 25 netlists. Additional arguments are used to configure constraints for 4 forks and use vstack tm when generating param values

        make DEVICE=grayskull LOOPS=50 C=src/grayskull/binary/binary_op_constraints.sv TLIST=gradient_acc.nightly ARGS='+gradient_acc=1’​

- Generate netlists

        verif/template_netlist/generate_netlists.py --template-yaml verif/op_tests/netlists/templates/grayskull/unary/netlist_unary_op.template.yaml --test-configs-yaml verif/op_tests/constraints/vcs/configs/grayskull/unary/unary_op_test_configs.nightly.yaml --output-dir build/test/unary-nightly​
    Generates netlists in build/test/unary-nightly  using random param values from generated test config tile

## Regenerate all of the test configs
    bash verif/op_tests/constraints/vcs/scripts/regen_op_tests.sh
    bash verif/op_tests/constraints/vcs/scripts/gen_tm_fork_test_configs.sh
    bash verif/op_tests/constraints/vcs/scripts/regen_op_tests_int8.sh

## Ex. Generate netlists and run tests for Int8
Prerequisites to build *test_op*(```make -j32 verif/op_tests```).

```bash
# Generate netlists from config files.
# Netlists are stored under build/tests/int8
verif/template_netlist/generate_netlists_int8_push.sh

# Run test op for all netlists in folder build/tests/int8
verif/op_tests/scripts/run_tests.sh build/test/int8
```

