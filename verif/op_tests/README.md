# Op Tests
These tests are used to test basic kernel functionlity.
### Running Op tests
```bash
# Push tests
python3 ci/run.py run --local-run --test op_tests_wormhole_b0_silicon_push --run_entire_testlist --device_runner silicon
python3 ci/run.py run --local-run --test op_multi_tests_wormhole_b0_silicon_push --run_entire_testlist --device_runner silicon

# Nightly tests
python3 ci/run.py run --local-run --test op_tests_wormhole_b0_silicon_nightly --run_entire_testlist --device_runner silicon --tag nightly
python3 ci/run.py run --local-run --test op_tests_int8_wormhole_b0_silicon_nightly --run_entire_testlist --device_runner silicon --tag nightly
```