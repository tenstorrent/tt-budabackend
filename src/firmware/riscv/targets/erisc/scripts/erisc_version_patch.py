# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from re import X
import subprocess

def semver_to_hex(semver: str):
    """Converts a semantic version string from format 10.15.1 to hex 0x0A0F0100"""
    entity, release, customer, debug = semver.split('.')
    byte_array = bytearray([int(entity), int(release), (int(customer) & 0xF )<<4 | ((int(debug) & 0xFFF) >>8), int(debug) & 0xFF])
    return f"{int.from_bytes(byte_array, byteorder='big'):08x}"

def date_to_hex(date: str):
    """Converts a given date string from format YYYYMMDDHHMM to hex 0xYMDDHHMM"""
    year = int(date[0:4]) - 2020 # Year counting from 2020
    month = int(date[4:6])
    day = int(date[6:8])
    hour = int(date[8:10])
    minute = int(date[10:12])
    byte_array = bytearray([year*16+month, day, hour, minute])
    return f"{int.from_bytes(byte_array, byteorder='big'):08x}"

toplevel = subprocess.check_output(["git", "rev-parse", "--show-toplevel"]).strip().decode("ascii")
erisc_fw = f"{toplevel}/src/firmware/riscv/targets/erisc/src"
#erisc_fw = f"{toplevel}/src/firmware/riscv/wormhole/epoch_q.h"


modified = "0000"
py2output = subprocess.check_output(['git','status','--porcelain', '-uno', erisc_fw]).decode('ascii', 'ignore')
if py2output:
    modified = "FFFF"

sha = subprocess.check_output(['git', 'log', '-1', '--pretty=format:%H', erisc_fw]).strip().decode('ascii')

date = subprocess.check_output(['git', 'show', '-s', '--date=format:%Y%m%d%H%M', '--format=%ad', sha]).strip().decode('ascii')
hexdate = date_to_hex(date)

semver = "6.6.7.0" # TODO: Need to find a better place for this
hexsemver = semver_to_hex(semver)

print()
print("FW Version:")
print(f"- Commit SHA  : {sha[0:12]}")
print(f"- Commit Date : {date} (0x{hexdate})")
print(f"- Version #   : {semver} (0x{hexsemver})")
print()

with open(f'release/version.h', 'w') as f:
    f.write("#ifndef _VERSION_H_\n")
    f.write("#define _VERSION_H_\n\n")
    f.write("#define FW_VERSION_HIGH 0x" + modified + sha[0:4] + "\n")
    f.write("#define FW_VERSION_LOW  0x" + sha[4:12] + "\n")
    f.write("#define FW_VERSION_DATE 0x" + hexdate + "\n")
    f.write("#define FW_VERSION_SEMANTIC 0x" + hexsemver + "\n\n")
    f.write("#endif\n")

if modified == "FFFF":
    print (f"FW version successfully patched -\33[31m ERISC FW folder has changes \33[0m")
else:
    print (f"FW version successfully patched -\33[32m Clean build of ERISC FW \33[0m")
