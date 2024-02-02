#!/bin/bash
# The presence of parent_path makes this script work from any directory.
parent_path=$(dirname "${BASH_SOURCE[0]}")
cat $parent_path/requirements.system | xargs sudo apt install
python3 -m venv $parent_path/env
bash -c "cd $parent_path; echo -e '\nexport PYTHONPATH=$PWD\n' >> env/bin/activate && source env/bin/activate && pip3 install -r requirements.txt --no-cache-dir"
