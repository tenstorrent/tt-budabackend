Debuda is a standalone application. To use it, one must have a separate Budabackend checkout. This is not ideal, and we plan to make Debuda a part of the Pybuda wheel.

To run Debuda from a Budabackend checkout, you need to be in a software development docker container (e.g. by running ird reserve...)

1. Prerequisites (need to be done only once)
```
git clone git@yyz-gitlab.local.tenstorrent.com:tenstorrent/budabackend.git
cd budabackend
git submodule update --init --recursive
make build_hw dbd
pip install -r dbd/requirements.txt
```

2. Run Debuda
```
dbd/debuda.py path-to-your-run-dir    # e.g. pybuda/tt_build/test_out
```
