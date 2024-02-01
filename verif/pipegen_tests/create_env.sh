#!/bin/bash
cat ./requirements.system | xargs sudo apt install
python3 -m venv env
bash -c "echo -e '\nexport PYTHONPATH=$PWD\n' >> env/bin/activate && source env/bin/activate && pip3 install -r requirements.txt --no-cache-dir"
