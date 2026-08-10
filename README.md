# LDPlayer ADB Tool

Công cụ desktop (Python + Tkinter) để kết nối và quản lý LDPlayer (hoặc bất kỳ
Android emulator/device nào) thông qua ADB, trên Windows.

## 1. Kiến trúc

```text
ldplayer_adb_tool/
├── main.py                  # Điểm khởi chạy ứng dụng
├── requirements.txt
├── README.md
├── core/
│   ├── adb_manager.py        # Wrapper quanh adb.exe: discovery, devices, install, push/pull, screenshot, logcat
│   ├── device_manager.py     # State layer: danh sách device, device đang chọn, load thông tin device
│   └── command_runner.py     # Chạy subprocess an toàn (argument list, không shell=True), quản lý process nền
├── ui/
│   ├── main_window.py        # Cửa sổ chính: dark theme, sidebar, header, điều hướng view
│   ├── dashboard.py          # Dashboard: các card thông tin (ADB status, device, battery, storage...)
│   ├── devices_view.py       # Bảng device + Connect/Disconnect/Restart ADB + APK Manager + Screenshot
│   ├── console_view.py       # ADB Console tương tác
│   ├── logcat_view.py        # Logcat viewer (chạy nền, không block UI)
│   └── settings_view.py      # Cấu hình đường dẫn ADB, auto-refresh, xác nhận thao tác nguy hiểm
├── utils/
│   ├── config.py              # Lưu/đọc cấu hình JSON (config.json cạnh exe)
│   ├── logger.py              # Logging xoay vòng file (logs/ldplayer_adb_tool.log)
│   └── helpers.py             # Hàm tiện ích (format bytes, timestamp, validate package/serial...)
└── assets/                    # (icon, ảnh tĩnh nếu cần)
```

**Nguyên tắc thiết kế chính:**

- Mọi lệnh ADB được gọi qua danh sách argument (`["adb.exe", "-s", serial, ...]`), **không bao giờ** dùng `shell=True`, giảm rủi ro command injection.
- Các tiến trình chạy dài (logcat) được quản lý như managed background process, đọc output trên thread riêng, đẩy dữ liệu vào UI qua `after()` — Tkinter main thread không bao giờ bị block.
- Không hard-code serial: `DeviceManager` hỗ trợ danh sách nhiều device, người dùng chọn device hiện tại, mọi thao tác dùng đúng serial đã chọn.
- Cấu hình (đường dẫn ADB, device đã chọn gần nhất, interval refresh...) được lưu vào `config.json` cạnh file thực thi.
- Toàn bộ lỗi ADB được bắt và hiển thị dạng thông báo thân thiện (không traceback trực tiếp lên UI); chi tiết đầy đủ được ghi vào `logs/`.

## 2. Cài đặt & chạy (development)

Yêu cầu Python 3.9+ trên Windows.

```powershell
cd ldplayer_adb_tool
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

## 3. Cấu hình ADB

1. Mở tab **Settings**.
2. Nhấn **Auto-detect** để ứng dụng tự tìm `adb.exe` (kiểm tra PATH và các thư mục cài đặt LDPlayer/Android SDK phổ biến).
3. Nếu không tìm thấy, nhấn **Browse...** và chọn `adb.exe` thủ công (thường nằm trong thư mục cài LDPlayer, ví dụ `C:\LDPlayer\LDPlayer9\adb.exe`).
4. Nhấn **Save Settings**.

## 4. Quy trình kết nối LDPlayer

1. Mở LDPlayer, đảm bảo emulator đã khởi động xong.
2. Trong tool, vào tab **Devices** → nhấn **Refresh** để ADB liệt kê device đang chạy.
3. Nếu LDPlayer chưa xuất hiện, nhấn **Connect...** và nhập địa chỉ (thường là `127.0.0.1:5555`, LDPlayer instance thứ 2 trở đi thường dùng cổng `5556`, `5557`, ...).
4. Chọn dòng device trong bảng để chọn làm device hiện tại — mọi thao tác (shell, install, screenshot, logcat...) sẽ áp dụng cho device này.
5. Nếu ADB gặp lỗi (server treo, device kẹt trạng thái `offline`), dùng nút **Restart ADB** để khởi động lại `adb` server.

## 5. Các tính năng chính

| Tab | Chức năng |
|---|---|
| Dashboard | Trạng thái ADB, số device kết nối, thông tin device đang chọn (model, Android version, độ phân giải, pin, dung lượng) |
| Devices | Bảng device, Connect/Disconnect/Restart ADB, Shell/Reboot/List Packages/Push/Pull, APK Manager (install/reinstall/uninstall), Screenshot |
| ADB Console | Gõ lệnh adb tự do, chạy trên device đã chọn, output dạng terminal, Copy/Clear |
| Logcat | Start/Stop logcat theo device, filter theo từ khóa, auto-scroll, lưu `.txt` |
| Settings | Đường dẫn `adb.exe`, interval auto-refresh, bật/tắt xác nhận thao tác nguy hiểm |

Các thao tác **phá hủy dữ liệu** (uninstall, reboot, disconnect, restart ADB) đều yêu cầu xác nhận trước khi thực hiện.

## 6. Xử lý lỗi thường gặp

| Tình huống | Cách tool xử lý |
|---|---|
| Không tìm thấy `adb.exe` | Header hiển thị "ADB: Not Found"; các thao tác báo lỗi thân thiện, hướng dẫn vào Settings |
| ADB server chưa chạy | `start-server` được gọi tự động khi cần; có nút Restart ADB thủ công |
| Device `offline` / `unauthorized` | Hiển thị đúng trạng thái trong bảng Devices; cần xác nhận debug USB/ADB trên LDPlayer |
| LDPlayer chưa bật | `adb devices` trả về danh sách rỗng → bảng Devices trống, Dashboard hiển thị "0 devices" |
| Timeout / mất kết nối giữa chừng | `CommandRunner` bắt `TimeoutExpired`/`OSError`, trả lỗi rõ ràng thay vì crash |
| APK không hợp lệ | `adb install` trả mã lỗi, thông báo lỗi được hiển thị trong panel APK Manager |
| Permission denied | Thông báo lỗi từ stderr của adb được hiển thị trực tiếp cho người dùng |

Toàn bộ log chi tiết (bao gồm traceback nếu có) được ghi vào `logs/ldplayer_adb_tool.log` để debug.

## 7. Đóng gói thành `.exe` — build tự động trên GitHub bằng Nuitka (C backend)

Thay vì PyInstaller (chỉ đóng gói interpreter + script), project này dùng
**[Nuitka](https://nuitka.net/)**: Nuitka dịch (transpile) toàn bộ `main.py` và
các module sang mã nguồn **C**, sau đó dùng trình biên dịch C (MSVC trên
Windows runner) để build ra một file `.exe` **native thật sự**, không phải
Python bundle. File `.github/workflows/build-exe.yml` đã cấu hình sẵn để làm
việc này tự động trên GitHub Actions — bạn không cần cài gì trên máy cá nhân.

### 7.1. Đưa code lên GitHub

```powershell
cd ldplayer_adb_tool
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/<username>/<repo>.git
git push -u origin main
```

### 7.2. Build exe tự động

Ngay khi bạn `push` lên nhánh `main` (hoặc mở Pull Request, hoặc bấm chạy tay),
GitHub Actions sẽ tự động:

1. Dựng máy ảo Windows.
2. Cài Python + `requirements.txt` + Nuitka (`requirements-build.txt`).
3. Chạy Nuitka với backend MSVC, biên dịch toàn bộ app thành
   `dist/LDPlayerADBTool.exe` (chế độ `--standalone --onefile`, tắt console,
   bật plugin `tk-inter`).
4. Upload file exe thành **workflow artifact** tên `LDPlayerADBTool-windows-exe`.

Xem tiến trình build và tải file exe tại tab **Actions** trên GitHub repo của
bạn → chọn lần chạy mới nhất → mục **Artifacts** ở cuối trang.

### 7.3. Tự động tạo Release khi gắn tag

Nếu bạn muốn có 1 bản phát hành chính thức kèm link tải cố định:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

Workflow sẽ tự đính kèm `LDPlayerADBTool.exe` vào GitHub Release `v1.0.0`
tương ứng — không cần thao tác thủ công.

### 7.4. Build local (tuỳ chọn, nếu máy bạn có Visual Studio Build Tools)

```powershell
pip install -r requirements.txt
pip install -r requirements-build.txt
python -m nuitka --standalone --onefile --msvc=latest ^
    --windows-console-mode=disable --enable-plugin=tk-inter ^
    --include-data-dir=assets=assets ^
    --output-dir=dist --output-filename=LDPlayerADBTool.exe ^
    main.py
```

File thực thi sẽ nằm ở `dist\LDPlayerADBTool.exe`. Vì ứng dụng dùng
`sys.frozen` để xác định thư mục lưu `config.json` và `logs/`, file exe hoạt
động độc lập, không cần cài Python trên máy người dùng cuối.

### 7.5. Đóng gói sẵn `adb.exe` cùng ứng dụng (tuỳ chọn)

Thêm dòng sau vào lệnh Nuitka (local hoặc trong workflow):

```
--include-data-files=path/to/adb.exe=adb.exe
--include-data-files=path/to/AdbWinApi.dll=AdbWinApi.dll
--include-data-files=path/to/AdbWinUsbApi.dll=AdbWinUsbApi.dll
```

Sau đó trong Settings, người dùng trỏ đường dẫn ADB tới file đã được bundle
(nằm cạnh exe khi chạy), hoặc để tool tự động dò tìm.

## 8. Ghi chú bảo mật

- Tool không tự động cài APK hay xoá dữ liệu nếu chưa có xác nhận rõ ràng từ người dùng.
- ADB Console chỉ chạy lệnh trên device đã chọn (`-s <serial>`); không có cách nào gửi
  nhầm lệnh sang device khác trong cùng một lần chạy.
- Toàn bộ input người dùng (đường dẫn, package name, serial, lệnh shell) được tách theo
  argument list (`shlex.split` cho console, không nối chuỗi shell), giảm rủi ro injection.
