
To run Debuda from a Budabackend checkout, you need to be in a software development docker container.

1. Prerequisites (need to be done only once)
```
git clone <path to budabackend repo> budabackend
cd budabackend
git submodule update --init --recursive
make build_hw dbd
pip install -r dbd/requirements.txt
```

2. Run Debuda
```
dbd/debuda.py path-to-your-run-dir    # e.g. pybuda/tt_build/test_out
```
