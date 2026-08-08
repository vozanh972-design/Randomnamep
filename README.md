# Lunex ReDown — Downloader & Editor

C++17/20 + Qt 6 Widgets desktop app (Windows `.exe`). Frameless custom
chrome, license-activation screen (pixel-matched to the reference image),
and a main downloader shell.

## Build automatically (no Windows machine needed) — recommended

This repo includes `.github/workflows/build-windows.yml`, which builds
`LunexReDown.exe` on a real Windows runner every time you push, using GitHub's
free CI minutes. One-time setup:

1. Create a new GitHub repo and push this folder to it (or use the GitHub
   web UI: "Add file" → "Upload files" → drag this whole folder in → Commit).
2. GitHub Actions starts automatically. Watch it under the repo's **Actions**
   tab (takes ~5-8 minutes: installs Qt 6, configures CMake, builds, runs
   `windeployqt`).
3. When it finishes, open the workflow run → **Artifacts** → download
   `LunexReDown-windows-x64.zip`. That zip contains `LunexReDown.exe` plus every Qt
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

The binary is produced at `build/bin/LunexReDown.exe`. Package it for
distribution with `windeployqt`:

```bash
windeployqt build/bin/Release/LunexReDown.exe
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

## Changes in this pass

- **Renamed** the app everywhere (window text, sidebar, settings key,
  CMake target/exe name, CI workflow) from `VideoX` to `LunexReDown`
  (displayed as **"Lunex ReDown"**).
- **Fixed the title-bar button position bug.** `ActivationWindow` never
  positioned `TitleBar` at all (it defaulted to the top-*left* corner of
  its parent), and `MainWindow` only positioned it once, at construction
  time, so any resize/maximize left the ‒ ▢ ✕ cluster in a stale spot.
  Both windows now pin the title bar to the true top-right corner in a
  `resizeEvent` override, so it tracks every resize/maximize/restore.
- **Fixed "maximize" not actually filling the screen.** The window is a
  frameless card with a 24px outer margin + drop shadow for the windowed
  look. That's fine restored, but it also applied while maximized, so the
  window looked like it stopped short of the screen edges. `resizeEvent`
  now drops the margin and shadow when `isMaximized()` and restores them
  otherwise.
- **Wired the two "hidden" links.** `AppConfig::purchaseUrl()` →
  `https://lunex.io.vn`, `AppConfig::supportUrl()` →
  `https://zalo.me/0931006827`. Both open via `QDesktopServices::openUrl`
  when the corresponding button is clicked — the URL is never shown as
  visible text in the UI, only the button labels ("Mua key bản quyền",
  "Hỗ trợ") are.
- **Key format / duration suffix already worked** —
  `LicenseService::validateKey()`'s regex is
  `^LUNEX-[A-Za-z0-9]{6}-[A-Za-z0-9]{6}-[A-Za-z0-9]{6}-(P|B)(\d+)D$`,
  which already accepts `P1D`, `P7D`, `P30D`, etc. (any digit count), and
  matches the example key you gave
  (`LUNEX-gc2nkD-vwFq5h-yzkHaQ-P30D`) exactly. Placeholder text in the key
  field updated to match this real format instead of the old `VX-...`
  mock text.
- Updated the key-input placeholder text to the real format.

### The one thing I could **not** fix for you: the "cannot connect to server" error

That error means the app successfully reached the network layer and got
*no valid response* from `licenseApiUrl()` — before this pass it was
still pointing at the placeholder `your-domain.example.com`, which
doesn't exist. I've pointed it at
`https://lunex.io.vn/api/verify_key.php` as a best guess based on the
purchase-page domain you gave me, but **I have no way to confirm that's
the real path to your `verify_key.php` on your actual server** — only
you know your hosting setup. Open `src/services/AppConfig.cpp` and make
sure `licenseApiUrl()` is the exact, publicly-reachable HTTPS URL where
`server/verify_key.php` is deployed (same file as in `server/`), then
rebuild. Two things worth doing before that goes live either way:
- Rotate the plaintext DB credentials in `server/verify_key.php` — put
  them in an environment variable / `.env` outside version control.
- Make sure the endpoint is served over **HTTPS** — Qt's
  `QNetworkAccessManager` will refuse/warn on a plain-HTTP endpoint with
  a self-signed or missing cert, which produces exactly this same error
  message.

### About "encrypt important files so users only get the .exe"

There's no meaningful way to "encrypt" a compiled C++ binary's own logic
from the person running it — the machine has to be able to execute it,
so anything shipped is inherently reversible with enough effort (same as
any commercial desktop app). What *is* real and already set up for you:
the GitHub Actions build (`.github/workflows/build-windows.yml`) only
ever hands your users the **`dist/`** folder — `LunexReDown.exe` + the Qt
runtime DLLs `windeployqt` copies in. It strips `.pdb`/`.lib` files
before upload. None of `src/`, `CMakeLists.txt`, or `server/` (your PHP +
DB credentials) is ever in that artifact. That folder is your safe
"give this .exe to a user" deliverable — never share the repo itself or
the `server/` folder with end users.

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
the "1000+ website" claim in the UI). It looks for the binary at, in order:

1. `<exe-dir>/yt-dlp.exe` — directly next to `LunexReDown.exe` (simplest,
   and what most people do — just drop it in the same folder as the exe)
2. `<exe-dir>/tools/yt-dlp.exe` — a `tools` subfolder next to the exe
3. Otherwise, on `PATH`

If none of those are found, `DownloadService::engineNotFoundMessage()` is
surfaced instead of silently failing. `yt-dlp.exe` and `ffmpeg.exe` (needed
for `--merge-output-format mp4`) are not bundled here — download them from
their official sources and drop them next to the built exe (either
directly, or in a `tools/` subfolder — both work).

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
