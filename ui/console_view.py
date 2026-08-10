"""
ui/console_view.py
Interactive ADB console: command entry, Execute button, terminal-style
output area (stdout/stderr), Clear and Copy buttons.

Only runs against the currently selected device and always builds the
command as an argument list (no shell=True), splitting the user's input
with shlex so quoting works but no shell metacharacters are interpreted.
"""

from __future__ import annotations

import shlex
import threading
import tkinter as tk
from datetime import datetime

from utils.logger import get_logger

logger = get_logger(__name__)


class ConsoleView(tk.Frame):
    def __init__(self, parent: tk.Widget, app) -> None:
        from ui.main_window import COLORS, FONT_FAMILY

        self.COLORS = COLORS
        self.FONT = FONT_FAMILY
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        self._build()

    def _build(self) -> None:
        C = self.COLORS
        header = tk.Frame(self, bg=C["bg"])
        header.pack(fill="x", padx=24, pady=(20, 10))
        tk.Label(header, text="ADB Console", bg=C["bg"], fg=C["text"], font=(self.FONT, 16, "bold")).pack(
            side="left"
        )

        self.target_var = tk.StringVar(value="No device selected")
        tk.Label(header, textvariable=self.target_var, bg=C["bg"], fg=C["text_dim"], font=(self.FONT, 10)).pack(
            side="right"
        )

        # Output area
        output_frame = tk.Frame(self, bg=C["bg"])
        output_frame.pack(fill="both", expand=True, padx=24, pady=(0, 10))

        self.output = tk.Text(
            output_frame,
            bg="#111318",
            fg="#c9f0c9",
            insertbackground="#c9f0c9",
            font=("Consolas", 10),
            wrap="word",
            bd=0,
        )
        self.output.pack(side="left", fill="both", expand=True)
        scrollbar = tk.Scrollbar(output_frame, command=self.output.yview)
        scrollbar.pack(side="left", fill="y")
        self.output.configure(yscrollcommand=scrollbar.set, state="disabled")

        # Input row
        input_row = tk.Frame(self, bg=C["bg"])
        input_row.pack(fill="x", padx=24, pady=(0, 10))

        tk.Label(input_row, text="adb", bg=C["bg"], fg=C["text_dim"], font=("Consolas", 11)).pack(side="left")

        self.command_entry = tk.Entry(
            input_row, bg=C["card"], fg=C["text"], insertbackground=C["text"], font=("Consolas", 11), bd=0
        )
        self.command_entry.pack(side="left", fill="x", expand=True, ipady=6, padx=8)
        self.command_entry.bind("<Return>", lambda e: self._on_execute())

        tk.Button(
            input_row, text="Execute", command=self._on_execute, bg=C["accent"], fg="white", bd=0, padx=14, pady=6,
            cursor="hand2",
        ).pack(side="left", padx=(0, 6))

        # Bottom bar
        bottom = tk.Frame(self, bg=C["bg"])
        bottom.pack(fill="x", padx=24, pady=(0, 20))
        tk.Button(
            bottom, text="Clear", command=self._on_clear, bg=C["bg_secondary"], fg=C["text"], bd=0, padx=12, pady=6,
            cursor="hand2",
        ).pack(side="left", padx=(0, 6))
        tk.Button(
            bottom, text="Copy", command=self._on_copy, bg=C["bg_secondary"], fg=C["text"], bd=0, padx=12, pady=6,
            cursor="hand2",
        ).pack(side="left")

    # ------------------------------------------------------------------ #
    def on_show(self) -> None:
        device = self.app.device_manager.get_selected_device()
        self.target_var.set(f"Target: {device.serial}" if device else "No device selected")

    def on_devices_updated(self, devices) -> None:
        self.on_show()

    # ------------------------------------------------------------------ #
    def _append_output(self, text: str, tag: str = "normal") -> None:
        self.output.configure(state="normal")
        self.output.insert("end", text)
        self.output.see("end")
        self.output.configure(state="disabled")

    def _on_execute(self) -> None:
        raw_command = self.command_entry.get().strip()
        if not raw_command:
            return
        device = self.app.device_manager.get_selected_device()
        if not device:
            self._append_output("[error] Chưa chọn device nào.\n")
            return

        try:
            adb_args = shlex.split(raw_command)
        except ValueError as exc:
            self._append_output(f"[error] Cú pháp lệnh không hợp lệ: {exc}\n")
            return

        if not adb_args:
            return

        timestamp = datetime.now().strftime("%H:%M:%S")
        self._append_output(f"\n[{timestamp}] adb -s {device.serial} {' '.join(adb_args)}\n")
        self.command_entry.delete(0, "end")

        def _worker() -> None:
            result = self.app.adb_manager.run_console_command(device.serial, adb_args)
            self.after(0, lambda: self._on_result(result))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_result(self, result) -> None:
        if result.stdout:
            self._append_output(result.stdout if result.stdout.endswith("\n") else result.stdout + "\n")
        if result.stderr:
            self._append_output(result.stderr if result.stderr.endswith("\n") else result.stderr + "\n")
        if result.error:
            self._append_output(f"[error] {result.error}\n")
        if not result.stdout and not result.stderr and not result.error:
            self._append_output("(no output)\n")

    def _on_clear(self) -> None:
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def _on_copy(self) -> None:
        content = self.output.get("1.0", "end")
        self.clipboard_clear()
        self.clipboard_append(content)
