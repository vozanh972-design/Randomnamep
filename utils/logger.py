"""
utils/logger.py
Centralized logging configuration for the LDPlayer ADB Tool.

All modules should obtain their logger via `get_logger(__name__)` instead of
calling `logging.getLogger` directly, so that log formatting / handlers stay
consistent across the whole application.
"""

from __future__ import annotations

import logging
import logging.handlers
import os
import sys
from pathlib import Path

_LOG_DIR_NAME = "logs"
_LOG_FILE_NAME = "ldplayer_adb_tool.log"
_INITIALIZED = False


def _get_log_dir() -> Path:
    """Return (and create if needed) the directory used to store log files."""
    # Store logs next to the executable / script so the tool is portable.
    if getattr(sys, "frozen", False):
        base_dir = Path(sys.executable).resolve().parent
    else:
        base_dir = Path(__file__).resolve().parent.parent
    log_dir = base_dir / _LOG_DIR_NAME
    try:
        log_dir.mkdir(parents=True, exist_ok=True)
    except OSError:
        # Fall back to a temp directory if we cannot write next to the exe.
        log_dir = Path(os.environ.get("TEMP", ".")) / "LDPlayerADBTool" / _LOG_DIR_NAME
        log_dir.mkdir(parents=True, exist_ok=True)
    return log_dir


def _init_root_logger() -> None:
    global _INITIALIZED
    if _INITIALIZED:
        return

    log_dir = _get_log_dir()
    log_file = log_dir / _LOG_FILE_NAME

    root = logging.getLogger("ldplayer_adb_tool")
    root.setLevel(logging.DEBUG)

    formatter = logging.Formatter(
        fmt="%(asctime)s | %(levelname)-8s | %(name)-28s | %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    file_handler = logging.handlers.RotatingFileHandler(
        log_file, maxBytes=2 * 1024 * 1024, backupCount=5, encoding="utf-8"
    )
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(formatter)

    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(formatter)

    root.addHandler(file_handler)
    root.addHandler(console_handler)

    _INITIALIZED = True


def get_logger(name: str) -> logging.Logger:
    """Return a namespaced logger under the application's root logger."""
    _init_root_logger()
    return logging.getLogger(f"ldplayer_adb_tool.{name}")
