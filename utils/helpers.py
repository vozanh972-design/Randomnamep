"""
utils/helpers.py
Small, dependency-free helper functions shared across the application.
"""

from __future__ import annotations

import re
from datetime import datetime

_PACKAGE_NAME_RE = re.compile(r"^[a-zA-Z][a-zA-Z0-9_]*(\.[a-zA-Z][a-zA-Z0-9_]*)+$")
_SERIAL_RE = re.compile(r"^[\w.\-:]+$")


def format_bytes(size: int | float) -> str:
    """Human readable byte size, e.g. 1536 -> '1.5 KB'."""
    try:
        size = float(size)
    except (TypeError, ValueError):
        return "N/A"
    units = ["B", "KB", "MB", "GB", "TB"]
    for unit in units:
        if size < 1024.0:
            return f"{size:.1f} {unit}" if unit != "B" else f"{int(size)} {unit}"
        size /= 1024.0
    return f"{size:.1f} PB"


def timestamp_for_filename() -> str:
    """Return a filesystem-safe timestamp, e.g. 20260810_142530."""
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def sanitize_device_serial_for_filename(serial: str) -> str:
    """Turn '127.0.0.1:5555' into '127.0.0.1_5555' for safe filenames."""
    return re.sub(r"[^\w.\-]", "_", serial)


def is_valid_package_name(package: str) -> bool:
    """Loose validation of an Android application package id."""
    if not package:
        return False
    return bool(_PACKAGE_NAME_RE.match(package.strip()))


def is_valid_serial(serial: str) -> bool:
    """Validate a device serial / address before using it in a command."""
    if not serial:
        return False
    return bool(_SERIAL_RE.match(serial.strip()))


def truncate(text: str, max_len: int = 200) -> str:
    text = text or ""
    return text if len(text) <= max_len else text[: max_len - 3] + "..."
