# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import logging
from datetime import datetime
from enum import Enum

# Define ANSI escape sequences for colors
COLORS = {
    'RED': '\033[91m',
    'GREEN': '\033[92m',
    'YELLOW': '\033[93m',
    'CYAN': '\033[96m',
    'BOLD_RED': '\033[1;91m',
    'RESET': '\033[0m'
}

# Some colors cannot be used in excel
# These are colors that excel supports
class ExcelColors(Enum):
    green = "#D9EAD3"
    red = "#F4CCCC"
    yellow = "#FFF2CC"
    blue = "#B4C7E7"

# Create a custom log formatter
class ColoredFormatter(logging.Formatter):
    def format(self, record):
        log_level = record.levelname
        message = super().format(record)
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        filename = os.path.basename(record.filename)

        if log_level == 'DEBUG':
            color = COLORS['CYAN']
        elif log_level == 'INFO':
            color = COLORS['GREEN']
        elif log_level == 'WARNING':
            color = COLORS['YELLOW']
        elif log_level == 'ERROR':
            color = COLORS['RED']
        elif log_level == 'CRITICAL':
            color = COLORS['BOLD_RED']
        else:
            color = COLORS['RESET']

        max_level_width = max(len(level) for level in COLORS.keys())
        level_padding = ' ' * (max_level_width - len(log_level))
        # prefix = f'{timestamp} | {filename}'
        prefix = f'{timestamp} | {log_level}{level_padding}'
        formatted_message = f'{color}{prefix}{COLORS["RESET"]} | {message}{COLORS["RESET"]}'

        return formatted_message

# Configure the logger
logger = logging.getLogger()
LOGGER_LEVEL = logging.INFO
logger.setLevel(LOGGER_LEVEL)

# Remove any existing handlers from the logger
for handler in logger.handlers[:]:
    logger.removeHandler(handler)

# Create a handler with the custom formatter
console_handler = logging.StreamHandler()
console_handler.setFormatter(ColoredFormatter())

# Add the handler to the logger
logger.addHandler(console_handler)


def print_progress_bar(current_iter, total_iter):
    if total_iter != 0:
        completed = 100 * current_iter // total_iter
        percent_completed = round(current_iter / total_iter, 2) * 100
    else:
        completed = 100
        percent_completed = 100
    bar = f"{COLORS['GREEN']}{'#'*completed}{COLORS['RED']}{'-' *(100-completed)}{COLORS['RESET']}"
    logger.info(f"{'Progress'} |{bar}| {COLORS['GREEN']}{percent_completed:.2f}% {COLORS['GREEN']}{current_iter}/{COLORS['RED']}{total_iter}")

def reset_all_handlers():
    for handler in logger.handlers[:]:
        logger.removeHandler(handler)    

def disable_log_file():
    reset_all_handlers()
    logger.addHandler(console_handler)

def redirect_log_to_file(path):
    logger.info(f"Redirecting the analyzer log to {path}")
    reset_all_handlers()
    file_handler = logging.FileHandler(path)
    file_handler.setLevel(LOGGER_LEVEL)
    file_handler.setFormatter(ColoredFormatter())
    logger.addHandler(file_handler)

def ASSERT(condition, message=""):
    if condition:
        return
    else:
        logger.critical(message)
        reset_all_handlers()
        raise Exception("Failed")
