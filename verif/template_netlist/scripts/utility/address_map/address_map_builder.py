import argparse
import os
import pathlib
import re


class AddressMapBuilder:
    file_name_suffix = "address_map.py"
    class_name_prefix = "@dataclass(frozen=True)\nclass"
    class_name_suffix = "AddressMap(AddressMap):"
    indentation = 4 * " "

    def __get_var_name_and_val(self, line):
        if "static_assert" in line:
            return (False, None, None)

        pattern = r"\b([A-Z0-9_]+)\s*=\s*(.*?);"
        match = re.findall(pattern, line)

        if match:
            assert len(match) == 1, "Found multiple matches which is not expected"
            var_name = match[0][0]
            var_value = match[0][1]
            return (True, var_name, var_value)

        return (False, None, None)

    def __get_file_and_class_name(self, arch: str) -> str:
        if arch == "grayskull":
            return (f"gs_{self.file_name_suffix}", f"{self.class_name_prefix} GS{self.class_name_suffix}")
        elif arch == "wormhole":
            return (f"wh_{self.file_name_suffix}", f"{self.class_name_prefix} WH{self.class_name_suffix}")
        elif arch == "blackhole":
            return (f"bh_{self.file_name_suffix}", f"{self.class_name_prefix} BH{self.class_name_suffix}")
        else:
            raise RuntimeError("Unknown arch")

    def __get_includes(self) -> str:
        includes = ["from dataclasses import dataclass", "from address_map import AddressMap"]
        return "\n".join(includes)

    def build_from_file(self, l1_address_map_file_path: str) -> None:
        memory_regions_decls = {}

        with open(l1_address_map_file_path, "r") as f:
            for line in f:
                match_found, attr_name, attr_val = self.__get_var_name_and_val(line)

                if match_found:
                    memory_regions_decls[attr_name] = attr_val

        arch = l1_address_map_file_path.split("/")[-2]
        filename, class_name = self.__get_file_and_class_name(arch)
        current_file_path = pathlib.Path(__file__).parent.resolve()

        with open(os.path.join(current_file_path, filename), "w") as f:
            f.write(self.__get_includes())
            f.write("\n\n")
            f.write(class_name)
            f.write("\n")

            for var_name, var_val in memory_regions_decls.items():
                f.write(f"{self.indentation}{var_name} = {var_val}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", type=str, required=True)
    args = parser.parse_args()

    builder = AddressMapBuilder()
    builder.build_from_file(args.file)
