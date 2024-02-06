#!/bin/bash
set -e

SMALL_READ="./build/test/loader/tests/test_pybuda_api --silicon --netlist loader/tests/net_perf/netlist_unary_dram_queue.yaml --num-pushes 4 --batched-push --skip-tile-check"
LARGE_READ="./build/test/loader/tests/test_pybuda_api --silicon --netlist loader/tests/net_perf/netlist_unary_dram_queue.yaml --num-pushes 4096 --skip-tile-check"

ARCH_NAME=(
'grayskull'
)

DMA_BUF_SIZE=(
'4096'
'8192'
'16384'
'32768'
'65536'
'131072'
'262144'
'524288'
'1048576'
'2097152'
'4194304'
)

for arch in "${ARCH_NAME[@]}"
do
    for buf in "${DMA_BUF_SIZE[@]}"
    do
        echo "--------------------------------------------------------------------------------------------------"

        logfile="${arch}_small_rd_${buf}.log"
        SMALL_READ_TEST="ARCH_NAME=${arch} TT_PCI_DMA_BUF_SIZE=${buf} $SMALL_READ 2>&1 > ${logfile}"
        SMALL_READ_PERF="grep 'Pop Bandwidth' $logfile"

        echo " - ARCH = ${arch}  READ SIZE = small"
        echo " - DMABUF SIZE = ${buf}"
        eval $SMALL_READ_TEST
        eval $SMALL_READ_PERF
    done
done

for arch in "${ARCH_NAME[@]}"
do
    for buf in "${DMA_BUF_SIZE[@]}"
    do
        echo "--------------------------------------------------------------------------------------------------"

        logfile="${arch}_large_rd_${buf}.log"
        LARGE_READ_TEST="ARCH_NAME=${arch} TT_PCI_DMA_BUF_SIZE=${buf} $LARGE_READ 2>&1 > ${logfile}"
        LARGE_READ_PERF="grep 'Pop Bandwidth' $logfile"

        echo " - ARCH = ${arch}  READ SIZE = large"
        echo " - DMABUF SIZE = ${buf}"
        eval $LARGE_READ_TEST
        eval $LARGE_READ_PERF
    done
done


printf "\n"
printf "All tests completed $status"
exit $result