#!/bin/bash
# parent_path variable allows this script to work from any directory.
# The script's parent dir and the repository dir are added to the PYTHONPATH.
# This allows one module to import code from another module, for example you can import from verif.common.test_utils
parent_path=$(dirname "${BASH_SOURCE[0]}")
abs_parent_path=$(realpath $parent_path)
verif_path=$(dirname $abs_parent_path)
repo_path=$(dirname $verif_path)
python3.8 -m venv $parent_path/env
bash -c "cd $parent_path; echo -e '\nexport PYTHONPATH=\$PYTHONPATH:$repo_path:$abs_parent_path\n' >> env/bin/activate && source env/bin/activate && pip3 install -r requirements.txt --no-cache-dir"