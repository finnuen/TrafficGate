# TrafficGate

A small native Win32 tray app for Windows 10 that shows live network speed in the taskbar, caps your total bandwidth, and keeps a daily usage history.

Ships as **one executable** (`TrafficGate_v<version>.exe`, about 500 KB): no installer, no DLLs beside it, no background service. It asks for administrator rights on launch because the bandwidth limiter uses a packet-filter driver that is embedded in the exe.

![TrafficGate Icon](docs/icon.png)

## Features

- **Taskbar speed indicator.** Upload and download speed sit inside the taskbar, just left of the notification tray, in Segoe UI to match the clock. The indicator is a child window of the taskbar, so it is not always-on-top and it goes away with the taskbar when a full-screen video hides it.
- **Total bandwidth limit.** One cap that covers upload and download together (for example 1 MB/s in total). Set it from the tray menu: **Set custom limit...**, choose a value and a unit (KB/s, MB/s, Mbps or Gbps). While a limit is active the indicator shows the traffic that actually gets through.
- **Usage history.** Daily download, upload and total, kept between runs. Open it from the tray menu: **Usage history...** (the Reset button clears all of it).
- **Start on boot.** Optional, from the tray menu.
- **Show on Taskbar.** Hides or shows the indicator; the tray icon stays.

Left- or right-click the tray icon (or the indicator) for the menu. Hovering the tray icon shows the current speeds.

## Requirements

- Windows 10, 64-bit. The taskbar embedding relies on the classic Windows 10 taskbar; the newer Windows 11 taskbar is not supported.
- Administrator rights (a UAC prompt on launch).

## How the limiter works

Windows' own QoS policies can only throttle outgoing traffic, so TrafficGate uses the [WinDivert](https://github.com/basil00/WinDivert) 2.2.2 driver instead. Every non-loopback IP packet is diverted into one queue and released by a token bucket at the chosen rate. Packets that do not fit in the queue are dropped, which makes TCP slow itself down.

- The limit is shared by upload and download, and it includes local-network traffic.
- There is a small amount of added latency while a limit is on.
- If TrafficGate exits or crashes, the limit is gone with it. Nothing about it persists in Windows.
- On first use the driver files (`WinDivert.dll`, `WinDivert64.sys`) are extracted to `C:\Program Files\TrafficGate\`. Some antivirus products flag WinDivert, and Windows Memory Integrity can block the driver. TrafficGate shows an error and turns the limit off if that happens.

## Where things are stored

- Settings and usage history: `%LOCALAPPDATA%\TrafficGate\settings.ini`.
- Start on boot: a scheduled task named `TrafficGate` (it runs elevated, so the usual `Run` registry key cannot be used).
- Limiter driver files: `C:\Program Files\TrafficGate\`.

## Building

Requires MinGW-w64 (x86_64) with `g++`, `windres` and `objdump` on `PATH`.

```powershell
# Build and bump the version (1.10 -> 1.11)
powershell -ExecutionPolicy Bypass -File .\build.ps1

# Build the current version unchanged
powershell -ExecutionPolicy Bypass -File .\build.ps1 -NoVersionBump

# Build a specific version
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Version 1.0
```

The executable is written to `dist\TrafficGate_v<version>.exe`. The script keeps the version in `src/TrafficGate.h` and `src/TrafficGate.rc` in sync with the file name, and checks that the result only imports Windows system libraries.

To regenerate the icon (`src/TrafficGate.ico` and `docs/icon.png`):

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
```

If you are changing the code, read [AGENTS.md](AGENTS.md) first.

## Licence

MIT License. Copyright (c) 2026 TrafficGate.

The embedded WinDivert driver is licensed separately, under LGPLv3 or GPLv2 at your choice. See `src/third_party/WinDivert-LICENSE.txt`.
