#!/bin/bash
# This script is meant to be run from module.mk like this:  make dbd/docs/public
set -u
pip install -U docutils==0.18.1 pip install sphinx==6.2.0
echo Make sure to add the ~/.local/bin to you PATH: export PATH=$PATH:~/.local/bin
sphinx-build -M $BUILDER $SOURCE_DIR $INSTALL_DIR
