#!/bin/bash

#program erisc firmware bin to all chips detected by get-wormhole-interfaces

export PATH=$PATH:device/bin/silicon/wormhole

for interface in $(get-wormhole-interfaces)
do
  flash-spi --interface $interface --args write_from_bin=0x23000,file=$1
done
