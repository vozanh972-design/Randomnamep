"""
ui/main_window.py
Root Tk window: dark theme, sidebar navigation, header bar, and a content
area that swaps between the different views (Dashboard, Devices, Console,
Logcat, Settings, About).
"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Dict, Optional, Type

from core.adb_manager import AdbManager
from core.device_manager import DeviceManager
from ui.console_view import ConsoleView
from ui.dashboard import DashboardView
from ui.devices_view import DevicesView
from ui.logcat_view import LogcatView
from ui.settings_view import SettingsView
from utils.config import config_manager
from utils.logger import get_logger

logger = get_logger(__name__)

# ---------------------------------------------------------------------- #
# Theme palette
# ---------------------------------------------------------------------- #
COLORS = {
    "bg": "#1e2128",
    "bg_secondary": "#262a33",
    "sidebar": "#1a1d24",
    "card": "#2a2f3a",
    "border": "#363c48",
    "text": "#e8e9ec",
    "text_dim": "#9aa0ac",
    "accent": "#4f8cff",
    "accent_hover": "#6ea1ff",
    "success": "#3ecf8e",
    "warning": "#f5a623",
    "danger": "#f0553f",
}

FONT_FAMILY = "Segoe UI"


class MainWindow(tk.Tk):
    NAV_ITEMS = [
        ("dashboard", "🏠  Dashboard"),
        ("devices", "📱  Devices"),
        ("console", "⌨️  ADB Console"),
        ("logcat", "📜  Logcat"),
        ("settings", "⚙️  Settings"),
        ("about", "ℹ️  About"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.title("LDPlayer ADB Tool")
        cfg = config_manager.config
        self.geometry(f"{cfg.window_width}x{cfg.window_height}")
        self.minsize(1000, 620)
        self.configure(bg=COLORS["bg"])

        self.adb_manager = AdbManager()
        self.device_manager = DeviceManager(self.adb_manager)

        self._setup_style()
        self._build_layout()

        self.views: Dict[str, tk.Frame] = {}
        self._nav_buttons: Dict[str, tk.Button] = {}
        self._current_view: Optional[str] = None

        self._build_views()
        self.show_view("dashboard")

        self.protocol("WM_DELETE_WINDOW", self._on_close)

        # Kick off an initial device refresh shortly after the window shows.
        self.after(200, self.refresh_devices)
        # Periodic auto-refresh loop.
        self._schedule_auto_refresh()

    # ------------------------------------------------------------------ #
    # Style
    # ------------------------------------------------------------------ #
    def _setup_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        style.configure(
            "Treeview",
            background=COLORS["card"],
            fieldbackground=COLORS["card"],
            foreground=COLORS["text"],
            rowheight=28,
            borderwidth=0,
            font=(FONT_FAMILY, 10),
        )
        style.map("Treeview", background=[("selected", COLORS["accent"])])
        style.configure(
            "Treeview.Heading",
            background=COLORS["bg_secondary"],
            foreground=COLORS["text_dim"],
            borderwidth=0,
            font=(FONT_FAMILY, 9, "bold"),
        )
        style.configure(
            "TProgressbar",
            background=COLORS["accent"],
            troughcolor=COLORS["bg_secondary"],
            borderwidth=0,
        )
        style.configure("TCombobox", fieldbackground=COLORS["card"], foreground=COLORS["text"])

    # ------------------------------------------------------------------ #
    # Layout
    # ------------------------------------------------------------------ #
    def _build_layout(self) -> None:
        # Header
        self.header = tk.Frame(self, bg=COLORS["bg_secondary"], height=56)
        self.header.pack(side="top", fill="x")
        self.header.pack_propagate(False)

        tk.Label(
            self.header,
            text="LDPlayer ADB Tool",
            bg=COLORS["bg_secondary"],
            fg=COLORS["text"],
            font=(FONT_FAMILY, 13, "bold"),
        ).pack(side="left", padx=20)

        self.header_status_var = tk.StringVar(value="ADB: Checking...")
        tk.Label(
            self.header,
            textvariable=self.header_status_var,
            bg=COLORS["bg_secondary"],
            fg=COLORS["text_dim"],
            font=(FONT_FAMILY, 10),
        ).pack(side="right", padx=12)

        self.header_device_count_var = tk.StringVar(value="0 devices")
        tk.Label(
            self.header,
            textvariable=self.header_device_count_var,
            bg=COLORS["bg_secondary"],
            fg=COLORS["accent"],
            font=(FONT_FAMILY, 10, "bold"),
        ).pack(side="right", padx=12)

        # Body: sidebar + content
        body = tk.Frame(self, bg=COLORS["bg"])
        body.pack(side="top", fill="both", expand=True)

        self.sidebar = tk.Frame(body, bg=COLORS["sidebar"], width=200)
        self.sidebar.pack(side="left", fill="y")
        self.sidebar.pack_propagate(False)

        self.content = tk.Frame(body, bg=COLORS["bg"])
        self.content.pack(side="left", fill="both", expand=True)

        self._build_sidebar()

    def _build_sidebar(self) -> None:
        tk.Frame(self.sidebar, bg=COLORS["sidebar"], height=12).pack(fill="x")
        self._nav_buttons = {}
        for key, label in self.NAV_ITEMS:
            btn = tk.Button(
                self.sidebar,
                text=label,
                anchor="w",
                bg=COLORS["sidebar"],
                fg=COLORS["text_dim"],
                activebackground=COLORS["bg_secondary"],
                activeforeground=COLORS["text"],
                bd=0,
                relief="flat",
                font=(FONT_FAMILY, 11),
                padx=20,
                pady=10,
                cursor="hand2",
                command=lambda k=key: self.show_view(k),
            )
            btn.pack(fill="x", padx=8, pady=2)
            self._nav_buttons[key] = btn

    def _build_views(self) -> None:
        self.views["dashboard"] = DashboardView(self.content, self)
        self.views["devices"] = DevicesView(self.content, self)
        self.views["console"] = ConsoleView(self.content, self)
        self.views["logcat"] = LogcatView(self.content, self)
        self.views["settings"] = SettingsView(self.content, self)
        self.views["about"] = _AboutView(self.content, self)

        for view in self.views.values():
            view.place(relx=0, rely=0, relwidth=1, relheight=1)

    # ------------------------------------------------------------------ #
    # Navigation
    # ------------------------------------------------------------------ #
    def show_view(self, key: str) -> None:
        if key not in self.views:
            return
        self._current_view = key
        self.views[key].tkraise()
        for k, btn in self._nav_buttons.items():
            if k == key:
                btn.configure(bg=COLORS["bg_secondary"], fg=COLORS["accent"])
            else:
                btn.configure(bg=COLORS["sidebar"], fg=COLORS["text_dim"])
        on_show = getattr(self.views[key], "on_show", None)
        if callable(on_show):
            on_show()

    # ------------------------------------------------------------------ #
    # Shared device refresh (used by header + dashboard + devices view)
    # ------------------------------------------------------------------ #
    def refresh_devices(self) -> None:
        self.header_status_var.set("ADB: Refreshing...")

        def _on_done(devices) -> None:
            self.after(0, lambda: self._apply_refresh_result(devices))

        if not self.adb_manager.is_adb_available():
            self.header_status_var.set("ADB: Not Found")
            self.header_device_count_var.set("0 devices")
            return

        self.device_manager.refresh_async(_on_done)

    def _apply_refresh_result(self, devices) -> None:
        connected = [d for d in devices if d.display_status == "Connected"]
        self.header_device_count_var.set(f"{len(connected)} / {len(devices)} devices")
        self.header_status_var.set("ADB: Ready" if self.adb_manager.is_adb_available() else "ADB: Not Found")

        for view in self.views.values():
            on_devices = getattr(view, "on_devices_updated", None)
            if callable(on_devices):
                on_devices(devices)

    def _schedule_auto_refresh(self) -> None:
        interval_ms = max(3, config_manager.config.auto_refresh_seconds) * 1000
        self.refresh_devices()
        self.after(interval_ms, self._schedule_auto_refresh)

    # ------------------------------------------------------------------ #
    # Shutdown
    # ------------------------------------------------------------------ #
    def _on_close(self) -> None:
        logger.info("Application closing — stopping managed ADB processes")
        try:
            self.adb_manager.shutdown()
        finally:
            self.destroy()


class _AboutView(tk.Frame):
    def __init__(self, parent: tk.Widget, app: "MainWindow") -> None:
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        wrapper = tk.Frame(self, bg=COLORS["bg"])
        wrapper.pack(expand=True)
        tk.Label(
            wrapper,
            text="LDPlayer ADB Tool",
            bg=COLORS["bg"],
            fg=COLORS["text"],
            font=(FONT_FAMILY, 20, "bold"),
        ).pack(pady=(0, 8))
        tk.Label(
            wrapper,
            text="Phiên bản 1.0.0 — Công cụ quản lý LDPlayer qua ADB",
            bg=COLORS["bg"],
            fg=COLORS["text_dim"],
            font=(FONT_FAMILY, 11),
        ).pack()
        tk.Label(
            wrapper,
            text="Xây dựng bằng Python + Tkinter",
            bg=COLORS["bg"],
            fg=COLORS["text_dim"],
            font=(FONT_FAMILY, 10),
        ).pack(pady=(4, 0))
