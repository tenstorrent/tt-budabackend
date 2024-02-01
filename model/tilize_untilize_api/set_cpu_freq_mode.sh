#!/bin/bash

# Check if mode argument is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <mode>"
    exit 1
fi

mode=$1
cpus=$(nproc)

for i in $(seq 0 $((cpus-1)))
do
    echo "$mode" | sudo tee /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor
    #cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor
done
