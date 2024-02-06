#!/bin/bash
sudo apt install bc
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Unzips the files such that if there is a directory already it will be used; otherwise a directory
# will created with the same basename of the zip
for f in $SCRIPT_DIR/*.zip
do
  NUM_UNIQ_FILES=`echo unzip -Z1 $f | tr '/' ' ' | awk '{ print $1 }' | sort -u | wc -l | bc`
  d=`basename $f .zip`

  if [ "$NUM_UNIQ_FILES" == "1" ]
  then
    unzip $f -d $SCRIPT_DIR
  else
    unzip $f -d $SCRIPT_DIR/$d
  fi
done
