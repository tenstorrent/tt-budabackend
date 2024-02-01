#!/bin/bash
GROUP_ID=$((1 + $RANDOM % 7))
unzip -qo verif/graph_tests/netlists/pregenerated/group_$GROUP_ID.zip -d build/test/verif/graph_tests/pregenerated/
touch build/test/verif/graph_tests/pregenerated/group_$GROUP_ID
find build/test/verif/graph_tests/pregenerated/ -name "test_configs.yaml" -type f -delete
