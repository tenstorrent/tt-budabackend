#!/bin/bash

TEST_MODULES=`find test_modules -name "test*.py" | xargs basename -a -s .py | sort`
#TEST_MODULES="test_dram_input_matmul_vstack_reblock test_datacopy_reblock_datacopy"

for t in $TEST_MODULES
do
  echo -n "${t}:"
  ./generate_tests.py --output-dir test_large_sweep --module-name test_modules.${t}  --max-num-configs 10 > ${t}.log 2>&1
  echo "`grep 'Status: Pass' ${t}.log | wc -l`"
done
