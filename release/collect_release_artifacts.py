# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os
import shutil

ARCH_AMD64 = "amd64"
ARCH_ARM64 = "arm64"
SUPPORTED_ARCHES = [ARCH_AMD64, ARCH_ARM64]

bbe_files = {
    "lib": {
        "path": "build/lib",
        "files": ["libtt.so", "libdevice.so"],
    },
    "backend_includes": {
        "path": "build/include",
        "files": [
            "tt_backend.hpp",
            "tt_backend_api_types.hpp",
        ],
    },
    "netlist_includes": {
        "path": "netlist",
        "target_path": "build/include",
        "files": [
            "tt_backend_api.hpp",
        ],
    },
    "common_includes": {
        "path": "common",
        "target_path": "build/include/common",
        "files": ["param_lib.hpp", "tti_lib.hpp", "env_lib.hpp"],
    },
    "device_includes": {
        "path": "device",
        "target_path": "build/include/device",
        "files": ["tt_arch_types.h", "tt_xy_pair.h"],
    },
    "perf_lib_includes": {
        "path": "perf_lib/op_model",
        "target_path": "build/include/perf_lib/op_model",
        "files": ["op_model.hpp", "op_params.hpp", "sparse_matmul_params.hpp"],
    },
    "third_party_includes": {
        "target_path": "build/include/third_party/json",
        "path": "third_party/json",
        "files": [
            "json.hpp",
        ],
    },
    "demos": {
        "path": "demos",
        "target_path": "../demos",  # /demos folder to be placed at the same level as /budabackend_lib
        "files": "*",
    },
    "bin": {
        "path": "build/bin",
        "files": ["net2pipe", "pipegen2", "op_model"],
    },
    "device_descriptors": {
        "path": "device",
        "files": [
            "grayskull_120_arch.yaml",
            "grayskull_10x12.yaml",
            "wormhole_8x10.yaml",
            "wormhole_80_arch.yaml",
            "wormhole_b0_8x10.yaml",
            "wormhole_b0_8x10_harvested.yaml",
            "wormhole_80_harvested.yaml",
            "wormhole_b0_80_arch.yaml",
            "wormhole_b0_80_harvested.yaml",
            "wormhole_b0_1x1.yaml",
            "grayskull_10x12.yaml",
        ],
    },
    "architecture_headers": {
        "path": "umd/device",
        "files": ["architecture.h", "xy_pair.h"],
        "target_path": "build/include/device",
    },
    "params": {"path": "perf_lib/op_model/params", "files": "*"},
    "device_silicon_wormhole_bin_x86": {
        "supported_arches": [ARCH_AMD64],
        "path": "./umd/device/bin/silicon/x86",
        "files": ["create-ethernet-map"],
    },
    "device_silicon_wormhole_bin_aarch64": {
        "supported_arches": [ARCH_ARM64],
        "path": "./umd/device/bin/silicon/aarch64",
        "files": ["create-ethernet-map"],
    },
    "misc": {"path": "infra", "files": ["common.mk"]},
    "firmware": {"path": "src/firmware/riscv", "files": "*"},
    "firmware_brisc_hex": {
        "supported_arches": [ARCH_AMD64],
        "path": "build/src/firmware/riscv/targets/brisc/out",
        "files": ["brisc.hex"],
    },
    "kernels": {
        "path": "src/ckernels",  # TODO clean up, maybe we don't need *everything* here?
        "files": "*",
    },
    "kernel_gen": {
        "supported_arches": [ARCH_AMD64],
        "path": "build/src/ckernels/gen/out",
        "files": "*",
    },
    "hlk": {
        "path": "hlks",
        "files": "*",
    },
    "perf_lib": {"path": "perf_lib", "files": ["scratch_api.h"]},
    "overlay": {
        "path": "tb/llk_tb/overlay",
        "files": "*",  # TODO, clean-up, don't need everything
    },
    "versim_lib": {  # TODO, remove
        "path": "common_lib",
        "files": "*",
    },
    "sfpi-compiler": {"path": "third_party/sfpi", "files": "*"},
    "llk": {"path": "third_party/tt_llk_" + os.environ["ARCH_NAME"], "files": "*"},
}

# Only copy eric if we are building Wormhole
if "BACKEND_ARCH_NAME" in os.environ and os.environ["BACKEND_ARCH_NAME"] == "wormhole":
    if "TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH" in os.environ:
        erisc_binaries_path = os.environ["TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH"]
    else:
        erisc_binaries_path = "build/src/firmware/riscv/targets/erisc_app/out"

    bbe_files["firmware_erisc_hex"] = {
        "supported_arches": [ARCH_AMD64],
        "path": erisc_binaries_path,
        "files": ["erisc_app.hex"],
    }

if (
    "BACKEND_ARCH_NAME" in os.environ
    and os.environ["BACKEND_ARCH_NAME"] == "wormhole_b0"
):
<<<<<<< HEAD
=======
    if "TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH" in os.environ:
        erisc_binaries_path = os.environ["TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH"]
    else:
        erisc_binaries_path = "build/src/firmware/riscv/targets/erisc_app/out"
>>>>>>> d542744496 (Added precompiled erisc binary hack.)
    bbe_files["firmware_erisc_hex"] = {
        "supported_arches": [ARCH_AMD64],
        "path": erisc_binaries_path,
        "files": [
            "erisc_app.hex",
            "erisc_app.iram.hex",
            "erisc_app.l1.hex",
            "split_iram_l1",
        ],
    }


def _copy_budabackend(target_folder, src_root, arch):
    if not src_root:
        src_root = ".."

    # Copy files
    for t, d in bbe_files.items():

        # Skip if files are not supported by current arch
        supported_arches = d.get("supported_arches")
        if supported_arches and arch not in supported_arches:
            continue

        target_path = d["target_path"] if "target_path" in d else d["path"]
        path = target_folder + "/budabackend_lib/" + target_path
        os.makedirs(path, exist_ok=True)

        src_path = src_root + "/" + d["path"]
        if d["files"] == "*":
            shutil.copytree(src_path, path, dirs_exist_ok=True)
        else:
            for f in d["files"]:
                shutil.copyfile(src_path + "/" + f, path + "/" + f)
    # Set file permissions to execute binaries
    for root, dirs, files in os.walk(target_folder):
        for d in dirs:
            dir_path = os.path.join(root, d)
            os.chmod(dir_path, 0o777)
        for f in files:
            file_path = os.path.join(root, f)
            os.chmod(file_path, 0o777)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--release_folder",
        required=True,
        type=str,
        help="Folder where to collect release artifacts.",
    )
    parser.add_argument(
        "--src_root",
        required=False,
        type=str,
        default="",
        help="Folder containing Budabackend build binaries relative to where the script is being called from.",
    )
    parser.add_argument(
        "--arch",
        required=False,
        type=str,
        default=ARCH_AMD64,
        help=f"Platform architecture {SUPPORTED_ARCHES}",
    )
    args = parser.parse_args()

    assert args.arch in SUPPORTED_ARCHES
    _copy_budabackend(args.release_folder, args.src_root, args.arch)
