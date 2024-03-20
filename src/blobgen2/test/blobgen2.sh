#!/bin/bash

# blobgen1 command
# ruby .//src/overlay/blob_gen.rb
#   --blob_out_dir ./tt_build/test_graph_3841463605768854442/temporal_epoch_0/overlay/
#   --graph_yaml 1
#   --graph_input_file ./tt_build/test_graph_3841463605768854442/temporal_epoch_0/overlay/blob.yaml
#   --graph_name pipegen_epoch0
#   --noc_x_size 2
#   --noc_y_size 2
#   --chip grayskull
#   --noc_version 1
#   --tensix_mem_size 1048576

# get arguments
if [ "$#" -gt 0 ]; then
    # blob.yaml passed as an argument
    GRAPH_INPUT_FILE=$1
else
    # default blob.yaml
    GRAPH_INPUT_FILE='src/blobgen2/test/blob.yaml'
fi

# blobgen2 command
cmd="./build/bin/blobgen2 \
  --blob_out_dir tt_build/test_graph_blobgen2_test \
  --graph_yaml 1 \
  --graph_input_file $GRAPH_INPUT_FILE \
  --graph_name pipegen_epoch0 \
  --noc_x_size 2 \
  --noc_y_size 2 \
  --chip grayskull \
  --noc_version 1 \
  --tensix_mem_size 1048576 \
  --output_hex 1 \
  --output_yaml 1 \
  --blob_yaml_version 1"

#echo "$cmd"
eval "$cmd"
