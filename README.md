# TrafficGate

TrafficGate is a lightweight, native Win32 network speed indicator, bandwidth limiter, and data usage monitor for Windows. It docks directly into the taskbar next to the notification area and provides instant speed metrics and traffic control via its system tray context menu.

Ships as **one self-contained executable** (`TrafficGate_v1.0.exe`, ~370 KB) with no installer, no external runtime DLLs, and zero background services.

![TrafficGate Icon](docs/icon.png)

## Features

1. **Live Taskbar Speed Indicator**:
   - Displays real-time upload and download speeds (`↑ : 20 MB/s`, `↓ : 10 MB/s`) docked directly to the left of the taskbar notification tray.
   - Transparent 32-bpp layered rendering blends into Windows 10/11 taskbar themes with subpixel anti-aliased typography.
2. **Bandwidth Limiter**:
   - Policy-based QoS bandwidth throttling directly from the tray menu.
   - Quick presets: 1 MB/s (8 Mbps), 2 MB/s (16 Mbps), 5 MB/s (40 Mbps), 10 MB/s (80 Mbps), or custom limit dialog.
3. **Internet Usage Tracking**:
   - Right-click tray menu displays live stats:
     - Today's cumulative download, upload, and total usage.
     - Current session usage.
     - Option to reset today's usage statistics.
4. **Launch at Startup**:
   - Optional "Launch TrafficGate at Windows startup" toggle in the tray menu (configured via standard `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).
5. **Standalone Native C++**:
   - Built with C++17 and MinGW-w64.
   - Fully statically linked with zero third-party dependencies, importing only standard Windows system libraries (`iphlpapi.dll`, `gdiplus.dll`, `user32.dll`, `kernel32.dll`, `shell32.dll`).

## Building

Requires MinGW-w64 (`g++` and `windres`) on `PATH`.

```powershell
# Build and automatically bump minor version on changes
powershell -ExecutionPolicy Bypass -File .\build.ps1

# Build current version without bumping
powershell -ExecutionPolicy Bypass -File .\build.ps1 -NoVersionBump

# Build specific version
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Version 1.0
```

The compiled release executable is output to `dist\TrafficGate_v<version>.exe`.

To regenerate the multi-frame application icon (`src/TrafficGate.ico` and `docs/icon.png`):

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
```

## Licence

MIT License. Copyright (c) 2026 TrafficGate.
