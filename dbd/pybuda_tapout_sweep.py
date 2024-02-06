#!/usr/bin/env python3
"""
Given a pytest command, this program determines the first Op that created erroneous results.
It does this by then iteratively running Buda backend with each run examining output of a different Op.
The Ops are examined in topological order from graph inputs to graph outputs. The execution of pytest
will save the stimulus so the Buda backend can run with the exact inputs that the pytest ran with.
"""
import json
import sys, os, argparse, subprocess
import tt_util as util
import tapout_sweep as tapout

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pytest_command', type=str, help='')
parser.add_argument('--dry-run', action='store_true', default=False, help=f'Debug')
parser.add_argument('--out_dir', type=str, default="tapout_output", help='Output directory')
parser.add_argument('--env', type=str, help='Additional environment variables in format --env \'{"PYBUDA_BALANCER_POLICY_TYPE":"MaximizeUtilization", "PYBUDA_BALANCER_ONE_ROW":"1"}\'')
args = parser.parse_args()
import pprint
pp = pprint.PrettyPrinter(indent=4)

ret_code = 0

BACKEND_RUNNER_EXE = 'build/test/verif/op_tests/test_op'
BACKEND_ARGS = '--seed 0 --silicon --timeout 500'

NETLIST_ID_STR="Parsing Netlist from file: "

def run(cmd_array, extra_env):
    my_env = dict(os.environ)
    for k, v in extra_env.items():
        my_env[k] = v

    ret_val = {
        "netlist_path" : "path-to-base-output-dir/pybuda_test_backend_test_bert_py_test_encoder_inference-Grayskull-cfg0-no_recompute/encoder_netlist.yaml",
    }

    if args.dry_run:
        print (f"DRY-RUN: {' '.join (cmd_array)} with ENV: {extra_env}")
        return ret_val

    try:
        with subprocess.Popen(cmd_array, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=my_env) as process:
            try:
                for line in process.stdout:
                    line = line.decode('utf8')
                    print(line, end ="")

                    if NETLIST_ID_STR in line:
                        filepath = line.split (NETLIST_ID_STR)[1].strip()
                        util.WARN (f"=========> Found netlist filepath: {filepath}")
                        ret_val["netlist_path"] = filepath
            except Exception as e:
                util.ERROR (e)
                process.terminate()

    except FileNotFoundError as e:
        util.WARN (e)
        util.INFO ("Make sure you have the environment setup:")
        print (f"source build/python_env/bin/activate")
        print (f"source env_for_silicon.sh")
        sys.exit (1)

    return ret_val

def run_pytest (run_cmd, env):
    util.INFO ("Running pytest")

    extra_env = {}
    if env:
        extra_env = json.loads(env)

    extra_env["PYBUDA_CI_CAPTURE_TENSORS"] = "1"
    extra_env["PYBUDA_CI_DIR"]=args.out_dir

    print(run_cmd)
    cmd_array = run_cmd.split(' ')

    ret_val = run (cmd_array, extra_env)

    return ret_val

def modify_netlist (netlist_path):
    # 1. Append test_debug
    new_netlist_path = netlist_path + ".modified.yaml"

    append_data = """
test-config:
  comparison-config:
    type: AllCloseHw
    atol: 0.01
    rtol: 0.15
    check_pct: 0.0
    check_pcc: 0.99
    verbosity: Concise
  stimulus-config:
    type: Uniform
    uniform_lower_bound: 0.01
    uniform_upper_bound: 0.25
"""

    f = open(netlist_path, "r")
    copy = open(new_netlist_path, "w")
    for line in f:
        if "param: [$p_loop_count]" in line:
            line = line.replace ("param: [$p_loop_count]", "var: {$p_loop_count: 1}")
        if "param: [$p_microbatch_count]" in line:   # TODO: fix that
            line = line.replace ("param: [$p_microbatch_count]", "var: {$p_microbatch_count: 1}")
        copy.write(line)
    copy.write (append_data)
    f.close()
    copy.close()

    return new_netlist_path


def run_sweep (test_data):
    util.INFO (f"Running sweep with {test_data}")

    if not os.path.exists(BACKEND_RUNNER_EXE):
        util.INFO ("You need to have test_op, as we use it for backend runs")
        print ("make -j32 verif/op_tests/test_op")
        sys.exit (1)

    pytorch_bin = os.path.dirname(os.path.abspath(test_data['netlist_path']))
    tapout_args = f"{BACKEND_RUNNER_EXE} {BACKEND_ARGS} --netlist {test_data['netlist_path']} --pytorch-bin {pytorch_bin}"
    tapout.TapoutCommandExecutor(tapout_args, args.out_dir).run()

test_data = run_pytest(args.pytest_command, args.env)
test_data["netlist_path"] = modify_netlist (test_data["netlist_path"])
run_sweep (test_data)

sys.exit (ret_code)