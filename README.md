# VideoX Downloader & Editor

C++17/20 + Qt 6 Widgets desktop app (Windows `.exe`). Frameless custom
chrome, license-activation screen (pixel-matched to the reference image),
and a main downloader shell.

## Build automatically (no Windows machine needed) — recommended

This repo includes `.github/workflows/build-windows.yml`, which builds
`VideoX.exe` on a real Windows runner every time you push, using GitHub's
free CI minutes. One-time setup:

1. Create a new GitHub repo and push this folder to it (or use the GitHub
   web UI: "Add file" → "Upload files" → drag this whole folder in → Commit).
2. GitHub Actions starts automatically. Watch it under the repo's **Actions**
   tab (takes ~5-8 minutes: installs Qt 6, configures CMake, builds, runs
   `windeployqt`).
3. When it finishes, open the workflow run → **Artifacts** → download
   `VideoX-windows-x64.zip`. That zip contains `VideoX.exe` plus every Qt
   DLL it needs, ready to run on any Windows 10/11 machine.

No Qt/MSVC install on your own machine required — GitHub's Windows runner
does all of it. Re-push any time you change the code and a fresh `.exe`
builds automatically.

## Build manually (if you prefer your own machine)

Requirements: Qt 6.5+ (Widgets, Network, Svg, SvgWidgets), CMake 3.21+, MSVC
(Visual Studio 2019/2022 Desktop C++ workload).

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
cmake --build build --config Release
```

The binary is produced at `build/bin/VideoX.exe`. Package it for
distribution with `windeployqt`:

```bash
windeployqt build/bin/Release/VideoX.exe
```

You can also open the folder directly in **Qt Creator** (it reads
`CMakeLists.txt` natively) or in **Visual Studio** via
"Open a local folder" (CMake integration).

## Project layout

```
src/
├── main.cpp                     entry point, license gate
├── ui/
│   ├── TitleBar.{h,cpp}          shared frameless title bar
│   ├── ActivationWindow.{h,cpp}  license key screen (first screen)
│   └── MainWindow.{h,cpp}        downloader shell shown post-activation
├── services/
│   ├── AppConfig.{h,cpp}         single source of truth for URLs/config
│   ├── LicenseService.{h,cpp}    validate/activate/persist license state
│   └── DownloadService.{h,cpp}   shells out to a downloader engine (yt-dlp)
├── models/
│   └── LicenseInfo.h
resources/
├── resources.qrc
├── styles/styles.qss             all widget styling (no inline stylesheets)
├── icons/*.svg                   Lucide-style stroke icons
└── images/*.svg                  logo + illustration
server/
└── verify_key.php                the activation endpoint you provided (reference copy)
```

## License activation flow

1. `main.cpp` asks `LicenseService::isActivated()`. If a still-valid license
   is stored locally, `MainWindow` opens immediately — no re-entry of the key.
2. Otherwise `ActivationWindow` is shown. Submitting a key calls
   `LicenseService::activateKey()`, which:
   - runs a client-side format check for instant feedback,
   - `GET`s `AppConfig::licenseApiUrl()` with `key` + a hashed per-machine
     `device_id`,
   - on `"status":"success"`, persists the license (lightly obfuscated, not
     plaintext) via `QSettings` and emits `activationSucceeded`.
3. `ActivationWindow` reacts to the three states from the prompt: loading
   (button text + disabled), success (inline message, then hands off to
   `MainWindow`), error (inline red message, no `QMessageBox`).

**Config you need to fill in** before shipping: open
`src/services/AppConfig.cpp` and set `licenseApiUrl()`,
`purchaseUrl()`, `supportUrl()`, `websiteUrl()` to your real domain.

## About `server/verify_key.php`

This is the file you pasted, included here only as a reference copy next to
the client that calls it — the C++ app never re-implements this logic
client-side.

Two things worth double-checking before this goes live, independent of the
Qt app:
- The script embeds real-looking DB credentials in plaintext
  (`lunex418_vn` / `lunex418_vn`). Since you shared this in a chat, it's
  worth rotating those credentials and moving them to an environment
  variable or a `.env`/config file that's excluded from version control.
- The license-key format the server validates
  (`LUNEX-xxxxxx-xxxxxx-xxxxxx-P30D` / `...B30D`) differs from the
  placeholder text shown in the reference screenshot
  (`VX-XXXXX-XXXXX-XXXXX`). The UI keeps the screenshot's exact placeholder
  text (per your "don't change the reference text" instruction), while
  `LicenseService::validateKey()` matches the server's real `LUNEX-...`
  pattern. Align these two before shipping, or the placeholder will mislead
  users about the expected format.

## Video downloading engine

`DownloadService` shells out to `yt-dlp.exe` via `QProcess` (industry
standard, MIT-licensed, actively maintained, supports 1000+ sites — matches
the "1000+ website" claim in the UI). It looks for the binary at:

1. `<exe-dir>/tools/yt-dlp.exe` (recommended — ship it alongside `VideoX.exe`)
2. Otherwise, on `PATH`

If neither is found, `DownloadService::engineNotFoundMessage()` is surfaced
instead of silently failing. `yt-dlp.exe` and `ffmpeg.exe` (needed for
`--merge-output-format mp4`) are not bundled here — download them from
their official sources and drop them in `tools/` next to the built exe.

## What's implemented vs. stubbed

- **Fully implemented**: frameless custom window/title bar, activation
  screen layout/spacing/colors/copy matching the reference image, all four
  activation states, license persistence + auto-skip on relaunch,
  `LicenseService`/`DownloadService`/`AppConfig` architecture, QSS-driven
  styling, resource-based (no absolute-path) assets, DPI-aware Qt 6 scaling.
- **Functional but intentionally simple**: `DownloadService` progress
  parsing (regex on yt-dlp's stdout) — solid for MP4/single-file downloads,
  but a production build will want a proper job queue instead of one
  `QProcess` per download, plus pause/resume beyond "kill and let yt-dlp
  resume the partial file."
- **Static/demo data**: the "Đang tải" / "Đã hoàn thành" lists in
  `MainWindow` render the sample entries from the reference screenshot.
  Wiring `DownloadService::progressChanged/jobCompleted` into those job
  cards (instead of the demo data) is the next step to make it live.
- **Not built here**: the actual video editor ("Trình chỉnh sửa"), "Lịch
  sử", "Công cụ", and "Cài đặt" screens are present as nav destinations
  only — the prompt's focus was the activation screen + main shell.
