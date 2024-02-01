# Debuda tests

Debuda tests run on silicon.

To run debuda tests locally:
```
Build : python3 ci/run.py build --local-run --arch_name grayskull --device_runner silicon --tag push
Run   : python3 ci/run.py run --local-run --test debuda-tests-grayskull --run_entire_testlist --device_runner silicon
```

All debuda test results can be found on [test dashboard](http://yyz-elk/goto/3ec66739aae83a0ac9242fea29db989d).

More information about adding test can be found in [CI Test Lists](./../../ci/test-lists/README.md)
