#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from docopt import docopt
import re


def is_command_separator_line(line):
    pattern = r"^-+\s+-+\s+-+$"
    return re.match(pattern, line)


def parse_file(file_path):
    commands = {}
    with open(file_path, "r") as f:
        lines = f.readlines()

    command = None
    section = None
    skip_line = False
    in_triple_backticks = False
    for i, line in enumerate(lines):
        if skip_line:
            skip_line = False
            continue

        try:
            if line.startswith("```"):  # We want keep the triple backticks lines as is
                in_triple_backticks = not in_triple_backticks

            if (
                is_command_separator_line(line) and not in_triple_backticks
            ):  # Next line describes a command (long form, short form)
                # get next line and read the command
                line = lines[i + 1]
                line_as_array = line.strip().split()
                if len(line_as_array) == 2:  # Make sure the line is two words
                    command, short_form = line_as_array[0], line_as_array[1]
                    commands[command] = {"short_form": short_form}
                    section = None
                    skip_line = True
                    continue

            if "Executing command: x" in line:
                print("Last command reached")
                break
            else:  # Just copy the line
                if line.strip() in [
                    "Usage:",
                    "Description:",
                    "Examples:",
                    "Arguments:",
                    "Options:",
                ]:
                    section = line.strip()[:-1].lower()
                    commands[command][section] = []
                else:
                    if line.strip():
                        if not in_triple_backticks:
                            line = line.strip()
                        else:  # Strip only the \n
                            line = line[:-1]
                        commands[command][section].append(line)
        except Exception as e:
            print(f"Error parsing line: {line}")
            raise e
    return commands


def create_markdown(data, output_path):
    with open(output_path, "w") as f:
        f.write("# Commands\n")
        for cmd, details in data.items():
            f.write(f"#### {cmd} / {details['short_form']}\n")
            for section, content in details.items():
                if section == "short_form":
                    continue

                # Handle 'arguments' and 'options' differently
                if section in ["arguments", "options"]:
                    f.write(f"##### {section.capitalize()}:\n")
                    f.write(f"```\n")
                    for line in content:
                        f.write(f"{line}\n")
                    f.write(f"```\n")

                # Handle other sections
                else:
                    f.write(f"##### {section.capitalize()}:\n")
                    in_triple_backticks = False
                    for line in content:
                        if "```" in line:
                            in_triple_backticks = not in_triple_backticks
                        elif section == "usage" and not in_triple_backticks:
                            line = f"`{line}`"
                        elif section == "examples" and not in_triple_backticks:
                            if "#" in line:
                                # Split to before first # and after first #
                                line, comment = line.split("#", 1)
                                line = f"`{line}` # {comment}"
                            else:
                                line = f"`{line}`"
                        f.write(f"{line}\n")
                f.write("\n")

            # Add page break after each command
            f.write("\n<div style='page-break-after: always;'></div>\n")


if __name__ == "__main__":
    args = docopt(
        """Text to Markdown
    Usage:
        convert-help-to-markdown.py <input_file> <output_file>
    """
    )

    input_file = args["<input_file>"]
    output_file = args["<output_file>"]

    parsed_data = parse_file(input_file)
    # print (parsed_data)
    create_markdown(parsed_data, output_file)
