#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import numpy as np
from sklearn.linear_model import LinearRegression
import pandas as pd

def get_mblock_m(data):
    assert "mblock_m" in data or "output_mb_r" in data
    if "mblock_m" in data:
        return np.array(data["mblock_m"])
    else:
        return np.array(data["output_mb_r"])

def get_mblock_n(data):
    assert "mblock_n" in data or "output_mb_c" in data
    if "mblock_n" in data:
        return np.array(data["mblock_n"])
    else:
        return np.array(data["output_mb_c"])
    
def get_mblock_k(data):
    assert "mblock_k" in data or "m_k" in data
    if "mblock_k" in data:
        return np.array(data["mblock_k"])
    else:
        return np.array(data["m_k"])

def get_ublock_rt(data):
    assert "ublock_rt" in data or "output_ub_r" in data
    if "ublock_rt" in data:
        return np.array(data["ublock_rt"])
    else:
        return np.array(data["output_ub_r"])

def get_ublock_ct(data):
    assert "ublock_ct" in data or "output_ub_c" in data
    if "ublock_ct" in data:
        return np.array(data["ublock_ct"])
    else:
        return np.array(data["output_ub_c"])

def get_ublock_kt(data):
    assert "ublock_kt" in data or "u_kt" in data
    if "ublock_kt" in data:
        return np.array(data["ublock_kt"])
    else:
        return np.array(data["u_kt"])

def get_t(data):
    assert "t" in data or "output_t" in data
    if "t" in data:
        return np.array(data["t"])
    else:
        return np.array(data["output_t"])

def get_runtime(data):
    assert "runtime" in data or "total-runtime" in data
    if "runtime" in data:
        return np.array(data["runtime"])
    else:
        total_runtime = np.array(data["total-runtime"])
        first_entry = np.array(data["first-input-idx-recorded"])
        last_entry = np.array(data["last-input-idx-recorded"])
        runtime = total_runtime / (last_entry - first_entry + 1)
        return runtime

def run_matmul_perf_regression(data, print_results):
    print(list(data.keys()))

    mblock_m = get_mblock_m(data)
    mblock_n = get_mblock_n(data)
    ublock_rt = get_ublock_rt(data)
    ublock_ct = get_ublock_ct(data)
    m_k = get_mblock_k(data)
    u_kt = get_ublock_kt(data)
    runtime = get_runtime(data)

    # Formula m_k_input * w0 + mblock_input * w1 + ublock_input * w2

    m_k_input = m_k - 1.0
    mblock_input = m_k * mblock_m * mblock_n
    ublock_input = mblock_input * ublock_rt * ublock_ct * u_kt

    X = np.stack((m_k_input, mblock_input, ublock_input)).transpose()
    # print(X)
    Y = runtime

    regr = LinearRegression(fit_intercept=False, positive=True).fit(X, Y)

    predict = regr.predict(X)
    mse = np.square(predict - Y).mean()

    if print_results:
        print("mse", mse)
        print("mean", np.mean(Y / predict))
        print("std", np.std(Y / predict))
        print("predict", np.stack((predict, Y)).transpose())
        print("error", (100*(predict-Y)/Y).astype(int))

        print("intercept", regr.intercept_)
        print("coef", regr.coef_)
    
    return {
        "m_k_weight": float(regr.coef_[0]),
        "ublock_weight": float(regr.coef_[1]),
        "tile_weight": float(regr.coef_[2]),
    }

def run_matmulV2_perf_regression(data, print_results):
    df_size_map = {"Bfp8": 1, "Bfp8_b": 1, "Float16": 2, "Float16_b": 2}
    fidelity_map = {"LoFi": 1, "HiFi2": 2, "HiFi3": 3, "HiFi4": 4}
    print(list(data.keys()))
    assert "math_fidelity" in data

    mblock_m = get_mblock_m(data)
    mblock_n = get_mblock_n(data)
    ublock_rt = get_ublock_rt(data)
    ublock_ct = get_ublock_ct(data)
    m_k = get_mblock_k(data)
    u_kt = get_ublock_kt(data)
    runtime = get_runtime(data)

    df_bytes = [df_size_map[item] for item in data["input1_df"] if item in df_size_map]
    df_bytes = np.array(df_bytes)

    fidelity = [fidelity_map[item] for item in data["math_fidelity"] if item in fidelity_map]
    fidelity = np.array(fidelity)

    mk_input = m_k - 1.0
    ublocks = m_k * mblock_m * mblock_n

    math_tiles = ublocks * ublock_rt * ublock_ct * u_kt
    math_tiles_scaled_by_fidelity = math_tiles * fidelity

    math_dest_spill_ublocks = mk_input * mblock_m * mblock_n
    math_dest_spill_tiles = math_dest_spill_ublocks * ublock_rt * ublock_ct

    # DEBUG ONLY -- testing the usefulness of these two math components
    # math_x = np.stack((math_tiles_scaled_by_fidelity, math_dest_spill_tiles)).transpose()
    # math_y = runtime * (math_util / 100.0)
    # regr = LinearRegression(fit_intercept=False, positive=True).fit(math_x, math_y)
    # predict = regr.predict(math_x)
    # mse = np.square(predict - math_y).mean()
    # print("mse", mse)
    # print("mean", np.mean(math_y / predict))
    # print("std", np.std(math_y / predict))
    # print("error", (100*(predict-math_y)/math_y).astype(int))
    # print("intercept", regr.intercept_)
    # print("coef", regr.coef_)

    unpack_tiles_per_ub = u_kt * ublock_rt * (1 + ublock_ct)
    unpack_tiles = ublocks * unpack_tiles_per_ub

    X = np.stack((math_tiles_scaled_by_fidelity, math_dest_spill_tiles, ublocks, unpack_tiles)).transpose()
    Y = runtime

    regr = LinearRegression(fit_intercept=False, positive=True).fit(X, Y)

    predict = regr.predict(X)
    mse = np.square(predict - Y).mean()
    
    if print_results:
        print("mse", mse)
        print("mean", np.mean(Y / predict))
        print("std", np.std(Y / predict))
        print("predict", np.stack((predict, Y)).transpose())
        print("error", (100*(predict-Y)/Y).astype(int))
        print("intercept", regr.intercept_)
        print("coef", regr.coef_)

    return {
        "math_tiles_weight": float(regr.coef_[0]),
        "math_dest_spill_weight": float(regr.coef_[1]),
        "unpack_ublocks_weight": float(regr.coef_[2]),
        "unpack_tiles_weight": float(regr.coef_[3]),
    }

def run_eltwise_perf_regression(data, print_results):
    assert "runtime" in data or "total-runtime" in data
    mblock_m = get_mblock_m(data)
    mblock_n = get_mblock_n(data)
    ublock_rt = get_ublock_rt(data)
    ublock_ct = get_ublock_ct(data)
    runtime = get_runtime(data)

    # Formula mblock_input * w0 + ublock_input * w1

    mblock_input = mblock_m * mblock_n
    ublock_input = mblock_input * ublock_rt * ublock_ct

    X = np.stack((mblock_input, ublock_input)).transpose()
    # print(X)
    Y = runtime

    regr = LinearRegression(fit_intercept=False, positive=True).fit(X, Y)

    predict = regr.predict(X)
    mse = np.square(predict - Y).mean()

    if print_results:
        print("mse", mse)
        print("mean", np.mean(Y / predict))
        print("std", np.std(Y / predict))
        print("predict", np.stack((predict, Y)).transpose())
        print("error", (100*(predict-Y)/Y).astype(int))
        print("intercept", regr.intercept_)
        print("coef", regr.coef_)

    return {
        "ublock_weight": float(regr.coef_[0]),
        "tile_weight": float(regr.coef_[1])
    }

def parse_perf_sweep_results(csv_path, filter):
    results = pd.read_csv(csv_path)

    for filter_key, filter_value in filter.items():
        results = results[results[filter_key] == filter_value]

    return results.to_dict(orient='list')


def main():
    models = {
        "matmul": run_matmul_perf_regression,
        "matmulV2": run_matmulV2_perf_regression,
        "depthwise": run_matmulV2_perf_regression,
        "eltwise": run_eltwise_perf_regression,
    }

    parser = argparse.ArgumentParser(
        description="Generate performance model coeficients from real device perf data"
    )
    parser.add_argument(
        "--model", required=True, choices=sorted(models.keys()), help="path to perf csv"
    )
    parser.add_argument("csv_path", help="path to perf csv")
    state = parser.parse_args()
    data = parse_perf_sweep_results(state.csv_path)
    models[state.model](data, print_results=True)
    return 0


if __name__ == "__main__":
    import sys

    sys.exit(main())
