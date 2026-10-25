# TrafficGate: notes for people and agents working on this repo

TrafficGate is a single-executable Win32 tray app (C++17, MinGW-w64, no frameworks). It shows network speed in the Windows 10 taskbar, limits total bandwidth with an embedded WinDivert driver, and records daily usage. Read this before changing anything; most of the odd-looking code exists because of a specific Windows behaviour listed below.

## Layout

| Path | What it is |
|---|---|
| `src/TrafficGate.cpp` | The whole app: settings, stats, taskbar window, tray menu, dialogs, startup task, `wWinMain`. The only translation unit. |
| `src/TrafficGate.h` | `TrafficGateApp` singleton, the version defines (`APP_VERSION`, `APP_VERSION_STR`), window messages and timer IDs. |
| `src/Shaper.h` | `TrafficShaper`: the WinDivert bandwidth limiter. Header-only, included by `TrafficGate.h`. |
| `src/resource.h`, `src/TrafficGate.rc` | Resource IDs, the two dialogs, version info, manifest, and the embedded WinDivert files (RCDATA). |
| `src/TrafficGate.manifest` | Requires administrator and declares Windows 7 to 10 compatibility. |
| `src/third_party/` | WinDivert 2.2.2 x64 (`WinDivert.dll`, `WinDivert64.sys`, `windivert.h`), its licence and SHA-256 hashes. |
| `build.ps1` | The only supported build. Bumps the version and writes `dist/TrafficGate_v<version>.exe`. |
| `tools/make-icon.ps1` | Regenerates `src/TrafficGate.ico` and `docs/icon.png`. |
| `dist/` | Build output. Git-ignored, as are `*.exe`, `*.o`, `*.res`. |

`build.ps1` compiles only `src/TrafficGate.cpp`. If you add a `.cpp`, add it to the g++ line in the script, or keep new code header-only like `Shaper.h`.

## Build and check

On Windows, run `build.ps1` (see README). On any machine with a MinGW-w64 cross compiler, a quick compile check without linking is:

```
x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -DUNICODE -D_UNICODE -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 src/TrafficGate.cpp
```

There are no automated tests. After a change, run the exe on Windows 10 and check, as relevant:

1. The indicator appears left of the tray, text matches the clock, and units line up on the right.
2. Set a limit (for example 1 MB/s) and download something: the download manager and the indicator should both read about 1 MB/s. Disable the limit and confirm full speed returns.
3. The limit still works after toggling it a few times, and disappears when the app exits.
4. Usage history opens, shows the last column filling the list, and Reset clears it.
5. "Start on boot" creates and removes the `TrafficGate` scheduled task; the app starts elevated after logon.
6. Restart `explorer.exe` (Task Manager, Restart): the indicator comes back.
7. Full-screen video: the indicator must not draw over it.

## Versioning and releases

- The version lives in `src/TrafficGate.h` (`APP_VERSION`, `APP_VERSION_STR`) and `src/TrafficGate.rc` (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`, `OriginalFilename`). `build.ps1` rewrites all of them; do not edit them by hand.
- The binary is named `TrafficGate_v<major>.<minor>.exe` (two numbers; `build.ps1` bumps the minor part). `-NoVersionBump` rebuilds unchanged, `-Version X.Y` sets it.
- A release attaches the exe only. No source archive, no docs bundle; the source is the tag.
- Tag releases `v<version>` (for example `v1.10`).

## Design decisions you must not undo by accident

**One exe, nothing beside it.** The driver files are embedded as RCDATA and extracted at runtime. The exe may import only Windows system DLLs; `build.ps1` fails the build if a GCC runtime DLL shows up.

**The limiter is WinDivert, not Windows QoS.** `New-NetQosPolicy` can only throttle outgoing traffic, so it cannot cap downloads. An earlier version used it; `Initialize` removes that leftover policy once (flag `LegacyQosRemoved`). Do not reintroduce QoS.

- The filter is `!loopback` at the network layer. All packets go into one queue (cap: 0.3 s of traffic, at least 128 KB). A send thread releases them with a token bucket; a packet may overdraw the bucket so large packets cannot deadlock. Packets over the queue cap are dropped on purpose, which is what slows TCP down.
- If the process dies, the WinDivert handle closes and traffic flows normally. Keep that fail-open property.
- `windivert.h` defines empty `__in` and `__out` macros that break libstdc++. `Shaper.h` includes the standard headers first and `#undef`s them after. Keep that order.
- The driver files are written to `%ProgramFiles%\TrafficGate\`, not a user-writable folder, because this process is elevated and must not load a DLL a normal user could swap.
- Updating WinDivert: replace the three files in `src/third_party/`, update `README.txt` there (hashes), and check the Microsoft signature of the `.sys`.

**The app is elevated.** `TrafficGate.manifest` requests `requireAdministrator`. Consequences:
- Windows will not start an elevated program from the `HKCU\...\Run` key, so "Start on boot" is a logon scheduled task named `TrafficGate` with highest privileges (created from XML via `schtasks`). An old `Run` entry is migrated automatically.
- A second launch only pokes the running instance (single-instance mutex) and shows its menu.

**The indicator is a child of the taskbar.** `PositionTaskbarWindow` turns a hidden popup into a child of `Shell_TrayWnd` (switch the styles, then `SetParent`) and positions it in the parent's client coordinates, `HWND_TOP`. It must never be topmost: a topmost window floats over full-screen video.
- A layered child window needs the Windows 8+ `supportedOS` entries in the manifest. Without them it silently does not draw.
- With a child window, call `UpdateLayeredWindow` with a null destination point and move it with `SetWindowPos` instead.
- The window is destroyed with its parent when Explorer restarts. `PositionTaskbarWindow` recreates it, and the 250 ms watchdog timer calls it repeatedly.
- The window shares an input queue with Explorer. Do not block the UI thread for long (the existing `schtasks` waits are short on purpose). Run slow work on a worker thread.
- Only the Windows 10 taskbar layout is handled. Windows 11 is not supported.

**Indicator numbers.** With no limit set, speeds come from `GetIfTable2`, counting only up, non-loopback, non-filter, hardware interfaces. `GetIfTable2` lists each adapter once per driver layer (QoS Packet Scheduler, WFP filters) with identical counters, and virtual or tunnel adapters re-count the same traffic, so summing every row multiplies the speed (it was about 4x on a normal PC). Do not widen this filter. With a limit active they come from the bytes the shaper actually re-injects (`DeliveredInboundBytes` / `DeliveredOutboundBytes`), because adapter counters include packets the limiter later drops. When the source changes, the baseline is reset (`lastUsedShaper`).

**Text rendering.** `RenderTaskbarWindow` draws with GDI, not GDI+: Segoe UI at 9 pt (`MulDiv(9, dpi, 72)`), `CLEARTYPE_QUALITY`, on whole-pixel positions, white on black into a 32-bit DIB. Each pixel then keeps its ClearType channels as premultiplied colour and uses the largest channel as alpha. That gives ClearType-looking text on a dark taskbar inside a per-pixel-alpha window. GDI+ text at fractional positions looked blurry; do not go back to it. The text colour is fixed white, so a light taskbar is not handled.

## Settings

Everything is in `%LOCALAPPDATA%\TrafficGate\settings.ini` (`GetSettingsFilePath`). Section `[TrafficGate]` has `ShowTaskbar`, `SpeedLimitEnabled`, `SpeedLimitBps`, `SpeedLimitInputVal`, `SpeedLimitInputUnit`, `TodayDate`, `TodayInBytes`, `TodayOutBytes`, `LegacyQosRemoved`. Section `[History]` has one `YYYY-MM-DD=down,up` line per day. Limit units: `0` KB/s (1024), `1` MB/s (1024 squared), `2` Mbps (10^6 bits), `3` Gbps; the limiter itself works in bits per second. Do not write settings anywhere else.

## Code conventions

- C++17, Unicode only (`UNICODE`/`_UNICODE`), Win32 API directly, wide strings, no exceptions and no RTTI (`-fno-exceptions -fno-rtti`; the build uses them).
- Keep it small: no frameworks, no new runtime dependencies. Prefer the Windows API over adding libraries.
- Windows target is `0x0601` for the compile defines; newer APIs are loaded with `GetProcAddress` (see `GetDpiForWindow`).
- The process is not DPI-aware by manifest; sizes are scaled from the DPI reported for the window.

## When you change something

- Keep README and this file true. If behaviour in either changes, update them in the same commit. The README opening paragraph and the "Common questions" section are written so search engines and AI assistants can summarise the app accurately; keep them plain, factual and in sync with what the app really does (including its limits: Windows 10 only, global limit, needs admin).
- Do not commit `dist/` or any `.exe`.
- Commit messages: short and imperative, one change per commit.
