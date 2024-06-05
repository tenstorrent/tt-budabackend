# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  export [<file-name>]

Arguments:
  file-name     Name of the zip file to export to. Default: 'debuda-export.zip'

Description:
  The result of the *export* command is similar to a 'core dump' in a conventional program.
  It allows one to run the debugger (debuda.py) offline.

  The exported file is a zip file containing all relevant yaml files, server cache and the
  command history.

  **Cache limitation**: Only the data actually read from the chip will be saved in the cache.
  As a result of this, one needs to run a set of commands while connected to the chip to
  populate the cache.

Examples:
  export
  export my-export.zip
"""

import tt_util as util

command_metadata = {"short": "xp", "type": "housekeeping", "description": __doc__}

import tt_device
from docopt import docopt


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])

    zip_file_name = args["<file-name>"] if args["<file-name>"] else "debuda-export.zip"

    # 1. Add all Yaml files
    filelist = [f for f in util.YamlFile.file_cache]
    util.VERBOSE(f"Export filelist:")
    util.VERBOSE(f"{util.pretty (filelist)}")

    # 2. See if server cache is made
    if tt_device.DEBUDA_SERVER_CACHED_IFC.enabled and tt_device.DEBUDA_SERVER_CACHED_IFC.cache_mode == "through":
        tt_device.DEBUDA_SERVER_CACHED_IFC.save()
        filelist.append(tt_device.DEBUDA_SERVER_CACHED_IFC.filepath)
    else:
        util.WARN(
            "Warning: there is no server cache to export ('--server-cache' must be set to 'through')"
        )

    # 3. Save command history
    COMMAND_HISTORY_FILENAME = "debuda-command-history.yaml"
    util.write_to_yaml_file(
        context.prompt_session.history.get_strings(), COMMAND_HISTORY_FILENAME
    )
    filelist.append(COMMAND_HISTORY_FILENAME)

    # Append the brisc file
    for filename in context.elf.filemap.values():
        filelist.append(filename)

    odir = context.args["<output_dir>"]
    de_odir = f"dbd/export-{odir}"
    zip_file_name = util.export_to_zip(
        filelist, out_file=zip_file_name, prefix_to_remove=odir
    )
    print(
        f"Exported '{zip_file_name}'. Import with:\n{util.CLR_GREEN}mkdir -p {de_odir} && unzip {zip_file_name} -d {de_odir} && dbd/debuda.py {de_odir} {'--server-cache on' if tt_device.DEBUDA_SERVER_CACHED_IFC.enabled else ''}{util.CLR_END}"
    )

    return None
