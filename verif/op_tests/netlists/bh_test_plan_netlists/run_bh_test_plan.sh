
#!/bin/bash

git_root="$(git rev-parse --show-toplevel)"
arch_name=
verif_dir_path=${git_root}/verif/op_tests/netlists/bh_test_plan_netlists/

if [ "$#" -eq  "0" ]
    then
    arch_name=wormhole_b0
    mode=func
elif [ "$#" -eq  "1" ]
    then
    arch_name=$1
    mode=func
elif [ "$#" -eq  "2" ]
    then
    arch_name=$1
    mode=$2
fi

cd ${git_root}
mkdir out
export ARCH_NAME=${arch_name}
export BACKEND_VERSIM_STUB=0
export TT_BACKEND_TIMEOUT=10000
perf_args="--perf-level 0 --dump-perf-events --timeout ${TT_BACKEND_TIMEOUT}"

if [ "$arch_name" = "wormhole_b0" ]; then
	export BACKEND_VERSIM_FULL_DUMP=1
fi

if [ "$mode" = "func" ]; then
  verif_dir_path+=hlk_functional_tests/
elif [ "$mode" = "perf_f0_p1" ]; then
  verif_dir_path+=hlk_perf_tests/feeder_in0_prologue_in1/
elif [ "$mode" = "perf_feeder" ]; then
  verif_dir_path+=hlk_perf_tests/feeder_inputs/
elif [ "$mode" = "perf_prog" ]; then
  verif_dir_path+=hlk_perf_tests/prologued_inputs/
fi

make clean 
make build_hw verif/op_tests

# ./build/test/verif/op_tests/test_op --netlist ${verif_dir_path}/matmul_bfp4_lofi.yaml ${perf_args} --outdir out/matmul_bfp4_lofi_input_4_${arch_name} |& tee matmul_bfp4_lofi_${arch_name}.log
# ./build/test/verif/op_tests/test_op --netlist ${verif_dir_path}/matmul_bfp8_lofi.yaml ${perf_args} --outdir out/matmul_bfp8_lofi_input_4_${arch_name} |& tee matmul_bfp8_lofi_${arch_name}.log
list_names=`find ${verif_dir_path} -name "*.yaml" -type f -exec basename \{} .yaml \;  `
for entry in $list_names
do

  echo "-----------------------------------------------------------------------------------------------------------------------------------"
  echo "RUNNING TEST: ${entry}"
  
  ./build/test/verif/op_tests/test_op --netlist ${verif_dir_path}/${entry}.yaml ${perf_args} --outdir out/${entry}_${arch_name} > ${entry}_${arch_name}.log 2>&1 
  
  if grep -q "Test Passed" ${entry}_${arch_name}.log; then
    echo "-----------------------------------------------------------------------------------------------------------------------------------"
    echo "Test: ${entry} -> PASS"
    echo "-----------------------------------------------------------------------------------------------------------------------------------"
  else
    echo "-----------------------------------------------------------------------------------------------------------------------------------"
    echo "Test: ${entry} -> FAIL"
    echo "-----------------------------------------------------------------------------------------------------------------------------------"
  fi

done
