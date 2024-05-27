# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import subprocess
import sys

__requires__ = ['pip >= 24.0']

from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext

# debuda files to be copied to build directory
dbd_folder_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
buda_home = os.path.dirname(dbd_folder_path)
debuda_files = {
    "debuda": {
        "path": "dbd",
        "files": [
            "debuda.py",
            "tt_buffer.py",
            "tt_commands.py",
            "tt_coordinate.py",
            "tt_debuda_context.py",
            "tt_debuda_server.py",
            "tt_debug_risc.py",
            "tt_device.py",
            "tt_firmware.py",
            "tt_gdb_communication.py",
            "tt_gdb_data.py",
            "tt_gdb_file_server.py",
            "tt_gdb_server.py",
            "tt_graph.py",
            "tt_grayskull.py",
            "tt_netlist.py",
            "tt_object.py",
            "tt_parse_elf.py",
            "tt_pipe.py",
            "tt_stream.py",
            "tt_temporal_epoch.py",
            "tt_util.py",
            "tt_wormhole.py"
        ],
        "output": "dbd"
    },
    "debuda_commands": {
        "path": "dbd/debuda_commands",
        "files": "*",
        "output": "dbd/debuda_commands"
    },
    "libtt.so": {
        "path": "build/lib",
        "files": [ "libtt.so", "libdevice.so", "tt_dbd_pybind.so" ],
        "output": "build/lib",
        "strip": True
    },
    "debuda-server-standalone": {
        "path": "build/bin" ,
        "files": [ "debuda-server-standalone", "debuda-create-ethernet-map-wormhole" ],
        "output": "build/bin",
        "strip": True
    }
}

class TTExtension(Extension):
    def __init__(self, name):
        Extension.__init__(self, name, sources=[])

class MyBuild(build_ext):
    def run(self):
        build_lib = self.build_lib
        if not os.path.exists(build_lib):
            print("Creating build directory", build_lib)
            os.makedirs(build_lib, exist_ok=True)

        self._call_build()

        # Copy the files to the build directory
        self._copy_files(build_lib)

    def _call_build(self):
        additional_env_variables = {
            "BUDA_HOME": buda_home,
        }
        env = os.environ.copy()
        env.update(additional_env_variables)
        nproc = os.cpu_count()
        print(f"make -j{nproc} dbd")
        subprocess.check_call([f"cd $BUDA_HOME && make -j{nproc} dbd"], env=env, shell=True)

    def _copy_files(self, target_path):
        strip_symbols = os.environ.get("STRIP_SYMBOLS", "0") == "1"
        for t, d in debuda_files.items():
            path = target_path + "/" + d["output"]
            os.makedirs(path, exist_ok=True)

            src_path = buda_home + "/" + d["path"]
            if d["files"] == "*":
                self.copy_tree(src_path, path)
            else:
                for f in d["files"]:
                    self.copy_file(src_path + "/" + f, path + "/" + f)
                    if d.get("strip", False) and strip_symbols:
                        print(f"Stripping symbols from {path}/{f}")
                        subprocess.check_call(["strip", path + "/" + f])

# Fake debuda extension
debuda_fake_extension = TTExtension("debuda.fake_extension")

with open("README.md", "r") as f:
    long_description = f.read()

# Add specific requirements for Debuda
with open(f"{dbd_folder_path}/requirements.txt", "r") as f:
    requirements = [r for r in f.read().splitlines() if not r.startswith("-r")]

short_hash = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('ascii').strip()
date = subprocess.check_output(['git', 'show', '-s', '--format=%cd', "--date=format:%y%m%d", 'HEAD']).decode('ascii').strip()

version = "0.1." + date + "+dev." + short_hash

setup(
    name='debuda',
    version=version,

    py_modules=['debuda'],
    package_dir={"debuda": "dbd"},

    author='Tenstorrent',
    url="http://www.tenstorrent.com",
    author_email='info@tenstorrent.com',
    description='AI/ML framework for Tenstorrent devices',
    python_requires='>=3.8',
    #long_description=long_description,
    #long_description_content_type="text/markdown",
    ext_modules=[debuda_fake_extension],
    cmdclass=dict(build_ext=MyBuild),
    zip_safe=False,
    install_requires=requirements,
    license="TBD",
    keywords="debugging tenstorrent",
    entry_points={
        'console_scripts': [
            'debuda = dbd.debuda:main'
        ]
    },
)
