"""
ui/devices_view.py
Device Manager: table of devices (serial / status / model / android /
actions), connect/disconnect/refresh/restart-adb controls, per-device
action buttons (shell, reboot, install/uninstall, pull/push, list
packages, screencap), an APK manager panel and a screenshot panel.
"""

from __future__ import annotations

import os
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk
from typing import List, Optional

from PIL import Image, ImageTk  # noqa: F401  (imported lazily where needed)

from utils.helpers import (
    format_bytes,
    is_valid_package_name,
    sanitize_device_serial_for_filename,
    timestamp_for_filename,
)
from utils.logger import get_logger

logger = get_logger(__name__)


class DevicesView(tk.Frame):
    def __init__(self, parent: tk.Widget, app) -> None:
        from ui.main_window import COLORS, FONT_FAMILY

        self.COLORS = COLORS
        self.FONT = FONT_FAMILY
        super().__init__(parent, bg=COLORS["bg"])
        self.app = app
        self._devices: List = []
        self._preview_image = None  # keep reference to avoid GC

        self._build()

    # ------------------------------------------------------------------ #
    # Layout
    # ------------------------------------------------------------------ #
    def _build(self) -> None:
        C = self.COLORS
        header = tk.Frame(self, bg=C["bg"])
        header.pack(fill="x", padx=24, pady=(20, 10))
        tk.Label(header, text="Devices", bg=C["bg"], fg=C["text"], font=(self.FONT, 16, "bold")).pack(
            side="left"
        )

        btn_bar = tk.Frame(header, bg=C["bg"])
        btn_bar.pack(side="right")
        self._make_btn(btn_bar, "Refresh", self._on_refresh).pack(side="left", padx=4)
        self._make_btn(btn_bar, "Connect...", self._on_connect).pack(side="left", padx=4)
        self._make_btn(btn_bar, "Disconnect", self._on_disconnect).pack(side="left", padx=4)
        self._make_btn(btn_bar, "Restart ADB", self._on_restart_adb, danger=True).pack(side="left", padx=4)

        # Table
        table_frame = tk.Frame(self, bg=C["bg"])
        table_frame.pack(fill="both", expand=False, padx=24, pady=(0, 10))

        columns = ("serial", "status", "model", "android")
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings", height=8, selectmode="browse")
        for col, label, width in [
            ("serial", "Device", 220),
            ("status", "Status", 120),
            ("model", "Model", 180),
            ("android", "Android", 100),
        ]:
            self.tree.heading(col, text=label)
            self.tree.column(col, width=width, anchor="w")
        self.tree.pack(side="left", fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", self._on_row_selected)

        scrollbar = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="left", fill="y")

        # Actions row (operates on the selected device)
        actions = tk.Frame(self, bg=C["bg"])
        actions.pack(fill="x", padx=24, pady=(0, 10))
        for text, cmd, danger in [
            ("Shell", self._on_shell, False),
            ("Reboot", self._on_reboot, True),
            ("List Packages", self._on_list_packages, False),
            ("Push File", self._on_push, False),
            ("Pull File", self._on_pull, False),
        ]:
            self._make_btn(actions, text, cmd, danger=danger).pack(side="left", padx=4)

        # Lower split: APK manager | Screenshot
        lower = tk.Frame(self, bg=C["bg"])
        lower.pack(fill="both", expand=True, padx=24, pady=(0, 20))
        lower.grid_columnconfigure(0, weight=1, uniform="half")
        lower.grid_columnconfigure(1, weight=1, uniform="half")
        lower.grid_rowconfigure(0, weight=1)

        self._build_apk_panel(lower)
        self._build_screenshot_panel(lower)

    def _make_btn(self, parent, text, command, danger: bool = False) -> tk.Button:
        C = self.COLORS
        return tk.Button(
            parent,
            text=text,
            command=command,
            bg=C["danger"] if danger else C["accent"],
            fg="white",
            bd=0,
            padx=12,
            pady=6,
            cursor="hand2",
            activebackground=C["accent_hover"],
        )

    def _build_apk_panel(self, parent: tk.Widget) -> None:
        C = self.COLORS
        panel = tk.Frame(parent, bg=C["card"], highlightbackground=C["border"], highlightthickness=1)
        panel.grid(row=0, column=0, sticky="nsew", padx=(0, 8))

        tk.Label(panel, text="APK Manager", bg=C["card"], fg=C["text"], font=(self.FONT, 12, "bold")).pack(
            anchor="w", padx=16, pady=(14, 8)
        )

        self.apk_path_var = tk.StringVar(value="No APK selected")
        tk.Label(
            panel, textvariable=self.apk_path_var, bg=C["card"], fg=C["text_dim"],
            font=(self.FONT, 9), wraplength=340, justify="left"
        ).pack(anchor="w", padx=16)

        self.apk_size_var = tk.StringVar(value="")
        tk.Label(panel, textvariable=self.apk_size_var, bg=C["card"], fg=C["text_dim"], font=(self.FONT, 9)).pack(
            anchor="w", padx=16, pady=(0, 8)
        )

        btn_row = tk.Frame(panel, bg=C["card"])
        btn_row.pack(anchor="w", padx=16, pady=(0, 8))
        self._make_btn(btn_row, "Choose APK", self._on_choose_apk).pack(side="left", padx=(0, 6))
        self._make_btn(btn_row, "Install", self._on_install_apk).pack(side="left", padx=6)
        self._make_btn(btn_row, "Reinstall", self._on_reinstall_apk).pack(side="left", padx=6)

        uninstall_row = tk.Frame(panel, bg=C["card"])
        uninstall_row.pack(anchor="w", padx=16, pady=(0, 8), fill="x")
        self.package_entry = tk.Entry(uninstall_row, bg=C["bg_secondary"], fg=C["text"], insertbackground=C["text"], bd=0)
        self.package_entry.insert(0, "com.example.package")
        self.package_entry.pack(side="left", fill="x", expand=True, ipady=4, padx=(0, 6))
        self._make_btn(uninstall_row, "Uninstall", self._on_uninstall_package, danger=True).pack(side="left")

        self.apk_result_var = tk.StringVar(value="")
        tk.Label(
            panel, textvariable=self.apk_result_var, bg=C["card"], fg=C["text_dim"],
            font=(self.FONT, 9), wraplength=340, justify="left"
        ).pack(anchor="w", padx=16, pady=(0, 14))

        self._selected_apk_path: Optional[str] = None

    def _build_screenshot_panel(self, parent: tk.Widget) -> None:
        C = self.COLORS
        panel = tk.Frame(parent, bg=C["card"], highlightbackground=C["border"], highlightthickness=1)
        panel.grid(row=0, column=1, sticky="nsew", padx=(8, 0))

        tk.Label(panel, text="Screenshot", bg=C["card"], fg=C["text"], font=(self.FONT, 12, "bold")).pack(
            anchor="w", padx=16, pady=(14, 8)
        )

        self._make_btn(panel, "📸 Screenshot", self._on_screenshot).pack(anchor="w", padx=16, pady=(0, 8))

        self.screenshot_preview_label = tk.Label(panel, bg=C["bg_secondary"], text="No screenshot yet", fg=C["text_dim"])
        self.screenshot_preview_label.pack(padx=16, pady=(0, 8), fill="both", expand=True)

        self.screenshot_status_var = tk.StringVar(value="")
        tk.Label(
            panel, textvariable=self.screenshot_status_var, bg=C["card"], fg=C["text_dim"],
            font=(self.FONT, 9), wraplength=340, justify="left"
        ).pack(anchor="w", padx=16, pady=(0, 14))

        self._last_screenshot_path: Optional[str] = None

    # ------------------------------------------------------------------ #
    # Device list refresh callbacks
    # ------------------------------------------------------------------ #
    def on_show(self) -> None:
        self.on_devices_updated(self.app.device_manager.get_devices())

    def on_devices_updated(self, devices: List) -> None:
        self._devices = devices
        selected_serial = self.app.device_manager.state.selected_serial

        self.tree.delete(*self.tree.get_children())
        for d in devices:
            self.tree.insert(
                "", "end", iid=d.serial,
                values=(d.serial, d.display_status, d.model or "-", d.android_version or "-"),
            )
        if selected_serial and self.tree.exists(selected_serial):
            self.tree.selection_set(selected_serial)

    def _on_row_selected(self, event=None) -> None:
        selection = self.tree.selection()
        if not selection:
            return
        serial = selection[0]
        self.app.device_manager.select_device(serial)
        # Trigger extended info load for freshly selected device.
        self.app.device_manager.load_device_info_async(serial, lambda d: None)

    def _require_selected_device(self) -> Optional[str]:
        device = self.app.device_manager.get_selected_device()
        if not device:
            messagebox.showwarning("No device selected", "Vui lòng chọn một device trước.")
            return None
        return device.serial

    # ------------------------------------------------------------------ #
    # Connection controls
    # ------------------------------------------------------------------ #
    def _on_refresh(self) -> None:
        self.app.refresh_devices()

    def _on_connect(self) -> None:
        address = simpledialog.askstring(
            "Connect Device", "Nhập địa chỉ device (VD: 127.0.0.1:5555):", parent=self
        )
        if not address:
            return

        def _worker() -> None:
            result = self.app.adb_manager.connect(address)
            self.after(0, lambda: self._on_connect_done(address, result))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_connect_done(self, address: str, result) -> None:
        if result.success:
            messagebox.showinfo("Connect", f"Đã kết nối tới {address}")
        else:
            messagebox.showerror("Connect Failed", result.stderr or result.error or "Không thể kết nối.")
        self.app.refresh_devices()

    def _on_disconnect(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        if not messagebox.askyesno("Disconnect", f"Ngắt kết nối {serial}?"):
            return

        def _worker() -> None:
            result = self.app.adb_manager.disconnect(serial)
            self.after(0, lambda: self._on_disconnect_done(result))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_disconnect_done(self, result) -> None:
        if not result.success:
            messagebox.showerror("Disconnect Failed", result.stderr or result.error or "Lỗi không xác định.")
        self.app.refresh_devices()

    def _on_restart_adb(self) -> None:
        if not messagebox.askyesno("Restart ADB", "Khởi động lại ADB server?"):
            return

        def _worker() -> None:
            self.app.adb_manager.restart_server()
            self.after(0, self.app.refresh_devices)

        threading.Thread(target=_worker, daemon=True).start()

    # ------------------------------------------------------------------ #
    # Device actions
    # ------------------------------------------------------------------ #
    def _on_shell(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        command = simpledialog.askstring("Shell Command", "Nhập lệnh shell (không gồm 'adb shell'):", parent=self)
        if not command:
            return

        def _worker() -> None:
            result = self.app.adb_manager.shell(serial, command.split())
            output = result.stdout or result.stderr or result.error or "(no output)"
            self.after(0, lambda: messagebox.showinfo("Shell Result", output[:2000]))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_reboot(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        if not messagebox.askyesno("Reboot Device", f"Khởi động lại {serial}? Thao tác này có thể mất vài phút."):
            return
        threading.Thread(target=lambda: self.app.adb_manager.reboot(serial), daemon=True).start()
        messagebox.showinfo("Reboot", "Đã gửi lệnh reboot.")

    def _on_list_packages(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return

        def _worker() -> None:
            result = self.app.adb_manager.list_packages(serial)
            packages = result.stdout.strip() if result.success else (result.stderr or "Lỗi khi lấy danh sách package")
            self.after(0, lambda: self._show_text_dialog("Installed Packages", packages))

        threading.Thread(target=_worker, daemon=True).start()

    def _show_text_dialog(self, title: str, content: str) -> None:
        C = self.COLORS
        win = tk.Toplevel(self)
        win.title(title)
        win.geometry("500x500")
        win.configure(bg=C["bg"])
        text = tk.Text(win, bg=C["bg_secondary"], fg=C["text"], wrap="word", bd=0)
        text.pack(fill="both", expand=True, padx=10, pady=10)
        text.insert("1.0", content)
        text.configure(state="disabled")

    def _on_push(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        local_path = filedialog.askopenfilename(title="Chọn file để push")
        if not local_path:
            return
        remote_path = simpledialog.askstring(
            "Push File", "Đường dẫn đích trên device:", initialvalue="/sdcard/", parent=self
        )
        if not remote_path:
            return

        def _worker() -> None:
            result = self.app.adb_manager.push(serial, local_path, remote_path)
            self.after(0, lambda: self._notify_result("Push File", result))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_pull(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        remote_path = simpledialog.askstring("Pull File", "Đường dẫn file trên device:", parent=self)
        if not remote_path:
            return
        local_dir = filedialog.askdirectory(title="Chọn thư mục lưu file")
        if not local_dir:
            return
        local_path = os.path.join(local_dir, os.path.basename(remote_path))

        def _worker() -> None:
            result = self.app.adb_manager.pull(serial, remote_path, local_path)
            self.after(0, lambda: self._notify_result("Pull File", result))

        threading.Thread(target=_worker, daemon=True).start()

    def _notify_result(self, title: str, result) -> None:
        if result.success:
            messagebox.showinfo(title, "Thành công.")
        else:
            messagebox.showerror(title, result.stderr or result.error or "Thao tác thất bại.")

    # ------------------------------------------------------------------ #
    # APK Manager
    # ------------------------------------------------------------------ #
    def _on_choose_apk(self) -> None:
        path = filedialog.askopenfilename(title="Chọn file APK", filetypes=[("APK files", "*.apk")])
        if not path:
            return
        self._selected_apk_path = path
        try:
            size = os.path.getsize(path)
        except OSError:
            size = 0
        self.apk_path_var.set(f"{os.path.basename(path)}\n{path}")
        self.apk_size_var.set(format_bytes(size))
        self.apk_result_var.set("")

    def _on_install_apk(self) -> None:
        self._do_install(reinstall=False)

    def _on_reinstall_apk(self) -> None:
        self._do_install(reinstall=True)

    def _do_install(self, reinstall: bool) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        if not self._selected_apk_path:
            messagebox.showwarning("APK Manager", "Vui lòng chọn file APK trước.")
            return
        if not messagebox.askyesno(
            "Confirm Install",
            f"{'Cài đặt lại' if reinstall else 'Cài đặt'} APK sau lên {serial}?\n\n{self._selected_apk_path}",
        ):
            return

        self.apk_result_var.set("Đang cài đặt...")

        def _worker() -> None:
            result = self.app.adb_manager.install_apk(serial, self._selected_apk_path, reinstall=reinstall)
            msg = "Cài đặt thành công." if result.success else (result.stderr or result.error or "Cài đặt thất bại.")
            self.after(0, lambda: self.apk_result_var.set(msg))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_uninstall_package(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return
        package = self.package_entry.get().strip()
        if not is_valid_package_name(package):
            messagebox.showwarning("APK Manager", "Tên package không hợp lệ.")
            return
        if not messagebox.askyesno("Confirm Uninstall", f"Gỡ cài đặt '{package}' khỏi {serial}?\nHành động này không thể hoàn tác."):
            return

        self.apk_result_var.set("Đang gỡ cài đặt...")

        def _worker() -> None:
            result = self.app.adb_manager.uninstall_package(serial, package)
            msg = "Gỡ cài đặt thành công." if result.success else (result.stderr or result.error or "Gỡ cài đặt thất bại.")
            self.after(0, lambda: self.apk_result_var.set(msg))

        threading.Thread(target=_worker, daemon=True).start()

    # ------------------------------------------------------------------ #
    # Screenshot
    # ------------------------------------------------------------------ #
    def _on_screenshot(self) -> None:
        serial = self._require_selected_device()
        if not serial:
            return

        self.screenshot_status_var.set("Đang chụp màn hình...")

        def _worker() -> None:
            filename = f"{sanitize_device_serial_for_filename(serial)}_{timestamp_for_filename()}.png"
            save_dir = Path.home() / "Pictures" / "LDPlayerADBTool"
            save_dir.mkdir(parents=True, exist_ok=True)
            local_path = str(save_dir / filename)
            result = self.app.adb_manager.capture_screenshot(serial, local_path)
            self.after(0, lambda: self._on_screenshot_done(result, local_path))

        threading.Thread(target=_worker, daemon=True).start()

    def _on_screenshot_done(self, result, local_path: str) -> None:
        if not result.success:
            self.screenshot_status_var.set(result.stderr or result.error or "Chụp màn hình thất bại.")
            return
        self._last_screenshot_path = local_path
        self.screenshot_status_var.set(f"Đã lưu: {local_path}")
        try:
            img = Image.open(local_path)
            img.thumbnail((320, 320))
            self._preview_image = ImageTk.PhotoImage(img)
            self.screenshot_preview_label.configure(image=self._preview_image, text="")
        except Exception as exc:  # noqa: BLE001
            logger.error("Failed to load screenshot preview: %s", exc)
            self.screenshot_preview_label.configure(text="(Không thể hiển thị preview)", image="")
