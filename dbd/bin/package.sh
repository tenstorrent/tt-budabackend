#!/bin/bash
# This script is used to create a standalone release of Debuda
set -e

# Validate git root
[ -d .git ] || { echo "ERROR: Not in git root"; exit 1; }

# Validate parameter
[ $# -eq 1 ] || { echo "ERROR: Need output directory"; exit 1; }
DBD_OUT=`realpath $1`

# Create staging folder
STAGING_DIR="$DBD_OUT/staging"
rm -rf $STAGING_DIR
mkdir -p $STAGING_DIR

# Build
make -j32 build_hw verif/op_tests dbd

# Copy LIB_FILES to the staging dir
LIB_FILES="./build/lib/libtt.so ./build/lib/libdevice.so"
for i in $LIB_FILES; do cp -f $i $STAGING_DIR; done

# Copy debuda-server-standalone to staging directory
cp ./build/bin/debuda-server-standalone $STAGING_DIR/debuda-server-standalone

# Copy compiled files and others to staging
cp dbd/README.txt dbd/requirements.txt dbd/debuda.py dbd/tt* $STAGING_DIR
cp build/dbd/debuda-help.pdf $STAGING_DIR/debuda-manual.pdf

# Filter NON_DEV_COMMAND_FILES
NON_DEV_COMMAND_FILES=""
for i in `ls dbd/debuda_commands -p | grep -v /`; do
    grep -q '"type" : "dev"' dbd/debuda_commands/$i || NON_DEV_COMMAND_FILES="$NON_DEV_COMMAND_FILES dbd/debuda_commands/$i"
done
mkdir -p $STAGING_DIR/debuda_commands
cp $NON_DEV_COMMAND_FILES $STAGING_DIR/debuda_commands

# Create zip
cd $STAGING_DIR
zip $DBD_OUT/debuda.zip * debuda_commands/* -x "debuda_commands/*test*" -x "debuda_commands/__pycache__"
cd -

# move the file to /home_mnt/ihamer/work/debuda-releases and rename it to inlude date and git hash
NAME=/home_mnt/ihamer/work/debuda-releases/debuda-`date +%Y%m%d`-`git rev-parse --short HEAD`.zip
cp -f $DBD_OUT/debuda.zip $NAME

# Show the contents of the dir in the order of last modified being at the bottom
echo "Contents of /home_mnt/ihamer/work/debuda-releases: "
ls -ltr /home_mnt/ihamer/work/debuda-releases

# Echo the target file
echo
echo "Released to: $NAME"
echo "DONE"

# Cleanup
# rm -rf $STAGING_DIR
