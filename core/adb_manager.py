"""
core/adb_manager.py
Wraps the `adb` executable: discovery, server control, device commands,
install/uninstall, push/pull, screenshot capture and logcat streaming.

This module never uses shell=True; every command is issued as an explicit
argument list through `CommandRunner`.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from enum import Enum
from pathlib import Path
from typing import Callable, List, Optional

from core.command_runner import CommandResult, CommandRunner
from utils.config import config_manager
from utils.logger import get_logger

logger = get_logger(__name__)

_LOGCAT_KEY_PREFIX = "logcat:"


class AdbError(Exception):
    """Raised for ADB-related failures that the UI should surface nicely."""


class DeviceStatus(str, Enum):
    DEVICE = "device"
    OFFLINE = "offline"
    UNAUTHORIZED = "unauthorized"
    NO_PERMISSIONS = "no permissions"
    UNKNOWN = "unknown"


class AdbManager:
    """High level API around the adb command-line tool."""

    def __init__(self) -> None:
        self.runner = CommandRunner()
        self.adb_path: str = self._resolve_initial_adb_path()

    # ------------------------------------------------------------------ #
    # Discovery
    # ------------------------------------------------------------------ #
    def _resolve_initial_adb_path(self) -> str:
        configured = config_manager.config.adb_path
        if configured and Path(configured).is_file():
            return configured
        found = self.find_adb_executable()
        if found:
            config_manager.update(adb_path=found)
            return found
        return configured or ""

    @staticmethod
    def find_adb_executable() -> Optional[str]:
        """
        Try to locate adb.exe automatically:
        1. Already on PATH.
        2. Common LDPlayer installation folders.
        3. Common Android SDK platform-tools folders.
        """
        exe_name = "adb.exe" if sys.platform.startswith("win") else "adb"

        # 1. PATH
        on_path = shutil.which(exe_name)
        if on_path:
            logger.info("Found adb on PATH: %s", on_path)
            return on_path

        # 2 & 3. Common install locations
        candidate_dirs: List[str] = []
        env_vars = ["ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "LOCALAPPDATA"]
        for var in env_vars:
            base = os.environ.get(var)
            if not base:
                continue
            candidate_dirs.extend(
                [
                    os.path.join(base, "LDPlayer", "LDPlayer9"),
                    os.path.join(base, "LDPlayer", "LDPlayer4.0"),
                    os.path.join(base, "LDPlayer9"),
                    os.path.join(base, "Netease", "MuMuPlayer"),
                    os.path.join(base, "Android", "android-sdk", "platform-tools"),
                    os.path.join(base, "Android", "Sdk", "platform-tools"),
                ]
            )
        user_profile = os.environ.get("USERPROFILE", "")
        if user_profile:
            candidate_dirs.append(
                os.path.join(user_profile, "AppData", "Local", "Android", "Sdk", "platform-tools")
            )

        for directory in candidate_dirs:
            candidate = os.path.join(directory, exe_name)
            if os.path.isfile(candidate):
                logger.info("Found adb in candidate directory: %s", candidate)
                return candidate

        logger.warning("Could not auto-detect adb executable")
        return None

    def set_adb_path(self, path: str) -> bool:
        """Manually set the adb.exe path; returns True if the path is valid."""
        if path and Path(path).is_file():
            self.adb_path = path
            config_manager.update(adb_path=path)
            logger.info("ADB path set to %s", path)
            return True
        logger.warning("Attempted to set invalid adb path: %s", path)
        return False

    def is_adb_available(self) -> bool:
        return bool(self.adb_path) and Path(self.adb_path).is_file()

    # ------------------------------------------------------------------ #
    # Server control
    # ------------------------------------------------------------------ #
    def _base_args(self) -> List[str]:
        if not self.is_adb_available():
            raise AdbError(
                "Không tìm thấy adb.exe. Hãy vào Settings để chọn đường dẫn ADB thủ công."
            )
        return [self.adb_path]

    def start_server(self) -> CommandResult:
        return self.runner.run(self._base_args() + ["start-server"], timeout=15)

    def kill_server(self) -> CommandResult:
        return self.runner.run(self._base_args() + ["kill-server"], timeout=15)

    def restart_server(self) -> CommandResult:
        logger.info("Restarting ADB server")
        self.kill_server()
        return self.start_server()

    # ------------------------------------------------------------------ #
    # Device discovery
    # ------------------------------------------------------------------ #
    def list_devices_raw(self) -> CommandResult:
        return self.runner.run(self._base_args() + ["devices", "-l"], timeout=15)

    def parse_devices(self, raw_stdout: str) -> List[dict]:
        """
        Parse the output of `adb devices -l` into a list of
        {"serial": str, "status": DeviceStatus, "extra": str} dicts.
        """
        devices = []
        lines = raw_stdout.strip().splitlines()
        for line in lines[1:]:  # skip "List of devices attached"
            line = line.strip()
            if not line:
                continue
            parts = line.split(maxsplit=1)
            if len(parts) < 2:
                continue
            serial, remainder = parts[0], parts[1]
            status_token = remainder.split()[0] if remainder.split() else "unknown"
            try:
                status = DeviceStatus(status_token)
            except ValueError:
                status = DeviceStatus.UNKNOWN
            devices.append({"serial": serial, "status": status, "extra": remainder})
        return devices

    def connect(self, address: str) -> CommandResult:
        """Connect to an emulator over TCP, e.g. 127.0.0.1:5555."""
        logger.info("Connecting to %s", address)
        return self.runner.run(self._base_args() + ["connect", address], timeout=15)

    def disconnect(self, address: Optional[str] = None) -> CommandResult:
        args = self._base_args() + ["disconnect"]
        if address:
            args.append(address)
        logger.info("Disconnecting %s", address or "all devices")
        return self.runner.run(args, timeout=15)

    # ------------------------------------------------------------------ #
    # Device info
    # ------------------------------------------------------------------ #
    def shell(self, serial: str, shell_args: List[str], timeout: float = 15.0) -> CommandResult:
        return self.runner.run(self._base_args() + ["-s", serial, "shell"] + shell_args, timeout=timeout)

    def get_prop(self, serial: str, prop: str) -> str:
        result = self.shell(serial, ["getprop", prop])
        return result.stdout.strip() if result.success else ""

    def get_device_info(self, serial: str) -> dict:
        """Collect model, android version, resolution, battery, storage."""
        info = {
            "model": self.get_prop(serial, "ro.product.model") or "Unknown",
            "android_version": self.get_prop(serial, "ro.build.version.release") or "Unknown",
            "resolution": "Unknown",
            "battery": "Unknown",
            "storage": "Unknown",
        }

        wm_result = self.shell(serial, ["wm", "size"])
        if wm_result.success:
            match = re.search(r"(\d+x\d+)", wm_result.stdout)
            if match:
                info["resolution"] = match.group(1)

        battery_result = self.shell(serial, ["dumpsys", "battery"])
        if battery_result.success:
            match = re.search(r"level:\s*(\d+)", battery_result.stdout)
            if match:
                info["battery"] = f"{match.group(1)}%"

        storage_result = self.shell(serial, ["df", "/data"])
        if storage_result.success:
            lines = [l for l in storage_result.stdout.strip().splitlines() if l.strip()]
            if len(lines) >= 2:
                cols = lines[1].split()
                if len(cols) >= 4:
                    try:
                        used_kb, total_kb = int(cols[2]), int(cols[1])
                        info["storage"] = f"{used_kb / 1024:.0f}MB / {total_kb / 1024:.0f}MB"
                    except (ValueError, IndexError):
                        pass
        return info

    def reboot(self, serial: str) -> CommandResult:
        logger.info("Rebooting device %s", serial)
        return self.runner.run(self._base_args() + ["-s", serial, "reboot"], timeout=20)

    # ------------------------------------------------------------------ #
    # Package management
    # ------------------------------------------------------------------ #
    def install_apk(self, serial: str, apk_path: str, reinstall: bool = False) -> CommandResult:
        args = self._base_args() + ["-s", serial, "install"]
        if reinstall:
            args.append("-r")
        args.append(apk_path)
        logger.info("Installing APK on %s: %s", serial, apk_path)
        return self.runner.run(args, timeout=120)

    def uninstall_package(self, serial: str, package: str) -> CommandResult:
        logger.info("Uninstalling package %s from %s", package, serial)
        return self.runner.run(self._base_args() + ["-s", serial, "uninstall", package], timeout=60)

    def list_packages(self, serial: str) -> CommandResult:
        return self.shell(serial, ["pm", "list", "packages"], timeout=20)

    # ------------------------------------------------------------------ #
    # File transfer
    # ------------------------------------------------------------------ #
    def push(self, serial: str, local_path: str, remote_path: str) -> CommandResult:
        return self.runner.run(
            self._base_args() + ["-s", serial, "push", local_path, remote_path], timeout=120
        )

    def pull(self, serial: str, remote_path: str, local_path: str) -> CommandResult:
        return self.runner.run(
            self._base_args() + ["-s", serial, "pull", remote_path, local_path], timeout=120
        )

    # ------------------------------------------------------------------ #
    # Screenshot
    # ------------------------------------------------------------------ #
    def capture_screenshot(self, serial: str, local_save_path: str) -> CommandResult:
        remote_tmp = "/sdcard/_ldadbtool_screenshot.png"
        cap_result = self.shell(serial, ["screencap", "-p", remote_tmp], timeout=20)
        if not cap_result.success:
            return cap_result
        pull_result = self.pull(serial, remote_tmp, local_save_path)
        self.shell(serial, ["rm", "-f", remote_tmp], timeout=10)
        return pull_result

    # ------------------------------------------------------------------ #
    # Logcat (long running managed process)
    # ------------------------------------------------------------------ #
    def start_logcat(self, serial: str) -> subprocess.Popen:
        args = self._base_args() + ["-s", serial, "logcat"]
        return self.runner.start_managed_process(_LOGCAT_KEY_PREFIX + serial, args)

    def stop_logcat(self, serial: str) -> None:
        self.runner.stop_managed_process(_LOGCAT_KEY_PREFIX + serial)

    def is_logcat_running(self, serial: str) -> bool:
        return self.runner.is_process_running(_LOGCAT_KEY_PREFIX + serial)

    # ------------------------------------------------------------------ #
    # Generic console command (used by the ADB Console view)
    # ------------------------------------------------------------------ #
    def run_console_command(self, serial: Optional[str], adb_args: List[str]) -> CommandResult:
        """
        Run an arbitrary but validated adb subcommand against `serial`
        (e.g. adb_args=["shell", "pm", "list", "packages"]).
        """
        args = self._base_args()
        if serial:
            args += ["-s", serial]
        args += adb_args
        return self.runner.run(args, timeout=30)

    def shutdown(self) -> None:
        """Called on application exit to make sure nothing is left running."""
        self.runner.stop_all()
