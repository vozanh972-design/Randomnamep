"""
utils/config.py
Persistent application configuration (stored as JSON next to the executable).
"""

from __future__ import annotations

import json
import sys
import threading
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Dict

from utils.logger import get_logger

logger = get_logger(__name__)

_CONFIG_FILE_NAME = "config.json"


@dataclass
class AppConfig:
    adb_path: str = ""
    last_selected_device: str = ""
    auto_refresh_seconds: int = 5
    theme: str = "dark"
    window_width: int = 1200
    window_height: int = 760
    screenshot_dir: str = ""
    logcat_dir: str = ""
    confirm_dangerous_actions: bool = True
    extra: Dict[str, Any] = field(default_factory=dict)


class ConfigManager:
    """Thread-safe load/save wrapper around a JSON config file."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._path = self._resolve_path()
        self.config = self._load()

    @staticmethod
    def _resolve_path() -> Path:
        if getattr(sys, "frozen", False):
            base_dir = Path(sys.executable).resolve().parent
        else:
            base_dir = Path(__file__).resolve().parent.parent
        return base_dir / _CONFIG_FILE_NAME

    def _load(self) -> AppConfig:
        if not self._path.exists():
            logger.info("No config file found, creating defaults at %s", self._path)
            cfg = AppConfig()
            self._write(cfg)
            return cfg
        try:
            with self._path.open("r", encoding="utf-8") as f:
                data = json.load(f)
            known_fields = {k: v for k, v in data.items() if k in AppConfig.__annotations__}
            return AppConfig(**known_fields)
        except (json.JSONDecodeError, OSError, TypeError) as exc:
            logger.warning("Failed to load config (%s); using defaults", exc)
            return AppConfig()

    def _write(self, cfg: AppConfig) -> None:
        try:
            with self._path.open("w", encoding="utf-8") as f:
                json.dump(asdict(cfg), f, indent=2, ensure_ascii=False)
        except OSError as exc:
            logger.error("Failed to write config file: %s", exc)

    def save(self) -> None:
        with self._lock:
            self._write(self.config)

    def update(self, **kwargs: Any) -> None:
        with self._lock:
            for key, value in kwargs.items():
                if hasattr(self.config, key):
                    setattr(self.config, key, value)
                else:
                    self.config.extra[key] = value
            self._write(self.config)


# Module-level singleton used across the app.
config_manager = ConfigManager()
