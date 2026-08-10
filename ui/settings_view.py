"""
ui/settings_view.py
Settings: manually choose adb.exe path, auto-refresh interval, confirmation
toggle for dangerous actions. Persisted via utils.config.config_manager.
"""

from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, messagebox

from utils.config import config_manager
from utils.logger import get_logger

logger = get_logger(__name__)


class SettingsView(tk.Frame):
    def __init__(self, parent: tk.Widget, app) -> None:
        from ui.main_window import COLORS, FONT_FAMILY

        self.COLORS = COLORS
        self.FONT = FONT_FAMILY
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        self._build()

    def _build(self) -> None:
        C = self.COLORS
        tk.Label(self, text="Settings", bg=C["bg"], fg=C["text"], font=(self.FONT, 16, "bold")).pack(
            anchor="w", padx=24, pady=(20, 16)
        )

        panel = tk.Frame(self, bg=C["card"], highlightbackground=C["border"], highlightthickness=1)
        panel.pack(fill="x", padx=24)

        # ADB path
        tk.Label(panel, text="ADB Executable Path", bg=C["card"], fg=C["text"], font=(self.FONT, 11, "bold")).pack(
            anchor="w", padx=18, pady=(18, 4)
        )
        path_row = tk.Frame(panel, bg=C["card"])
        path_row.pack(fill="x", padx=18, pady=(0, 4))

        self.adb_path_var = tk.StringVar(value=self.app.adb_manager.adb_path or "(Chưa cấu hình)")
        tk.Entry(
            path_row, textvariable=self.adb_path_var, bg=C["bg_secondary"], fg=C["text"],
            insertbackground=C["text"], bd=0
        ).pack(side="left", fill="x", expand=True, ipady=5, padx=(0, 8))

        tk.Button(
            path_row, text="Browse...", command=self._on_browse, bg=C["accent"], fg="white", bd=0, padx=12,
            pady=6, cursor="hand2",
        ).pack(side="left")

        tk.Button(
            path_row, text="Auto-detect", command=self._on_auto_detect, bg=C["bg_secondary"], fg=C["text"],
            bd=0, padx=12, pady=6, cursor="hand2",
        ).pack(side="left", padx=(6, 0))

        self.adb_status_var = tk.StringVar(
            value="✓ ADB hợp lệ" if self.app.adb_manager.is_adb_available() else "✗ Không tìm thấy ADB"
        )
        tk.Label(panel, textvariable=self.adb_status_var, bg=C["card"], fg=C["text_dim"], font=(self.FONT, 9)).pack(
            anchor="w", padx=18, pady=(0, 16)
        )

        # Auto refresh interval
        tk.Label(panel, text="Auto-refresh Interval (giây)", bg=C["card"], fg=C["text"], font=(self.FONT, 11, "bold")).pack(
            anchor="w", padx=18, pady=(4, 4)
        )
        refresh_row = tk.Frame(panel, bg=C["card"])
        refresh_row.pack(anchor="w", padx=18, pady=(0, 16))

        self.refresh_var = tk.IntVar(value=config_manager.config.auto_refresh_seconds)
        tk.Spinbox(
            refresh_row, from_=3, to=60, textvariable=self.refresh_var, width=6, bg=C["bg_secondary"],
            fg=C["text"], bd=0, insertbackground=C["text"],
        ).pack(side="left")

        # Confirm dangerous actions
        self.confirm_var = tk.BooleanVar(value=config_manager.config.confirm_dangerous_actions)
        tk.Checkbutton(
            panel, text="Yêu cầu xác nhận cho thao tác nguy hiểm (reboot, uninstall, ...)",
            variable=self.confirm_var, bg=C["card"], fg=C["text_dim"], selectcolor=C["bg_secondary"],
            activebackground=C["card"],
        ).pack(anchor="w", padx=18, pady=(0, 16))

        tk.Button(
            panel, text="Save Settings", command=self._on_save, bg=C["success"], fg="white", bd=0, padx=16,
            pady=8, cursor="hand2",
        ).pack(anchor="w", padx=18, pady=(0, 18))

    # ------------------------------------------------------------------ #
    def _on_browse(self) -> None:
        path = filedialog.askopenfilename(
            title="Chọn adb.exe", filetypes=[("adb executable", "adb.exe"), ("All files", "*.*")]
        )
        if not path:
            return
        self.adb_path_var.set(path)

    def _on_auto_detect(self) -> None:
        from core.adb_manager import AdbManager

        found = AdbManager.find_adb_executable()
        if found:
            self.adb_path_var.set(found)
            messagebox.showinfo("Auto-detect", f"Đã tìm thấy: {found}")
        else:
            messagebox.showwarning("Auto-detect", "Không tìm thấy adb.exe tự động. Vui lòng chọn thủ công.")

    def _on_save(self) -> None:
        path = self.adb_path_var.get().strip()
        if not self.app.adb_manager.set_adb_path(path):
            messagebox.showerror("Settings", "Đường dẫn ADB không hợp lệ.")
            self.adb_status_var.set("✗ Không tìm thấy ADB")
        else:
            self.adb_status_var.set("✓ ADB hợp lệ")

        config_manager.update(
            auto_refresh_seconds=max(3, self.refresh_var.get()),
            confirm_dangerous_actions=self.confirm_var.get(),
        )
        messagebox.showinfo("Settings", "Đã lưu cài đặt.")
        self.app.refresh_devices()
