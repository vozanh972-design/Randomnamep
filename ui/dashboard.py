"""
ui/dashboard.py
Dashboard: ADB status, connected device count, selected device details
(model, android version, resolution, battery, storage) shown as cards.
"""

from __future__ import annotations

import tkinter as tk
from typing import List, Optional

from utils.logger import get_logger

logger = get_logger(__name__)


class DashboardView(tk.Frame):
    def __init__(self, parent: tk.Widget, app) -> None:
        from ui.main_window import COLORS, FONT_FAMILY  # local import avoids cycle

        self.COLORS = COLORS
        self.FONT = FONT_FAMILY
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        self._card_value_labels: dict[str, tk.StringVar] = {}

        self._build()

    # ------------------------------------------------------------------ #
    def _build(self) -> None:
        C = self.COLORS
        header = tk.Frame(self, bg=C["bg"])
        header.pack(fill="x", padx=24, pady=(20, 10))
        tk.Label(
            header, text="Dashboard", bg=C["bg"], fg=C["text"], font=(self.FONT, 16, "bold")
        ).pack(side="left")

        refresh_btn = tk.Button(
            header,
            text="⟳ Refresh",
            bg=C["accent"],
            fg="white",
            bd=0,
            padx=14,
            pady=6,
            cursor="hand2",
            command=self.app.refresh_devices,
        )
        refresh_btn.pack(side="right")

        grid = tk.Frame(self, bg=C["bg"])
        grid.pack(fill="both", expand=True, padx=24, pady=10)
        for col in range(3):
            grid.grid_columnconfigure(col, weight=1, uniform="col")

        cards = [
            ("adb_status", "ADB Status", "Checking..."),
            ("connected_devices", "Connected Devices", "0"),
            ("selected_device", "Selected Device", "None"),
            ("android_version", "Android Version", "-"),
            ("device_model", "Device Model", "-"),
            ("resolution", "Screen Resolution", "-"),
            ("battery", "Battery", "-"),
            ("storage", "Storage", "-"),
        ]

        for idx, (key, title, default) in enumerate(cards):
            row, col = divmod(idx, 3)
            self._make_card(grid, row, col, key, title, default)

    def _make_card(self, parent: tk.Widget, row: int, col: int, key: str, title: str, default: str) -> None:
        C = self.COLORS
        card = tk.Frame(parent, bg=C["card"], highlightbackground=C["border"], highlightthickness=1)
        card.grid(row=row, column=col, sticky="nsew", padx=8, pady=8)

        tk.Label(
            card, text=title, bg=C["card"], fg=C["text_dim"], font=(self.FONT, 9, "bold")
        ).pack(anchor="w", padx=16, pady=(14, 2))

        var = tk.StringVar(value=default)
        tk.Label(
            card, textvariable=var, bg=C["card"], fg=C["text"], font=(self.FONT, 15, "bold"), wraplength=240
        ).pack(anchor="w", padx=16, pady=(0, 14))

        self._card_value_labels[key] = var

    # ------------------------------------------------------------------ #
    def on_show(self) -> None:
        self._refresh_from_state()

    def on_devices_updated(self, devices: List) -> None:
        self._refresh_from_state(devices)

    def _refresh_from_state(self, devices: Optional[List] = None) -> None:
        app = self.app
        if devices is None:
            devices = app.device_manager.get_devices()

        adb_ok = app.adb_manager.is_adb_available()
        self._card_value_labels["adb_status"].set("Ready" if adb_ok else "Not Found")

        connected = [d for d in devices if d.display_status == "Connected"]
        self._card_value_labels["connected_devices"].set(str(len(connected)))

        selected = app.device_manager.get_selected_device()
        if selected:
            self._card_value_labels["selected_device"].set(selected.serial)
            self._card_value_labels["android_version"].set(selected.android_version or "-")
            self._card_value_labels["device_model"].set(selected.model or "-")
            self._card_value_labels["resolution"].set(selected.resolution or "-")
            self._card_value_labels["battery"].set(selected.battery or "-")
            self._card_value_labels["storage"].set(selected.storage or "-")

            if not selected.info_loaded and selected.display_status == "Connected":
                app.device_manager.load_device_info_async(
                    selected.serial, lambda d: self.after(0, self._refresh_from_state)
                )
        else:
            self._card_value_labels["selected_device"].set("None")
            for key in ("android_version", "device_model", "resolution", "battery", "storage"):
                self._card_value_labels[key].set("-")
