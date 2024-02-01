# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import logging


def make_custom_logger(name: str, verbose: bool, log_to_file: bool) -> logging.Logger:
    """Create Logger(name) with NullHandler by default, and one StreamHandler if verbose==True, and
    one FileHandler if log_to_file==True."""
    logger = logging.getLogger(name)
    logger.setLevel(logging.DEBUG)

    if not logger.hasHandlers():
        # Add a dummy NullHandler which does nothing in order to prevent logging
        # from using lastResort handler and outputting WARNINGs to console.
        logger.addHandler(logging.NullHandler())

        if verbose:
            # Set up console logging for INFO level.
            console_handler = logging.StreamHandler()
            console_handler.setLevel(logging.INFO)
            formatter = logging.Formatter("[%(name)s][PID:%(process)d][%(levelname)-7s] %(message)s")
            console_handler.setFormatter(formatter)
            logger.addHandler(console_handler)

        if log_to_file:
            # Set up console logging for INFO level.
            file_handler = logging.FileHandler(f"{name}.log", mode='w')
            file_handler.setLevel(logging.DEBUG)
            formatter = logging.Formatter("[PID:%(process)d][%(levelname)-7s] %(message)s")
            file_handler.setFormatter(formatter)
            logger.addHandler(file_handler)

    return logger

def make_default_logger(name: str, verbose: bool) -> logging.Logger:
    """Create Logger(name) with NullHandler by default, and one StreamHandler if verbose==True."""
    logger = logging.getLogger(name)
    logger.setLevel(logging.INFO)

    if not logger.hasHandlers():
        # Add a dummy NullHandler which does nothing in order to prevent logging
        # from using lastResort handler and outputting WARNINGs to console.
        logger.addHandler(logging.NullHandler())

        if verbose:
            # Set up console logging for INFO level.
            console_handler = logging.StreamHandler()
            console_handler.setLevel(logging.INFO)
            formatter = logging.Formatter("[%(name)s] %(message)s")
            console_handler.setFormatter(formatter)
            logger.addHandler(console_handler)

    return logger

def get_logger(name: str) -> logging.Logger:
    return logging.getLogger(name)