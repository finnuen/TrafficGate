# TrafficGate: network speed meter and total bandwidth limiter for Windows 10

**TrafficGate is a free, open-source Windows 10 tray app that shows your live upload and download speed in the taskbar and lets you cap your total internet bandwidth (upload and download together) with one setting.** It also records daily data usage. It is a single ~500 KB `.exe`: no installer, no background service, nothing else to install.

Use it when you want to see how fast your connection really is, keep a download from saturating your network, or stay inside a monthly data cap.

![TrafficGate Icon](docs/icon.png)

**[Download the latest release](../../releases/latest)**, run `TrafficGate_v<version>.exe`, accept the UAC prompt, and right-click the tray icon.

## What it does

- **Taskbar speed meter.** Live upload and download speed sit inside the taskbar next to the notification tray, in Segoe UI so they match the clock. The indicator is part of the taskbar rather than a floating overlay, so it is not always-on-top and it does not draw over full-screen video.
- **One bandwidth cap for the whole PC.** Set a single limit that covers upload and download combined, for example "1 MB/s in total". Units: KB/s, MB/s, Mbps or Gbps. Turn it on and off from the tray menu.
- **Usage history.** Daily download, upload and total, kept between runs, with a history window.
- **Start on boot.** Optional toggle in the tray menu.
- **Tiny and self-contained.** Native C++ (Win32), one executable, settings in a single `settings.ini`.

Right-click (or left-click) the tray icon or the indicator for the menu: **Usage history...**, **Set custom limit...**, **Show on Taskbar**, **Start on boot**, **Exit**.

## Common questions

**How do I limit my total internet speed on Windows 10?**
Run TrafficGate, choose **Set custom limit...** from the tray menu, enter a value and a unit, and enable it. The cap applies to upload and download together.

**Can Windows 10 limit download speed on its own?**
Not for the whole connection. Windows QoS policies (`New-NetQosPolicy`) can only throttle outgoing traffic. TrafficGate limits both directions by queuing packets with the WinDivert driver and releasing them at the chosen rate.

**How do I show network speed in the Windows 10 taskbar?**
TrafficGate does this by default: upload and download speed appear just left of the notification area.

**Does it limit speed per application?**
No. The limit is global for the PC. If you need per-application limits, use a dedicated per-app tool such as NetLimiter or NetBalancer (both commercial).

**Is it free? Is it open source?**
Yes. MIT licensed, source in this repository. The embedded WinDivert driver is licensed separately (LGPLv3 or GPLv2).

**Does it work on Windows 11?**
Not currently. The taskbar embedding relies on the Windows 10 taskbar layout.

**Why does it need administrator rights?**
The limiter loads a packet-filter driver (WinDivert), which requires elevation. The app asks for it once at launch.

**Is it safe? Will the limit stay on if it crashes?**
If TrafficGate exits or crashes, the limit disappears with it; nothing about the limiter persists in Windows. Some antivirus products flag WinDivert because it is a packet-interception driver, and Windows Memory Integrity can block it. TrafficGate shows an error and turns the limit off if the driver cannot load.

## Requirements

- Windows 10, 64-bit.
- Administrator rights (a UAC prompt on launch).

## How the limiter works

Every non-loopback IP packet is diverted by WinDivert 2.2.2 into one queue and released by a token bucket at the chosen rate. Packets that do not fit in the queue are dropped, which makes TCP slow itself down.

- The limit is shared by upload and download, and it includes local-network traffic.
- There is a small amount of added latency while a limit is on.
- While a limit is active, the taskbar indicator shows the traffic that actually gets through.
- On first use the driver files (`WinDivert.dll`, `WinDivert64.sys`) are extracted to `C:\Program Files\TrafficGate\`.

## Where things are stored

- Settings and usage history: `%LOCALAPPDATA%\TrafficGate\settings.ini`.
- Start on boot: a scheduled task named `TrafficGate` (it runs elevated, so the usual `Run` registry key cannot be used).
- Limiter driver files: `C:\Program Files\TrafficGate\`.

## Building

Requires MinGW-w64 (x86_64) with `g++`, `windres` and `objdump` on `PATH`.

```powershell
# Build and bump the version (1.11 -> 1.12)
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
