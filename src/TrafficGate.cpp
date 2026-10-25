#include "TrafficGate.h"
#include <cstdio>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

TrafficGateApp& TrafficGateApp::Instance() {
    static TrafficGateApp s_instance;
    return s_instance;
}

std::wstring TrafficGateApp::GetSettingsFilePath() const {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring dir = std::wstring(localAppData) + L"\\TrafficGate";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }
    return L"settings.ini";
}

std::wstring TrafficGateApp::GetCurrentDateString() const {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

void TrafficGateApp::LoadSettings() {
    std::wstring path = GetSettingsFilePath();
    showTaskbar = GetPrivateProfileIntW(L"TrafficGate", L"ShowTaskbar", 1, path.c_str()) != 0;
    fixedMbUnit = GetPrivateProfileIntW(L"TrafficGate", L"FixedMbUnit", 1, path.c_str()) != 0;
    speedLimitEnabled = GetPrivateProfileIntW(L"TrafficGate", L"SpeedLimitEnabled", 0, path.c_str()) != 0;

    wchar_t buf[64] = { 0 };
    GetPrivateProfileStringW(L"TrafficGate", L"SpeedLimitBps", L"0", buf, 64, path.c_str());
    speedLimitBps = _wcstoui64(buf, nullptr, 10);

    GetPrivateProfileStringW(L"TrafficGate", L"TodayDate", L"", buf, 64, path.c_str());
    stats.todayDateStr = buf;

    GetPrivateProfileStringW(L"TrafficGate", L"TodayInBytes", L"0", buf, 64, path.c_str());
    stats.todayInBytes = _wcstoui64(buf, nullptr, 10);

    GetPrivateProfileStringW(L"TrafficGate", L"TodayOutBytes", L"0", buf, 64, path.c_str());
    stats.todayOutBytes = _wcstoui64(buf, nullptr, 10);

    std::wstring curDate = GetCurrentDateString();
    if (stats.todayDateStr != curDate) {
        stats.todayDateStr = curDate;
        stats.todayInBytes = 0;
        stats.todayOutBytes = 0;
    }
}

void TrafficGateApp::SaveSettings() {
    std::wstring path = GetSettingsFilePath();
    WritePrivateProfileStringW(L"TrafficGate", L"ShowTaskbar", showTaskbar ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"TrafficGate", L"FixedMbUnit", fixedMbUnit ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitEnabled", speedLimitEnabled ? L"1" : L"0", path.c_str());

    wchar_t buf[64];
    swprintf_s(buf, L"%llu", speedLimitBps);
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitBps", buf, path.c_str());

    WritePrivateProfileStringW(L"TrafficGate", L"TodayDate", stats.todayDateStr.c_str(), path.c_str());

    swprintf_s(buf, L"%llu", stats.todayInBytes);
    WritePrivateProfileStringW(L"TrafficGate", L"TodayInBytes", buf, path.c_str());

    swprintf_s(buf, L"%llu", stats.todayOutBytes);
    WritePrivateProfileStringW(L"TrafficGate", L"TodayOutBytes", buf, path.c_str());
}

bool TrafficGateApp::IsRunAtStartupEnabled() const {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD cbData = 0;
        LSTATUS st = RegQueryValueExW(hKey, L"TrafficGate", nullptr, &type, nullptr, &cbData);
        RegCloseKey(hKey);
        return (st == ERROR_SUCCESS && cbData > 0);
    }
    return false;
}

void TrafficGateApp::ToggleRunAtStartup() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (IsRunAtStartupEnabled()) {
            RegDeleteValueW(hKey, L"TrafficGate");
        } else {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"TrafficGate", 0, REG_SZ, (const BYTE*)quoted.c_str(), (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hKey);
    }
}

void TrafficGateApp::ToggleTaskbarVisible() {
    showTaskbar = !showTaskbar;
    SaveSettings();
    if (showTaskbar) {
        PositionTaskbarWindow();
        RenderTaskbarWindow();
        ShowWindow(hTaskbarWnd, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(hTaskbarWnd, SW_HIDE);
    }
}

void TrafficGateApp::ToggleFixedMbUnit() {
    fixedMbUnit = !fixedMbUnit;
    SaveSettings();
    RenderTaskbarWindow();
}

void TrafficGateApp::ResetTodayStats() {
    stats.todayInBytes = 0;
    stats.todayOutBytes = 0;
    SaveSettings();
}

void TrafficGateApp::SetSpeedLimit(uint64_t bps, bool enable) {
    speedLimitEnabled = enable;
    speedLimitBps = enable ? bps : 0;
    SaveSettings();

    std::wstring cmd;
    if (enable && bps > 0) {
        cmd = L"-WindowStyle Hidden -Command \"Remove-NetQosPolicy -Name 'TrafficGate_Limit' -Confirm:$false -ErrorAction SilentlyContinue; New-NetQosPolicy -Name 'TrafficGate_Limit' -ThrottleRateActionBitsPerSecond " + std::to_wstring(bps) + L"\"";
    } else {
        cmd = L"-WindowStyle Hidden -Command \"Remove-NetQosPolicy -Name 'TrafficGate_Limit' -Confirm:$false -ErrorAction SilentlyContinue\"";
    }

    // QoS policy modification requires elevation
    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = cmd.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    ShellExecuteExW(&sei);
}

std::wstring TrafficGateApp::FormatSpeed(uint64_t bytesPerSec) {
    double bps = (double)bytesPerSec;
    wchar_t buf[32];
    if (bps >= 1024.0 * 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
    } else if (bps >= 1024.0 * 1024.0) {
        if (bps >= 10.0 * 1024.0 * 1024.0) {
            swprintf_s(buf, L"%.0f MB/s", bps / (1024.0 * 1024.0));
        } else {
            swprintf_s(buf, L"%.1f MB/s", bps / (1024.0 * 1024.0));
        }
    } else if (bps >= 1024.0) {
        swprintf_s(buf, L"%.0f KB/s", bps / 1024.0);
    } else {
        swprintf_s(buf, L"%llu B/s", bytesPerSec);
    }
    return buf;
}

std::wstring TrafficGateApp::FormatSpeedNumber(uint64_t bytesPerSec, bool fixedMb) {
    double bps = (double)bytesPerSec;
    wchar_t buf[32];
    if (fixedMb) {
        double mb = bps / (1024.0 * 1024.0);
        if (mb >= 10.0) {
            swprintf_s(buf, L"%.0f", mb);
        } else if (mb >= 0.1) {
            swprintf_s(buf, L"%.1f", mb);
        } else if (bytesPerSec > 0) {
            swprintf_s(buf, L"0.1");
        } else {
            swprintf_s(buf, L"0");
        }
    } else {
        if (bps >= 1024.0 * 1024.0 * 1024.0) {
            swprintf_s(buf, L"%.1f", bps / (1024.0 * 1024.0 * 1024.0));
        } else if (bps >= 1024.0 * 1024.0) {
            double mb = bps / (1024.0 * 1024.0);
            if (mb >= 10.0) swprintf_s(buf, L"%.0f", mb);
            else swprintf_s(buf, L"%.1f", mb);
        } else if (bps >= 1024.0) {
            swprintf_s(buf, L"%.0f", bps / 1024.0);
        } else {
            swprintf_s(buf, L"%llu", bytesPerSec);
        }
    }
    return buf;
}

std::wstring TrafficGateApp::FormatSpeedUnit(uint64_t bytesPerSec, bool fixedMb) {
    if (fixedMb) return L"MB/s";
    double bps = (double)bytesPerSec;
    if (bps >= 1024.0 * 1024.0 * 1024.0) return L"GB/s";
    if (bps >= 1024.0 * 1024.0) return L"MB/s";
    if (bps >= 1024.0) return L"KB/s";
    return L"B/s";
}

std::wstring TrafficGateApp::FormatBytes(uint64_t bytes) {
    double b = (double)bytes;
    wchar_t buf[32];
    if (b >= 1024.0 * 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    } else if (b >= 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f MB", b / (1024.0 * 1024.0));
    } else if (b >= 1024.0) {
        swprintf_s(buf, L"%.0f KB", b / 1024.0);
    } else {
        swprintf_s(buf, L"%llu B", bytes);
    }
    return buf;
}

void TrafficGateApp::UpdateNetworkStats() {
    PMIB_IF_TABLE2 pTable = nullptr;
    if (GetIfTable2(&pTable) != NO_ERROR || !pTable) {
        return;
    }

    uint64_t totalIn = 0;
    uint64_t totalOut = 0;

    for (ULONG i = 0; i < pTable->NumEntries; ++i) {
        const MIB_IF_ROW2& row = pTable->Table[i];
        if (row.OperStatus == IfOperStatusUp &&
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            (row.InOctets > 0 || row.OutOctets > 0)) {
            totalIn += row.InOctets;
            totalOut += row.OutOctets;
        }
    }
    FreeMibTable(pTable);

    ULONGLONG now = GetTickCount64();

    if (!hasBaseline) {
        prevInOctets = totalIn;
        prevOutOctets = totalOut;
        prevTickMs = now;
        hasBaseline = true;
        return;
    }

    ULONGLONG elapsed = now - prevTickMs;
    if (elapsed < 100) return; // Skip tiny increments

    uint64_t deltaIn = (totalIn >= prevInOctets) ? (totalIn - prevInOctets) : 0;
    uint64_t deltaOut = (totalOut >= prevOutOctets) ? (totalOut - prevOutOctets) : 0;

    prevInOctets = totalIn;
    prevOutOctets = totalOut;
    prevTickMs = now;

    stats.currentDownBps = (uint64_t)((double)deltaIn * 1000.0 / (double)elapsed);
    stats.currentUpBps = (uint64_t)((double)deltaOut * 1000.0 / (double)elapsed);

    stats.sessionInBytes += deltaIn;
    stats.sessionOutBytes += deltaOut;

    std::wstring curDate = GetCurrentDateString();
    if (curDate != stats.todayDateStr) {
        stats.todayDateStr = curDate;
        stats.todayInBytes = 0;
        stats.todayOutBytes = 0;
    }
    stats.todayInBytes += deltaIn;
    stats.todayOutBytes += deltaOut;

    UpdateTrayTooltip();
    if (showTaskbar) {
        PositionTaskbarWindow();
        RenderTaskbarWindow();
    }
}

void TrafficGateApp::PositionTaskbarWindow() {
    if (!hTaskbarWnd) return;

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray || !IsWindowVisible(hTray)) {
        ShowWindow(hTaskbarWnd, SW_HIDE);
        return;
    }

    // Ensure hTray is the owner so hTaskbarWnd can never drop behind hTray in Z-order
    HWND curOwner = GetWindow(hTaskbarWnd, GW_OWNER);
    if (curOwner != hTray) {
        SetWindowLongPtrW(hTaskbarWnd, GWLP_HWNDPARENT, (LONG_PTR)hTray);
    }

    RECT rcTray = { 0 };
    GetWindowRect(hTray, &rcTray);

    HWND hNotify = FindWindowExW(hTray, nullptr, L"TrayNotifyWnd", nullptr);
    int rightBoundary = 0;

    if (hNotify && IsWindowVisible(hNotify)) {
        // Find the "^" Show hidden icons button inside TrayNotifyWnd
        HWND hChevron = FindWindowExW(hNotify, nullptr, L"Button", nullptr);
        if (hChevron && IsWindowVisible(hChevron)) {
            RECT rcChevron = { 0 };
            GetWindowRect(hChevron, &rcChevron);
            rightBoundary = rcChevron.left;
        } else {
            RECT rcNotify = { 0 };
            GetWindowRect(hNotify, &rcNotify);
            rightBoundary = rcNotify.left;
        }
    } else {
        rightBoundary = rcTray.right - 180;
    }

    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
    auto pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
    if (pGetDpiForWindow) {
        dpi = pGetDpiForWindow(hTaskbarWnd);
        if (dpi == 0) dpi = 96;
    }
    taskbarDpi = dpi;

    float dpiScale = (float)dpi / 96.0f;
    int width = (int)(80.0f * dpiScale);
    int height = rcTray.bottom - rcTray.top;
    if (height < 32) height = 40;

    int x = rightBoundary - width - (int)(3.0f * dpiScale);
    int y = rcTray.top;

    if (x < rcTray.left) x = rcTray.left;

    lastTaskbarRect = { x, y, x + width, y + height };

    // Always re-assert HWND_TOPMOST so opening the overflow flyout or clicking Explorer never hides it
    SetWindowPos(hTaskbarWnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | (showTaskbar ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void TrafficGateApp::RenderTaskbarWindow() {
    if (!hTaskbarWnd || !showTaskbar) return;

    RECT rc = { 0 };
    GetWindowRect(hTaskbarWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // Top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // Transparent background
    memset(pBits, 0, width * height * 4);

    {
        Graphics g(hdcMem);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        float dpiScale = (float)taskbarDpi / 96.0f;
        // Same font and size as Windows date and time clock (9pt Segoe UI)
        float fontSize = 11.5f * dpiScale;

        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, fontSize, FontStyleRegular, UnitPixel);

        // Arrows: light cyan / blue
        // Numbers & Unit: pure white (same as taskbar clock)
        SolidBrush arrowBrush(Color(255, 68, 182, 255));
        SolidBrush textBrush(Color(255, 255, 255, 255));

        // Fixed column positions:
        // Column 1: Static arrows (xArrow)
        // Column 2: Speed number right-aligned to (xNumRight)
        // Column 3: Static unit (xUnit)
        float xArrow = 2.0f * dpiScale;
        float xNumRight = 48.0f * dpiScale;
        float xUnit = 51.0f * dpiScale;

        float line1Y = (float)height * 0.10f;
        float line2Y = (float)height * 0.52f;

        // Line 1: Upload (↑    <num> MB/s)
        PointF ptArrow1(xArrow, line1Y);
        g.DrawString(L"\x2191", -1, &font, ptArrow1, &arrowBrush);

        std::wstring upNum = FormatSpeedNumber(stats.currentUpBps, fixedMbUnit);
        RectF rcNum1;
        g.MeasureString(upNum.c_str(), -1, &font, PointF(0, 0), &rcNum1);
        PointF ptNum1(xNumRight - rcNum1.Width, line1Y);
        g.DrawString(upNum.c_str(), -1, &font, ptNum1, &textBrush);

        std::wstring upUnit = FormatSpeedUnit(stats.currentUpBps, fixedMbUnit);
        PointF ptUnit1(xUnit, line1Y);
        g.DrawString(upUnit.c_str(), -1, &font, ptUnit1, &textBrush);

        // Line 2: Download (↓    <num> MB/s)
        PointF ptArrow2(xArrow, line2Y);
        g.DrawString(L"\x2193", -1, &font, ptArrow2, &arrowBrush);

        std::wstring downNum = FormatSpeedNumber(stats.currentDownBps, fixedMbUnit);
        RectF rcNum2;
        g.MeasureString(downNum.c_str(), -1, &font, PointF(0, 0), &rcNum2);
        PointF ptNum2(xNumRight - rcNum2.Width, line2Y);
        g.DrawString(downNum.c_str(), -1, &font, ptNum2, &textBrush);

        std::wstring downUnit = FormatSpeedUnit(stats.currentDownBps, fixedMbUnit);
        PointF ptUnit2(xUnit, line2Y);
        g.DrawString(downUnit.c_str(), -1, &font, ptUnit2, &textBrush);
    }

    POINT ptSrc = { 0, 0 };
    POINT ptDst = { rc.left, rc.top };
    SIZE wndSize = { width, height };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hTaskbarWnd, hdcScreen, &ptDst, &wndSize, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void TrafficGateApp::ShowContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();

    // 1. Header (Application title & version)
    std::wstring title = std::wstring(APP_NAME) + L" v" + APP_VERSION;
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER_VERSION, title.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. Current speeds
    std::wstring upStr = L"  \x2191 Upload:   " + FormatSpeed(stats.currentUpBps);
    std::wstring downStr = L"  \x2193 Download: " + FormatSpeed(stats.currentDownBps);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_SPEED_UP, upStr.c_str());
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_SPEED_DOWN, downStr.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 3. Internet Usage (Requirement 3)
    std::wstring todayStr = L"  Today:   " + FormatBytes(stats.todayInBytes + stats.todayOutBytes) +
                            L" (\x2193 " + FormatBytes(stats.todayInBytes) +
                            L" | \x2191 " + FormatBytes(stats.todayOutBytes) + L")";
    std::wstring sessionStr = L"  Session: " + FormatBytes(stats.sessionInBytes + stats.sessionOutBytes) +
                              L" (\x2193 " + FormatBytes(stats.sessionInBytes) +
                              L" | \x2191 " + FormatBytes(stats.sessionOutBytes) + L")";
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_USAGE_TODAY, todayStr.c_str());
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_USAGE_SESSION, sessionStr.c_str());
    AppendMenuW(hMenu, MF_STRING, IDM_USAGE_RESET, L"  Reset Today's Stats");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 4. Speed Limiter Submenu (Requirement 2)
    HMENU hSubLimit = CreatePopupMenu();
    AppendMenuW(hSubLimit, MF_STRING | (!speedLimitEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_LIMIT_OFF, L"Disabled (Unlimited)");
    AppendMenuW(hSubLimit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hSubLimit, MF_STRING | (speedLimitEnabled && speedLimitBps == 8000000ULL ? MF_CHECKED : MF_UNCHECKED), IDM_LIMIT_1MB, L"1 MB/s (8 Mbps)");
    AppendMenuW(hSubLimit, MF_STRING | (speedLimitEnabled && speedLimitBps == 16000000ULL ? MF_CHECKED : MF_UNCHECKED), IDM_LIMIT_2MB, L"2 MB/s (16 Mbps)");
    AppendMenuW(hSubLimit, MF_STRING | (speedLimitEnabled && speedLimitBps == 40000000ULL ? MF_CHECKED : MF_UNCHECKED), IDM_LIMIT_5MB, L"5 MB/s (40 Mbps)");
    AppendMenuW(hSubLimit, MF_STRING | (speedLimitEnabled && speedLimitBps == 80000000ULL ? MF_CHECKED : MF_UNCHECKED), IDM_LIMIT_10MB, L"10 MB/s (80 Mbps)");
    AppendMenuW(hSubLimit, MF_SEPARATOR, 0, nullptr);

    std::wstring customLabel = L"Set Custom Limit...";
    if (speedLimitEnabled && speedLimitBps != 8000000ULL && speedLimitBps != 16000000ULL &&
        speedLimitBps != 40000000ULL && speedLimitBps != 80000000ULL) {
        customLabel = L"Custom: " + FormatSpeed(speedLimitBps / 8) + L" (Active)";
    }
    AppendMenuW(hSubLimit, MF_STRING, IDM_LIMIT_CUSTOM, customLabel.c_str());

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubLimit, L"Speed Limiter");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 5. Taskbar and Boot Toggles (Requirements 5 & 6)
    AppendMenuW(hMenu, MF_STRING | (showTaskbar ? MF_CHECKED : MF_UNCHECKED), IDM_SHOW_TASKBAR, L"Show on Taskbar");
    AppendMenuW(hMenu, MF_STRING | (fixedMbUnit ? MF_CHECKED : MF_UNCHECKED), IDM_FIXED_MB_UNIT, L"Fixed MB/s Unit");
    AppendMenuW(hMenu, MF_STRING | (IsRunAtStartupEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_STARTUP_RUN, L"Launch TrafficGate at Windows startup");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 6. Exit
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit TrafficGate");

    SetForegroundWindow(hDaemonWnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hDaemonWnd, nullptr);
    DestroyMenu(hMenu);
    PostMessageW(hDaemonWnd, WM_NULL, 0, 0);

    if (cmd != 0) {
        SendMessageW(hDaemonWnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
    }
}

void TrafficGateApp::OpenCustomLimitDialog() {
    DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_CUSTOM_LIMIT), hDaemonWnd, CustomLimitDlgProc, (LPARAM)this);
}

INT_PTR CALLBACK TrafficGateApp::CustomLimitDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static TrafficGateApp* pApp = nullptr;

    switch (msg) {
        case WM_INITDIALOG: {
            pApp = (TrafficGateApp*)lParam;
            HWND hCombo = GetDlgItem(hDlg, IDC_LIMIT_UNIT);
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"MB/s");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"KB/s");
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

            if (pApp && pApp->IsSpeedLimitEnabled()) {
                uint64_t bps = pApp->GetSpeedLimitBps();
                uint64_t bytesPerSec = bps / 8;
                std::wstring status = L"Active Limit: " + FormatSpeed(bytesPerSec);
                SetDlgItemTextW(hDlg, IDC_LIMIT_STATUS, status.c_str());

                if (bytesPerSec >= 1024 * 1024) {
                    SetDlgItemInt(hDlg, IDC_LIMIT_VALUE, (UINT)(bytesPerSec / (1024 * 1024)), FALSE);
                    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
                } else {
                    SetDlgItemInt(hDlg, IDC_LIMIT_VALUE, (UINT)(bytesPerSec / 1024), FALSE);
                    SendMessageW(hCombo, CB_SETCURSEL, 1, 0);
                }
            } else {
                SetDlgItemInt(hDlg, IDC_LIMIT_VALUE, 5, FALSE);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                BOOL translated = FALSE;
                UINT val = GetDlgItemInt(hDlg, IDC_LIMIT_VALUE, &translated, FALSE);
                if (translated && val > 0) {
                    HWND hCombo = GetDlgItem(hDlg, IDC_LIMIT_UNIT);
                    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                    uint64_t bytesSec = (sel == 0) ? ((uint64_t)val * 1024ULL * 1024ULL) : ((uint64_t)val * 1024ULL);
                    uint64_t bps = bytesSec * 8ULL;
                    if (pApp) {
                        pApp->SetSpeedLimit(bps, true);
                    }
                }
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void TrafficGateApp::InitTrayIcon() {
    hAppIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!hAppIcon) {
        hAppIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hDaemonWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = hAppIcon;
    wcsncpy_s(nid.szTip, L"TrafficGate v1.0", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void TrafficGateApp::UpdateTrayTooltip() {
    std::wstring tip = std::wstring(APP_NAME) + L" v" + APP_VERSION +
                       L"\n\x2191 " + FormatSpeed(stats.currentUpBps) +
                       L"  \x2193 " + FormatSpeed(stats.currentDownBps);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrafficGateApp::RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (hAppIcon) {
        DestroyIcon(hAppIcon);
        hAppIcon = nullptr;
    }
}

LRESULT CALLBACK TrafficGateApp::DaemonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrafficGateApp& app = TrafficGateApp::Instance();

    if (msg == app.wmTaskbarCreated) {
        app.InitTrayIcon();
        if (app.showTaskbar) {
            app.PositionTaskbarWindow();
            app.RenderTaskbarWindow();
        }
        return 0;
    }

    switch (msg) {
        case WM_TIMER:
            if (wParam == IDT_TIMER_POLL) {
                app.UpdateNetworkStats();
            } else if (wParam == IDT_TIMER_WATCH) {
                if (app.showTaskbar) {
                    app.PositionTaskbarWindow();
                }
            }
            return 0;

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                app.ShowContextMenu(pt);
            }
            return 0;
        }

        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            switch (cmd) {
                case IDM_SHOW_TASKBAR:
                    app.ToggleTaskbarVisible();
                    break;

                case IDM_FIXED_MB_UNIT:
                    app.ToggleFixedMbUnit();
                    break;

                case IDM_STARTUP_RUN:
                    app.ToggleRunAtStartup();
                    break;

                case IDM_USAGE_RESET:
                    if (MessageBoxW(hWnd, L"Reset today's download and upload statistics?", L"TrafficGate", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        app.ResetTodayStats();
                    }
                    break;

                case IDM_LIMIT_OFF:
                    app.SetSpeedLimit(0, false);
                    break;

                case IDM_LIMIT_1MB:
                    app.SetSpeedLimit(8000000ULL, true);
                    break;

                case IDM_LIMIT_2MB:
                    app.SetSpeedLimit(16000000ULL, true);
                    break;

                case IDM_LIMIT_5MB:
                    app.SetSpeedLimit(40000000ULL, true);
                    break;

                case IDM_LIMIT_10MB:
                    app.SetSpeedLimit(80000000ULL, true);
                    break;

                case IDM_LIMIT_CUSTOM:
                    app.OpenCustomLimitDialog();
                    break;

                case IDM_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        }

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (app.showTaskbar) {
                app.PositionTaskbarWindow();
                app.RenderTaskbarWindow();
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK TrafficGateApp::TaskbarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrafficGateApp& app = TrafficGateApp::Instance();

    switch (msg) {
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            app.ShowContextMenu(pt);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool TrafficGateApp::Initialize(HINSTANCE hInstance) {
    hInst = hInstance;

    // Single instance mutex
    hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(APP_WNDCLASS_DAEMON, nullptr);
        if (hExisting) {
            POINT pt;
            GetCursorPos(&pt);
            PostMessageW(hExisting, WM_TRAYICON, 0, WM_RBUTTONUP);
        }
        return false;
    }

    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // Common Controls
    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Register Daemon Window Class
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = DaemonWndProc;
    wcx.hInstance = hInst;
    wcx.lpszClassName = APP_WNDCLASS_DAEMON;
    RegisterClassExW(&wcx);

    hDaemonWnd = CreateWindowExW(0, APP_WNDCLASS_DAEMON, L"TrafficGateDaemon",
                                 WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                                 nullptr, nullptr, hInst, nullptr);

    // Register Taskbar Docked Window Class
    WNDCLASSEXW wct = { 0 };
    wct.cbSize = sizeof(wct);
    wct.lpfnWndProc = TaskbarWndProc;
    wct.hInstance = hInst;
    wct.lpszClassName = APP_WNDCLASS_TASKBAR;
    wct.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wct);

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);

    hTaskbarWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        APP_WNDCLASS_TASKBAR, L"TrafficGateTaskbar",
        WS_POPUP | WS_CLIPSIBLINGS,
        0, 0, 80, 40,
        hTray, nullptr, hInst, nullptr);

    LoadSettings();
    InitTrayIcon();

    if (showTaskbar) {
        PositionTaskbarWindow();
        RenderTaskbarWindow();
        ShowWindow(hTaskbarWnd, SW_SHOWNOACTIVATE);
    }

    // Poll every 1 second for network stats
    SetTimer(hDaemonWnd, IDT_TIMER_POLL, 1000, nullptr);
    // Watchdog every 250ms for taskbar position & Z-order
    SetTimer(hDaemonWnd, IDT_TIMER_WATCH, 250, nullptr);

    return true;
}

int TrafficGateApp::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void TrafficGateApp::Shutdown() {
    if (hDaemonWnd) {
        KillTimer(hDaemonWnd, IDT_TIMER_POLL);
        KillTimer(hDaemonWnd, IDT_TIMER_WATCH);
    }
    SaveSettings();
    RemoveTrayIcon();

    if (hTaskbarWnd) {
        DestroyWindow(hTaskbarWnd);
        hTaskbarWnd = nullptr;
    }
    if (hDaemonWnd) {
        DestroyWindow(hDaemonWnd);
        hDaemonWnd = nullptr;
    }
    if (gdiplusToken) {
        GdiplusShutdown(gdiplusToken);
        gdiplusToken = 0;
    }
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        hMutex = nullptr;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    TrafficGateApp& app = TrafficGateApp::Instance();
    if (!app.Initialize(hInstance)) {
        return 0;
    }
    int code = app.Run();
    app.Shutdown();
    return code;
}
