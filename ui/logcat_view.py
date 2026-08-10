"""
ui/logcat_view.py
Logcat viewer: Start/Stop/Clear, search/filter, auto-scroll toggle, save to
.txt. The `adb logcat` process is a long-running managed subprocess (see
CommandRunner.start_managed_process); its stdout is drained on a background
thread and pushed into the Tk Text widget via `after()` so the GUI never
blocks.
"""

from __future__ import annotations

import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox

from utils.logger import get_logger

logger = get_logger(__name__)

_POLL_INTERVAL_MS = 150
_MAX_LINES = 5000


class LogcatView(tk.Frame):
    def __init__(self, parent: tk.Widget, app) -> None:
        from ui.main_window import COLORS, FONT_FAMILY

        self.COLORS = COLORS
        self.FONT = FONT_FAMILY
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        self._line_queue: "queue.Queue[str]" = queue.Queue()
        self._reader_thread: threading.Thread | None = None
        self._current_serial: str | None = None
        self._running = False
        self._all_lines: list[str] = []
        self._poll_job = None

        self._build()

    def _build(self) -> None:
        C = self.COLORS
        header = tk.Frame(self, bg=C["bg"])
        header.pack(fill="x", padx=24, pady=(20, 10))
        tk.Label(header, text="Logcat", bg=C["bg"], fg=C["text"], font=(self.FONT, 16, "bold")).pack(side="left")

        self.target_var = tk.StringVar(value="No device selected")
        tk.Label(header, textvariable=self.target_var, bg=C["bg"], fg=C["text_dim"], font=(self.FONT, 10)).pack(
            side="right"
        )

        toolbar = tk.Frame(self, bg=C["bg"])
        toolbar.pack(fill="x", padx=24, pady=(0, 10))

        self.start_btn = tk.Button(
            toolbar, text="▶ Start", command=self._on_start, bg=C["success"], fg="white", bd=0, padx=12, pady=6,
            cursor="hand2",
        )
        self.start_btn.pack(side="left", padx=(0, 6))

        self.stop_btn = tk.Button(
            toolbar, text="■ Stop", command=self._on_stop, bg=C["danger"], fg="white", bd=0, padx=12, pady=6,
            cursor="hand2", state="disabled",
        )
        self.stop_btn.pack(side="left", padx=6)

        tk.Button(
            toolbar, text="Clear", command=self._on_clear, bg=C["bg_secondary"], fg=C["text"], bd=0, padx=12,
            pady=6, cursor="hand2",
        ).pack(side="left", padx=6)

        tk.Button(
            toolbar, text="Save .txt", command=self._on_save, bg=C["bg_secondary"], fg=C["text"], bd=0, padx=12,
            pady=6, cursor="hand2",
        ).pack(side="left", padx=6)

        self.autoscroll_var = tk.BooleanVar(value=True)
        tk.Checkbutton(
            toolbar, text="Auto-scroll", variable=self.autoscroll_var, bg=C["bg"], fg=C["text_dim"],
            selectcolor=C["bg_secondary"], activebackground=C["bg"],
        ).pack(side="left", padx=12)

        tk.Label(toolbar, text="Filter:", bg=C["bg"], fg=C["text_dim"]).pack(side="left", padx=(12, 4))
        self.filter_var = tk.StringVar()
        filter_entry = tk.Entry(toolbar, textvariable=self.filter_var, bg=C["card"], fg=C["text"], bd=0, width=24)
        filter_entry.pack(side="left", ipady=4)
        self.filter_var.trace_add("write", lambda *_: self._render_filtered())

        # Output
        output_frame = tk.Frame(self, bg=C["bg"])
        output_frame.pack(fill="both", expand=True, padx=24, pady=(0, 20))

        self.output = tk.Text(
            output_frame, bg="#111318", fg="#d0d0d0", insertbackground="#d0d0d0", font=("Consolas", 9),
            wrap="none", bd=0,
        )
        self.output.pack(side="left", fill="both", expand=True)
        yscroll = tk.Scrollbar(output_frame, command=self.output.yview)
        yscroll.pack(side="left", fill="y")
        self.output.configure(yscrollcommand=yscroll.set, state="disabled")

    # ------------------------------------------------------------------ #
    def on_show(self) -> None:
        device = self.app.device_manager.get_selected_device()
        self.target_var.set(f"Target: {device.serial}" if device else "No device selected")

    def on_devices_updated(self, devices) -> None:
        self.on_show()

    # ------------------------------------------------------------------ #
    def _on_start(self) -> None:
        device = self.app.device_manager.get_selected_device()
        if not device:
            messagebox.showwarning("Logcat", "Vui lòng chọn một device trước.")
            return
        if self._running:
            messagebox.showinfo("Logcat", "Logcat đang chạy.")
            return

        self._current_serial = device.serial
        self._running = True
        self.start_btn.configure(state="disabled")
        self.stop_btn.configure(state="normal")

        try:
            proc = self.app.adb_manager.start_logcat(device.serial)
        except Exception as exc:  # noqa: BLE001
            logger.error("Failed to start logcat: %s", exc)
            messagebox.showerror("Logcat", f"Không thể khởi động logcat: {exc}")
            self._running = False
            self.start_btn.configure(state="normal")
            self.stop_btn.configure(state="disabled")
            return

        self._reader_thread = threading.Thread(target=self._read_loop, args=(proc,), daemon=True)
        self._reader_thread.start()
        self._schedule_poll()

    def _read_loop(self, proc) -> None:
        """Runs on a background thread; never touches Tk widgets directly."""
        try:
            for line in iter(proc.stdout.readline, ""):
                if not self._running:
                    break
                if line:
                    self._line_queue.put(line)
        except (OSError, ValueError) as exc:
            logger.warning("Logcat reader stopped: %s", exc)

    def _schedule_poll(self) -> None:
        self._drain_queue()
        if self._running:
            self._poll_job = self.after(_POLL_INTERVAL_MS, self._schedule_poll)

    def _drain_queue(self) -> None:
        new_lines = []
        try:
            while True:
                new_lines.append(self._line_queue.get_nowait())
        except queue.Empty:
            pass
        if not new_lines:
            return
        self._all_lines.extend(new_lines)
        if len(self._all_lines) > _MAX_LINES:
            self._all_lines = self._all_lines[-_MAX_LINES:]
        self._append_lines(new_lines)

    def _append_lines(self, lines: list[str]) -> None:
        filter_text = self.filter_var.get().strip().lower()
        visible = [l for l in lines if not filter_text or filter_text in l.lower()]
        if not visible:
            return
        self.output.configure(state="normal")
        for line in visible:
            self.output.insert("end", line)
        if self.autoscroll_var.get():
            self.output.see("end")
        self.output.configure(state="disabled")

    def _render_filtered(self) -> None:
        """Re-render the full buffer whenever the filter text changes."""
        filter_text = self.filter_var.get().strip().lower()
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        lines = self._all_lines if not filter_text else [l for l in self._all_lines if filter_text in l.lower()]
        self.output.insert("end", "".join(lines))
        if self.autoscroll_var.get():
            self.output.see("end")
        self.output.configure(state="disabled")

    def _on_stop(self) -> None:
        self._running = False
        if self._current_serial:
            self.app.adb_manager.stop_logcat(self._current_serial)
        if self._poll_job:
            self.after_cancel(self._poll_job)
            self._poll_job = None
        self.start_btn.configure(state="normal")
        self.stop_btn.configure(state="disabled")

    def _on_clear(self) -> None:
        self._all_lines = []
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def _on_save(self) -> None:
        if not self._all_lines:
            messagebox.showinfo("Logcat", "Không có log để lưu.")
            return
        path = filedialog.asksaveasfilename(defaultextension=".txt", filetypes=[("Text files", "*.txt")])
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.writelines(self._all_lines)
            messagebox.showinfo("Logcat", f"Đã lưu log vào {path}")
        except OSError as exc:
            messagebox.showerror("Logcat", f"Không thể lưu file: {exc}")

    def shutdown(self) -> None:
        if self._running:
            self._on_stop()
