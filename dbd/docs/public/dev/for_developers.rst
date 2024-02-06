For developers
==============

Smoke test
----------

Run a very basic test of debuda with: ``dbd/test/test-debuda-py.sh``. This test will run a simple
netlist and start debuda.py.

Offline use (--server-cache)
----------------------------

It is sometimes desirable to run debuda without having a connection to a running device. For this purpose, Debuda can use a cache file. An example usage scenario could be:

- A customer runs Debuda with `--server-cache through` and sends the resulting cache file to Tenstorrent for debugging
- A Tenstorrent engineer uses Debuda locally with `--server-cache on` to load the cache file and debug the design.

Note: not all commands will be available to a Tenstorrent engineer. Naturally, any command that writes to the device will not accomplish much. Any command that reads from an area of the device that was not cached will also fail. Therefore, the customer run of Debuda should read from all the relevant device areas as that will store those areas in the cache.

Currently, this functionality is supported through a fairly manual flow (perhaps by using --commands for some automation). In the future, the feature will be expanded to automatically dump all relevant chip areas (DRAM, L1, registers...)

Offline tests
-------------

Offline tests are stored in ``dbd/test/exported-runs``. They are in binary form. Run the following command
to extract them: ``dbd/test/exported-runs/unpack.sh``.

After unzipping, you should go into the test directory
and start debuda.py from there. E.g.:
``cd test/exported-runs/simple-matmul-no-hangs && ../../../debuda.py --server-cache on``

Low Level Debug Messages
------------------------

Low level messages (debug and trace) are printed in debuda-server source code with the following calls:

.. code-block::

    log_trace(tt::LogDebuda, ...)
    log_debug(tt::LogDebuda, ...)

Make sure all relevant source code is compiled with ``CONFIG=debug`` in the environment to enable
trace and debug messages.
When running debuda-server, add ``LOGGER_LEVEL=Trace`` or ``LOGGER_LEVEL=Debug`` to enable the messasges.

Cleaning and recompiling debuda-server
--------------------------------------

To clean and recompile debuda-server, run ``CONFIG=debug dbd/bin/rebuild-debuda-server.sh``.

Visual Studio Code
------------------

Debuda comes with specific debug targets for VS Code. These are configured in ``dbd/launch.json``. You
need add the ``dbd`` folder to workspace to make them available.

Debugging debuda-server
-----------------------

Add this configuration to your main budabackend `launch.json`:
```
    "configurations": [
        {
            "name": "GDB debuda-server",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/debuda-server-standalone",
            "args":
            [
                "5555",
                "tt_build/test_op_10997462431124419207/runtime_data.yaml",
            ],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [
                {
                    "name": "TT_PCI_LOG_LEVEL",
                    "value": "2"
                }
            ],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
            ],
            "miDebuggerPath": "/usr/bin/gdb",
        },
```
You will have to modify the args to point to the correct runtime_data.yaml file.
