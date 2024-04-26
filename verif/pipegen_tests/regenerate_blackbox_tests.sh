#!/bin/bash
# Script which regenerates all baseline.zip files found in netlists/ folder.
# You can use the commands here as an example or just run the whole script.
# This will destroy your build folder.
# You should run this script from repo root, like so:
# verif/pipegen_tests/regenerate_blackbox_tests.sh

GS="grayskull"
WH="wormhole_b0"
TEMPDIR="/localdev/$USER/work/temp_blackbox_regen"

bash verif/pipegen_tests/create_env.sh &&
source verif/pipegen_tests/env/bin/activate &&

rm -rf build $TEMPDIR &&
ARCH_NAME=$GS make -j64 --debug build_hw &&
python3 verif/pipegen_tests/pipegen_blackbox_test.py --command update --baseline-file verif/pipegen_tests/netlists/$GS/push/baseline.zip --out-dir $TEMPDIR --arch $GS &&
rm -rf build/test $TEMPDIR &&
python3 verif/pipegen_tests/pipegen_blackbox_test.py --command update --baseline-file verif/pipegen_tests/netlists/$GS/nightly/baseline.zip --out-dir $TEMPDIR --arch $GS &&

rm -rf build $TEMPDIR &&
ARCH_NAME=$WH make -j64 --debug build_hw &&
python3 verif/pipegen_tests/pipegen_blackbox_test.py --command update --baseline-file verif/pipegen_tests/netlists/$WH/push/baseline.zip --out-dir $TEMPDIR --arch $WH &&
rm -rf build/test $TEMPDIR &&
python3 verif/pipegen_tests/pipegen_blackbox_test.py --command update --baseline-file verif/pipegen_tests/netlists/$WH/nightly/baseline.zip --out-dir $TEMPDIR --arch $WH &&

rm -rf build $TEMPDIR

if [ $? -eq 0 ]; then
  echo -e "\n\nAll commands successful!"
else
  echo -e "\n\nSomething failed, script stopped."
fi
