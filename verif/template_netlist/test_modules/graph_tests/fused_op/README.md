### Fused op test generation using modules in this directory

The following command generates netlists for the passed module name. Each module has its own netlist template for which it generates configurations.

`python3 verif/template_netlist/generate_tests.py --module-name test_modules.graph_tests.fused_op.<module_name> --output-dir <output_dir> --arch <arch_name> --dry-run --dump-config-yaml --search-type  parallel-sweep --max-num-configs <max_num> --random-seed <random_seed> --timeout 60 --enable-strategy --harvested-rows 2`

To clarify some of the options:

- --dump-config-yaml - we need the config file as we don't check in final netlists, we check in just the configurations and the netlists are generated on the CI before the tests are executed
- --harvested-rows 2 - generated netlists can be run on the harvested machines with up to 2 harvested rows
