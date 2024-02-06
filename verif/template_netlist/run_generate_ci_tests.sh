#!/bin/bash
cd verif/template_netlist
if [[ -z "$SEED" ]]; then
        SEED=$(git rev-parse --short HEAD)
        SEED=$((16#$SEED))
        SEED=${SEED#-}
        
else
        SEED="$SEED"
fi

echo "Random seed is:" $SEED

if [ $# -eq 0 ]; then
    echo "Usage: run_generate_ci_tests.sh <test_module> <output_dir> <num_tests> <num_loops> <determinism-tests>"
    echo "     <test_module>         - python test module that defines constraints of the test"
    echo "     <output_dir>          - directory path where the generated tests will be stored"
    echo "     <num_tests>           - number of tests to generate (default: 50)"
    echo "     <num_loops>           - number of loops a program is run for each test (default: 1)"
    echo "     <determinism_tests>   - run determinism tests (default: false)"
    exit 1
fi


if [ $# -eq 1 ] || { [ $# -gt 4 ] && [[ "$*" != *"determinism-tests"* ]]; } || [ $# -gt 5 ]; then
        echo "Wrong number of arguments provided"
        exit 1
fi

if [ $# -eq 5 ]; then
         ./generate_ci_tests.py --test-module ${1} --output-dir ${2} --num-tests ${3} --num-loops ${4} --determinism-tests --random-seed $SEED --arch $ARCH_NAME
elif [ $# -eq 4 ]; then
        ./generate_ci_tests.py --test-module ${1} --output-dir ${2} --num-tests ${3} --num-loops ${4} --random-seed $SEED --arch $ARCH_NAME
elif [ $# -eq 3 ]; then
        ./generate_ci_tests.py --test-module ${1} --output-dir ${2} --num-tests ${3} --random-seed $SEED --arch $ARCH_NAME
else
        ./generate_ci_tests.py --test-module ${1} --output-dir ${2} --random-seed $SEED --arch $ARCH_NAME
fi

unset SEED
