# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility methods to be used by tests.
"""

class ConsoleColors:
    OK_GREEN = "\033[92m"
    WARNING = "\033[95m"
    FAIL = "\033[91m"
    END = "\033[0m"


def print_success(message: str):
    print(f"{ConsoleColors.OK_GREEN}{message}{ConsoleColors.END}")

def print_warning(message: str):
    print(f"{ConsoleColors.WARNING}{message}{ConsoleColors.END}")


def print_fail(message: str):
    print(f"{ConsoleColors.FAIL}{message}{ConsoleColors.END}")