"""
core/device_manager.py
Keeps track of the list of ADB devices and which one is currently selected.
Provides thread-safe refresh (backed by AdbManager) that the UI can call
from a background thread and then marshal results back onto the main thread.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import Callable, List, Optional

from core.adb_manager import AdbManager, AdbError, DeviceStatus
from utils.config import config_manager
from utils.logger import get_logger

logger = get_logger(__name__)


@dataclass
class Device:
    serial: str
    status: DeviceStatus = DeviceStatus.UNKNOWN
    model: str = ""
    android_version: str = ""
    resolution: str = ""
    battery: str = ""
    storage: str = ""
    info_loaded: bool = False

    @property
    def display_status(self) -> str:
        mapping = {
            DeviceStatus.DEVICE: "Connected",
            DeviceStatus.OFFLINE: "Offline",
            DeviceStatus.UNAUTHORIZED: "Unauthorized",
            DeviceStatus.NO_PERMISSIONS: "No Permissions",
            DeviceStatus.UNKNOWN: "Unknown",
        }
        return mapping.get(self.status, "Unknown")


@dataclass
class DeviceManagerState:
    devices: List[Device] = field(default_factory=list)
    selected_serial: Optional[str] = None


class DeviceManager:
    """Owns the current device list + selection; thread-safe."""

    def __init__(self, adb_manager: AdbManager) -> None:
        self.adb = adb_manager
        self._lock = threading.Lock()
        self.state = DeviceManagerState()
        self._restore_last_selection()

    def _restore_last_selection(self) -> None:
        last = config_manager.config.last_selected_device
        if last:
            self.state.selected_serial = last

    # ------------------------------------------------------------------ #
    # Refresh
    # ------------------------------------------------------------------ #
    def refresh(self) -> List[Device]:
        """
        Synchronously query `adb devices -l` and rebuild the device list.
        Safe to call from a background thread.
        """
        try:
            result = self.adb.list_devices_raw()
        except AdbError as exc:
            logger.error("Cannot refresh devices: %s", exc)
            with self._lock:
                self.state.devices = []
            return []

        if not result.success:
            logger.error("adb devices failed: %s", result.stderr or result.error)
            with self._lock:
                self.state.devices = []
            return []

        parsed = self.adb.parse_devices(result.stdout)
        with self._lock:
            existing_by_serial = {d.serial: d for d in self.state.devices}
            new_devices: List[Device] = []
            for entry in parsed:
                serial = entry["serial"]
                status = entry["status"]
                existing = existing_by_serial.get(serial)
                if existing:
                    existing.status = status
                    new_devices.append(existing)
                else:
                    new_devices.append(Device(serial=serial, status=status))
            self.state.devices = new_devices

            # Keep selection valid; auto-select the first device if none chosen.
            serials = [d.serial for d in new_devices]
            if self.state.selected_serial not in serials:
                self.state.selected_serial = serials[0] if serials else None
                if self.state.selected_serial:
                    config_manager.update(last_selected_device=self.state.selected_serial)

        return list(self.state.devices)

    def refresh_async(self, callback: Callable[[List[Device]], None]) -> threading.Thread:
        def _worker() -> None:
            devices = self.refresh()
            callback(devices)

        thread = threading.Thread(target=_worker, daemon=True)
        thread.start()
        return thread

    # ------------------------------------------------------------------ #
    # Selection
    # ------------------------------------------------------------------ #
    def select_device(self, serial: str) -> None:
        with self._lock:
            self.state.selected_serial = serial
        config_manager.update(last_selected_device=serial)
        logger.info("Selected device: %s", serial)

    def get_selected_device(self) -> Optional[Device]:
        with self._lock:
            if not self.state.selected_serial:
                return None
            for d in self.state.devices:
                if d.serial == self.state.selected_serial:
                    return d
        return None

    def get_devices(self) -> List[Device]:
        with self._lock:
            return list(self.state.devices)

    def get_device(self, serial: str) -> Optional[Device]:
        with self._lock:
            for d in self.state.devices:
                if d.serial == serial:
                    return d
        return None

    # ------------------------------------------------------------------ #
    # Extended info (model / android version / resolution / battery / storage)
    # ------------------------------------------------------------------ #
    def load_device_info(self, serial: str) -> Optional[Device]:
        """Synchronous — fetch extended `adb shell` based info for a device."""
        device = self.get_device(serial)
        if not device:
            return None
        try:
            info = self.adb.get_device_info(serial)
        except AdbError as exc:
            logger.error("Failed to load device info for %s: %s", serial, exc)
            return device
        with self._lock:
            device.model = info["model"]
            device.android_version = info["android_version"]
            device.resolution = info["resolution"]
            device.battery = info["battery"]
            device.storage = info["storage"]
            device.info_loaded = True
        return device

    def load_device_info_async(
        self, serial: str, callback: Callable[[Optional[Device]], None]
    ) -> threading.Thread:
        def _worker() -> None:
            device = self.load_device_info(serial)
            callback(device)

        thread = threading.Thread(target=_worker, daemon=True)
        thread.start()
        return thread
