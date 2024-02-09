# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import re
import yaml
from typing import Dict, List, Union

LOC_VAR_PATTERN = r"\$TEMPLATE_.*_dram_loc|\$TEMPLATE_loc_var.*"


def fix_dram_buffers(buffers: List) -> str:
    return "[" + ", ".join([f"[{channel}, {hex(addr)}]" for (channel, addr) in buffers]) + "]"


def fix_tms(tms: List) -> str:
    def _format_tm(tm: Union[Dict, str]) -> str:
        # transpose
        if isinstance(tm, str):
            return tm

        for tm_name, tm_factor in tm.items():
            if not isinstance(tm_factor, dict):
                return f"{tm_name}: {tm_factor}"

            for tm_factor_name, tm_factor_value in tm_factor.items():
                if tm_name == "broadcast":
                    return f"broadcast: {{{tm_factor_name}: {tm_factor_value}}}"
                else:
                    return f"{tm_name}: {{{tm_factor_name}}}"

    return "[" + ", ".join([_format_tm(tm) for tm in tms]) + "]"


def extract_model_vars_from_netlist(netlist_yaml: str, template_yaml: str) -> Dict:
    def _dfs(netlist, template, config):
        # tms and dram locations
        if isinstance(template, str):
            if not template.startswith("$TEMPLATE_"):
                return

            template_key = template.lstrip("$TEMPLATE_")
            netlist_key = netlist
            if "dram_buffers" in template_key:
                netlist_key = fix_dram_buffers(netlist)
            elif "_tms" in template_key:
                netlist_key = fix_tms(netlist)

            config[template_key] = netlist_key
            return

        elif isinstance(template, list):
            for idx, key in enumerate(template):
                _dfs(netlist[idx], key, config)

        elif isinstance(template, dict):
            for key in template:
                if re.search(LOC_VAR_PATTERN, key):
                    _dfs(netlist[netlist["loc"]], template[key], config)
                else:
                    _dfs(netlist[key], template[key], config)

    with open(netlist_yaml, "r") as netlist_file:
        netlist_dict = yaml.safe_load(netlist_file.read())

    with open(template_yaml, "r") as template_file:
        template_dict = yaml.safe_load(template_file.read())

    config = {}
    _dfs(netlist_dict, template_dict, config)
    return config


def dump_model_vars(config: Dict, output_dir: str) -> None:
    with open(f"{output_dir}/test_config.yaml", "w") as f:
        yaml.dump(config, f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--netlist-yaml", required=True, type=str)
    parser.add_argument("--template-yaml", required=True, type=str)
    parser.add_argument("--output-dir", default=None, type=str)
    args = parser.parse_args()

    config = extract_model_vars_from_netlist(args.netlist_yaml, args.template_yaml)
    dump_model_vars(config, args.output_dir)


if __name__ == "__main__":
    main()
