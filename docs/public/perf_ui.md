\page perf_ui Performance Visualizer

# Performance Visualizer (Perf-UI)

Performance visualizer (perf-ui) is a standalone tool that can be used to visualize the output of the performance postprocessor. The input to perf-ui is the `perf_results` directory that is by default written under `tt_build/<TEST_OUTPUT_DIRECTORY>`.

- Perf-ui can be installed on macbooks.
- The instructions below are based on the latest dmg `perf-ui v1.1.0` .

After launching the app, the following page will appear:

![Perf UI Front Page](images/perf_ui_front_page.png)

There are two options on the screen:

1. **Select a Workspace**: A `workspace` is a parent directory of test outputs. The directory structure perf-ui expects is `<WORKSPACE_DIR>/tt_build/<TEST_OUTPUT_DIRS>/perf_results` . To select a previously-added workspace, click on a workspace in the dropdown menu and click `Select` . To add a new workspace, click `Add New Workspace` .
2. **Launch without Workspace:** perf-ui can be launched without specifying a workspace. If this option is chosen, the user can select test output perf results from their local machine, but no remote selection will be enabled.

**Adding a New Workspace**

After clicking `Add New Workspace`, the following window will appear:

![Selecting a Remote Workspace](images/perf_ui_select_workspace.png)

On the top of the window, there are two tabs:

`Local` (use a directory on the local machine)

* The window will prompt the user to enter a directory `path` on the local machine. Clicking `Add` will set the workspace to that directory and also cache the directory as a known workspace that can be selected directly next time.

`Remote (SSH)` (use a directory on a remote machine through ssh)

* This window will prompt the user to enter a directory `path` , an `SSH Host`, and an `SSH Port` .
  * For the `SSH Port`, please use port 22. We have seen issues where remote selection fails when other ports (specifically IRD ports) are used.
* After providing the workspace information, click `Verify` . Perf-ui will try to initiate an ssh connection, and will verify that the directory path exists. Once verified, there will be two green check-marks next to `Connect` and `Check directory` . Click `Add` to set the workspace. Perf-ui will cache the workspace so that it can be selected directly next time.
* If there is an error when verifying a remote workspace, some common issues:
  * Are you connected to VPN?
  * Does the directory exist on the remote machine?
  * Is the host name/port number correct?
  * If you execute `ssh` commands in your shell on this host/port, do they work?
  * The host/port should be ssh-able without needing to enter a password or going through an ssh key fingerprint check.

**Selecting a Test to Visualize**

After workspace selection, the app will transition to this following page:

![Test Selection Panel](images/perf_ui_select_test.png)

There are two options on the screen:

1. **Local Selection**: Any perf_results directory on the local machine can be opened by this option. Select `Open Folder` option and navigate to the perf trace directory.
   - To copy the perf_results directory to the local machine, it is recommended to zip the directory first and then scp over to the local machine. This significantly reduces the time it takes to copy the perf reports:

```bash
# On the remote machine:
tar -czvf perf_results.tar.gz tt_build/<TEST_OUTPUT_DIR>/perf_results

# On the local machine:
scp <REMOTE_MACHINE>:<PATH_TO_ZIP_FILE_DIR>/perf_results.tar.gz ~/<LOCAL_MACHINE_PATH>
```

![Selecting a Local Test](images/perf_ui_local_select.png)

1. **Remote Selection**: All perf_results directories under `<WORKSPACE_DIR>/tt_build` will be listed in the drop-down menu. Click on the drop down menu, select a perf_results directory, and click `Plot` . The `Refresh` button will signal perf-ui to regenerate the drop-down menu with the latest perf_results directories. This is useful when a new test was run on the remote machine and the user wants to plot it on perf-ui.

**Visualization Panel**

* After opening the perf results directory the following page will appear, and the backend profiler (host) is initially loaded.

![Visualization Panel](images/perf_ui_host.png)

* On the left hand side, we can select epochs to add to the visualization. A selection can be removed by clicking on an epoch (or host) that is already selected.
* On the bottom of the screen we have a console that currently has 2 commands: `select <str>` and `version`.
  * `select <str>`: Highlights the event/op names that contain \<str\>. For example, `select matmul` will highlight all event/op names that have the phrase "matmul" in them.
  * `version` : Logs the version of the perf-ui app.

![Device Performance Trace](images/perf_ui_device.png)

- Using the input drop-down menu we can select which inputs that were recorded should be displayed.

![Selecting Inputs](images/perf_ui_inputs.png)

- For each op, the candle stick bar is created as depicted below:

![Candlestick Bar](images/perf_ui_candlestick.png)

- Clicking on each op, will expand that and displays all the Tensix cores that were running that op. In addition to the runtime (the green bar), for each core, `wait-for-tiles`, and `wait-for-free-tiles` events will also be depicted (If the test was run with perf-level \> 0).

![Wait For Tiles](images/perf_ui_wft.png)

- To zoom in/out on a perf trace, we can use scroll in/out.
- To zoom into a specific range of time, we can `right-click and drag` on any part of the screen:
- To drop a vertical pin: `Shift + left-click`
  - We can drop a second one, which also displays the diff between the two lines
  - To remove all pins: `Alt + Shift + left-click`

![Vertical Pin and Diff](images/perf_ui_diff.png)