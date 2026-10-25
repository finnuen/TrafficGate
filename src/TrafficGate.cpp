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
    // Always %LOCALAPPDATA%\\TrafficGate\\settings.ini
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData)) ||
        !localAppData[0]) {
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
            localAppData[0] = L'\0';
        }
    }
    std::wstring dir = std::wstring(localAppData) + L"\\TrafficGate";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
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
    speedLimitEnabled = GetPrivateProfileIntW(L"TrafficGate", L"SpeedLimitEnabled", 0, path.c_str()) != 0;

    wchar_t buf[64] = { 0 };
    GetPrivateProfileStringW(L"TrafficGate", L"SpeedLimitBps", L"0", buf, 64, path.c_str());
    speedLimitBps = _wcstoui64(buf, nullptr, 10);

    speedLimitInputVal = GetPrivateProfileIntW(L"TrafficGate", L"SpeedLimitInputVal", 10, path.c_str());
    speedLimitInputUnit = GetPrivateProfileIntW(L"TrafficGate", L"SpeedLimitInputUnit", 1, path.c_str());

    GetPrivateProfileStringW(L"TrafficGate", L"TodayDate", L"", buf, 64, path.c_str());
    stats.todayDateStr = buf;

    GetPrivateProfileStringW(L"TrafficGate", L"TodayInBytes", L"0", buf, 64, path.c_str());
    stats.todayInBytes = _wcstoui64(buf, nullptr, 10);

    GetPrivateProfileStringW(L"TrafficGate", L"TodayOutBytes", L"0", buf, 64, path.c_str());
    stats.todayOutBytes = _wcstoui64(buf, nullptr, 10);

    std::wstring curDate = GetCurrentDateString();
    if (stats.todayDateStr.empty()) {
        stats.todayDateStr = curDate;
    } else if (stats.todayDateStr != curDate) {
        SaveTodayHistory();
        stats.todayDateStr = curDate;
        stats.todayInBytes = 0;
        stats.todayOutBytes = 0;
    }
}

void TrafficGateApp::SaveSettings() {
    std::wstring path = GetSettingsFilePath();
    WritePrivateProfileStringW(L"TrafficGate", L"ShowTaskbar", showTaskbar ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitEnabled", speedLimitEnabled ? L"1" : L"0", path.c_str());

    wchar_t buf[64];
    swprintf_s(buf, L"%llu", speedLimitBps);
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitBps", buf, path.c_str());

    swprintf_s(buf, L"%u", speedLimitInputVal);
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitInputVal", buf, path.c_str());

    swprintf_s(buf, L"%d", speedLimitInputUnit);
    WritePrivateProfileStringW(L"TrafficGate", L"SpeedLimitInputUnit", buf, path.c_str());

    WritePrivateProfileStringW(L"TrafficGate", L"TodayDate", stats.todayDateStr.c_str(), path.c_str());

    swprintf_s(buf, L"%llu", stats.todayInBytes);
    WritePrivateProfileStringW(L"TrafficGate", L"TodayInBytes", buf, path.c_str());

    swprintf_s(buf, L"%llu", stats.todayOutBytes);
    WritePrivateProfileStringW(L"TrafficGate", L"TodayOutBytes", buf, path.c_str());

    SaveTodayHistory();
}

void TrafficGateApp::SaveTodayHistory() {
    if (stats.todayDateStr.empty()) return;
    std::wstring path = GetSettingsFilePath();
    wchar_t val[128];
    swprintf_s(val, L"%llu,%llu", stats.todayInBytes, stats.todayOutBytes);
    WritePrivateProfileStringW(L"History", stats.todayDateStr.c_str(), val, path.c_str());
}

std::vector<UsageRecord> TrafficGateApp::GetUsageHistory() {
    std::vector<UsageRecord> records;
    std::wstring path = GetSettingsFilePath();

    // Read [History] section
    std::vector<wchar_t> buffer(32768, 0);
    DWORD len = GetPrivateProfileSectionW(L"History", buffer.data(), (DWORD)buffer.size(), path.c_str());

    bool todayFound = false;
    if (len > 0) {
        wchar_t* p = buffer.data();
        while (*p) {
            std::wstring line = p;
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring date = line.substr(0, eq);
                std::wstring data = line.substr(eq + 1);
                size_t comma = data.find(L',');
                if (comma != std::wstring::npos) {
                    UsageRecord rec;
                    rec.date = date;
                    rec.downBytes = _wcstoui64(data.substr(0, comma).c_str(), nullptr, 10);
                    rec.upBytes = _wcstoui64(data.substr(comma + 1).c_str(), nullptr, 10);
                    if (rec.date == stats.todayDateStr) {
                        rec.downBytes = stats.todayInBytes;
                        rec.upBytes = stats.todayOutBytes;
                        todayFound = true;
                    }
                    records.push_back(rec);
                }
            }
            p += line.length() + 1;
        }
    }

    if (!todayFound && !stats.todayDateStr.empty()) {
        UsageRecord rec;
        rec.date = stats.todayDateStr;
        rec.downBytes = stats.todayInBytes;
        rec.upBytes = stats.todayOutBytes;
        records.push_back(rec);
    }

    // Sort descending by date (newest first)
    std::sort(records.begin(), records.end(), [](const UsageRecord& a, const UsageRecord& b) {
        return a.date > b.date;
    });

    return records;
}

void TrafficGateApp::ClearUsageHistory() {
    std::wstring path = GetSettingsFilePath();
    WritePrivateProfileSectionW(L"History", L"", path.c_str());
    stats.todayInBytes = 0;
    stats.todayOutBytes = 0;
    SaveSettings();
}

// Runs a hidden command line and returns its exit code (-1 on launch failure).
static int RunHidden(std::wstring cmdLine, bool wait) {
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    DWORD code = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, 20000);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

static std::wstring XmlEscape(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

// The app runs elevated, and Windows will not start elevated programs from the
// HKCU Run key, so "Start on boot" is a logon scheduled task (highest privileges).
static bool CreateStartupTask() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t dom[256] = L"", usr[256] = L"";
    GetEnvironmentVariableW(L"USERDOMAIN", dom, 256);
    GetEnvironmentVariableW(L"USERNAME", usr, 256);
    std::wstring user = XmlEscape(std::wstring(dom) + L"\\" + usr);

    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + user + L"</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"Author\"><UserId>" + user + L"</UserId>"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Enabled>true</Enabled></Settings>"
        L"<Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exePath) + L"</Command></Exec></Actions>"
        L"</Task>";

    wchar_t tmpDir[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    GetTempFileNameW(tmpDir, L"tg", 0, tmpFile);
    HANDLE f = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const WORD bom = 0xFEFF;
    WriteFile(f, &bom, 2, &w, nullptr);
    WriteFile(f, xml.c_str(), (DWORD)(xml.size() * sizeof(wchar_t)), &w, nullptr);
    CloseHandle(f);

    int rc = RunHidden(L"schtasks.exe /Create /TN TrafficGate /XML \"" + std::wstring(tmpFile) + L"\" /F", true);
    DeleteFileW(tmpFile);
    return rc == 0;
}

void TrafficGateApp::RefreshStartupState() {
    startupEnabled = (RunHidden(L"schtasks.exe /Query /TN TrafficGate", true) == 0);
}

void TrafficGateApp::ToggleRunAtStartup() {
    if (startupEnabled) {
        RunHidden(L"schtasks.exe /Delete /TN TrafficGate /F", true);
    } else {
        if (!CreateStartupTask()) {
            MessageBoxW(hDaemonWnd, L"Could not create the startup task.", APP_NAME, MB_OK | MB_ICONWARNING);
        }
    }
    RefreshStartupState();
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

void TrafficGateApp::ResetTodayStats() {
    stats.todayInBytes = 0;
    stats.todayOutBytes = 0;
    SaveSettings();
}

void TrafficGateApp::SetSpeedLimit(uint64_t bps, bool enable, UINT inputVal, int inputUnit) {
    speedLimitEnabled = enable;
    speedLimitBps = enable ? bps : 0;
    speedLimitInputVal = inputVal;
    speedLimitInputUnit = inputUnit;
    SaveSettings();
    ApplySpeedLimit();
}

void TrafficGateApp::ApplySpeedLimit() {
    shaper.Stop();
    if (!speedLimitEnabled || speedLimitBps == 0) return;

    std::wstring err;
    if (!shaper.Start(speedLimitBps, hInst, err)) {
        speedLimitEnabled = false;
        speedLimitBps = 0;
        SaveSettings();
        MessageBoxW(hDaemonWnd, (L"The speed limit could not be applied.\n\n" + err).c_str(),
                    APP_NAME, MB_OK | MB_ICONWARNING);
    }
}

void TrafficGateApp::FormatSpeedComponents(uint64_t bytesPerSec, std::wstring& outNum, std::wstring& outUnit) {
    double bps = (double)bytesPerSec;
    wchar_t buf[32];
    if (bps >= 1024.0 * 1024.0 * 1024.0) {
        outUnit = L"GB/s";
        swprintf_s(buf, L"%.1f", bps / (1024.0 * 1024.0 * 1024.0));
    } else if (bps >= 1024.0 * 1024.0) {
        outUnit = L"MB/s";
        double mb = bps / (1024.0 * 1024.0);
        if (mb >= 10.0) swprintf_s(buf, L"%.0f", mb);
        else swprintf_s(buf, L"%.1f", mb);
    } else if (bps >= 1024.0) {
        outUnit = L"KB/s";
        swprintf_s(buf, L"%.0f", bps / 1024.0);
    } else {
        outUnit = L"B/s";
        swprintf_s(buf, L"%llu", bytesPerSec);
    }
    outNum = buf;
}

std::wstring TrafficGateApp::FormatSpeed(uint64_t bytesPerSec) {
    std::wstring num, unit;
    FormatSpeedComponents(bytesPerSec, num, unit);
    return num + L" " + unit;
}

std::wstring TrafficGateApp::FormatBytes(uint64_t bytes) {
    double b = (double)bytes;
    wchar_t buf[32];
    if (b >= 1024.0 * 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
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

    // While the limiter is running, measure what it actually delivers. Adapter
    // counters include packets it drops. Switching source needs a fresh baseline.
    bool useShaper = shaper.Running();
    if (useShaper) {
        totalIn = shaper.DeliveredInboundBytes();
        totalOut = shaper.DeliveredOutboundBytes();
    }
    if (useShaper != lastUsedShaper) {
        lastUsedShaper = useShaper;
        hasBaseline = false;
    }

    ULONGLONG now = GetTickCount64();

    if (!hasBaseline) {
        prevInOctets = totalIn;
        prevOutOctets = totalOut;
        prevTickMs = now;
        hasBaseline = true;
        return;
    }

    ULONGLONG elapsed = now - prevTickMs;
    if (elapsed < 100) return;

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
        SaveTodayHistory();
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

void TrafficGateApp::CreateTaskbarWindow() {
    // Created as a hidden top-level window; PositionTaskbarWindow() turns it into
    // a child of the taskbar (Shell_TrayWnd) so it lives inside the taskbar:
    // it hides with it (e.g. for full-screen video) and is never topmost.
    hTaskbarWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        APP_WNDCLASS_TASKBAR, L"TrafficGateTaskbar",
        WS_POPUP | WS_CLIPSIBLINGS,
        0, 0, 80, 40,
        nullptr, nullptr, hInst, nullptr);
}

void TrafficGateApp::PositionTaskbarWindow() {
    // Our window is destroyed together with its parent if Explorer restarts.
    if (!hTaskbarWnd || !IsWindow(hTaskbarWnd)) {
        CreateTaskbarWindow();
        if (!hTaskbarWnd) return;
    }

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray || !IsWindowVisible(hTray)) {
        ShowWindow(hTaskbarWnd, SW_HIDE);
        return;
    }

    if (GetParent(hTaskbarWnd) != hTray) {
        // Popup -> child: switch the styles first, then reparent (per SetParent docs).
        LONG_PTR style = GetWindowLongPtrW(hTaskbarWnd, GWL_STYLE);
        style = (style & ~(LONG_PTR)WS_POPUP) | WS_CHILD;
        SetWindowLongPtrW(hTaskbarWnd, GWL_STYLE, style);
        SetParent(hTaskbarWnd, hTray);
    }

    RECT rcTray = { 0 };
    GetWindowRect(hTray, &rcTray);

    HWND hNotify = FindWindowExW(hTray, nullptr, L"TrayNotifyWnd", nullptr);
    int rightBoundary = 0;

    if (hNotify && IsWindowVisible(hNotify)) {
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

    // Child windows are positioned in the parent's client coordinates.
    POINT pt = { x, y };
    ScreenToClient(hTray, &pt);
    SetWindowPos(hTaskbarWnd, HWND_TOP, pt.x, pt.y, width, height,
                 SWP_NOACTIVATE | (showTaskbar ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void TrafficGateApp::RenderTaskbarWindow() {
    if (!hTaskbarWnd || !showTaskbar || !IsWindow(hTaskbarWnd)) return;

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
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    memset(pBits, 0, width * height * 4);

    {
        // Hinted ClearType GDI text on whole-pixel positions, drawn white on black.
        // Each pixel keeps its ClearType colour channels as premultiplied colour and
        // uses the strongest channel as alpha, so on a dark taskbar the result is
        // the same weight and sharpness as ordinary ClearType text.
        int fontPx = MulDiv(9, taskbarDpi, 72); // 9 pt Segoe UI, as the taskbar clock
        HFONT hFont = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 255, 255));

        TEXTMETRICW tm;
        GetTextMetricsW(hdcMem, &tm);
        int lineH = tm.tmHeight;

        // Widest unit decides where the number column ends.
        int unitW = 0;
        {
            const wchar_t* samples[] = { L"B/s", L"KB/s", L"MB/s", L"GB/s" };
            for (const wchar_t* t : samples) {
                SIZE sz;
                GetTextExtentPoint32W(hdcMem, t, (int)wcslen(t), &sz);
                if (sz.cx > unitW) unitW = (int)sz.cx;
            }
        }
        int margin = MulDiv(2, taskbarDpi, 96);
        int gap = MulDiv(4, taskbarDpi, 96);
        int xUnitRight = width - margin;
        int xNumRight = xUnitRight - unitW - gap;
        // Arrow sits just left of the widest number that can occur ("1023"), so it
        // stays close to the digits instead of hugging the left edge.
        SIZE szNum, szArrow;
        GetTextExtentPoint32W(hdcMem, L"1023", 4, &szNum);
        GetTextExtentPoint32W(hdcMem, L"\x2191", 1, &szArrow);
        int xArrow = xNumRight - (int)szNum.cx - (int)szArrow.cx - MulDiv(2, taskbarDpi, 96);
        if (xArrow < margin) xArrow = margin;

        int top = (height - 2 * lineH) / 2;
        if (top < 0) top = 0;

        auto drawLine = [&](int y, const wchar_t* arrow, uint64_t bytesPerSec) {
            RECT ra = { xArrow, y, xArrow + 3 * fontPx, y + lineH };
            DrawTextW(hdcMem, arrow, -1, &ra, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            std::wstring num, unit;
            FormatSpeedComponents(bytesPerSec, num, unit);
            RECT rn = { 0, y, xNumRight, y + lineH };
            DrawTextW(hdcMem, num.c_str(), -1, &rn, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            RECT ru = { xNumRight, y, xUnitRight, y + lineH };
            DrawTextW(hdcMem, unit.c_str(), -1, &ru, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        };
        drawLine(top, L"\x2191", stats.currentUpBps);
        drawLine(top + lineH, L"\x2193", stats.currentDownBps);

        GdiFlush();
        SelectObject(hdcMem, hOldFont);
        DeleteObject(hFont);

        // ClearType channels -> premultiplied colour, alpha = strongest channel.
        uint32_t* px = (uint32_t*)pBits;
        for (int i = 0; i < width * height; ++i) {
            uint32_t v = px[i];
            uint32_t r = (v >> 16) & 0xFF, gch = (v >> 8) & 0xFF, bl = v & 0xFF;
            uint32_t a = r > gch ? r : gch;
            if (bl > a) a = bl;
            px[i] = (a << 24) | (r << 16) | (gch << 8) | bl;
        }
    }

    POINT ptSrc = { 0, 0 };
    SIZE wndSize = { width, height };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hTaskbarWnd, hdcScreen, nullptr, &wndSize, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void TrafficGateApp::ShowContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();

    std::wstring title = std::wstring(APP_NAME) + L" v" + APP_VERSION;
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER_VERSION, title.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDM_USAGE_HISTORY, L"Usage history...");
    AppendMenuW(hMenu, MF_STRING, IDM_SET_CUSTOM_LIMIT, L"Set custom limit...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING | (showTaskbar ? MF_CHECKED : MF_UNCHECKED), IDM_SHOW_TASKBAR, L"Show on Taskbar");
    AppendMenuW(hMenu, MF_STRING | (IsRunAtStartupEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_STARTUP_RUN, L"Start on boot");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

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

void TrafficGateApp::OpenUsageHistoryDialog() {
    DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_USAGE_HISTORY), hDaemonWnd, UsageHistoryDlgProc, (LPARAM)this);
}

INT_PTR CALLBACK TrafficGateApp::CustomLimitDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static TrafficGateApp* pApp = nullptr;

    switch (msg) {
        case WM_INITDIALOG: {
            pApp = (TrafficGateApp*)lParam;
            HWND hCombo = GetDlgItem(hDlg, IDC_LIMIT_UNIT);
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"KB/s");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"MB/s");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Mbps");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Gbps");

            if (pApp) {
                bool enabled = pApp->IsSpeedLimitEnabled();
                CheckDlgButton(hDlg, IDC_LIMIT_ENABLED, enabled ? BST_CHECKED : BST_UNCHECKED);
                SetDlgItemInt(hDlg, IDC_LIMIT_VALUE, pApp->GetSpeedLimitInputVal(), FALSE);
                SendMessageW(hCombo, CB_SETCURSEL, pApp->GetSpeedLimitInputUnit(), 0);

                EnableWindow(GetDlgItem(hDlg, IDC_LIMIT_VALUE), enabled);
                EnableWindow(hCombo, enabled);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_LIMIT_ENABLED && code == BN_CLICKED) {
                BOOL checked = (IsDlgButtonChecked(hDlg, IDC_LIMIT_ENABLED) == BST_CHECKED);
                EnableWindow(GetDlgItem(hDlg, IDC_LIMIT_VALUE), checked);
                EnableWindow(GetDlgItem(hDlg, IDC_LIMIT_UNIT), checked);
                return TRUE;
            }

            if (id == IDOK) {
                BOOL checked = (IsDlgButtonChecked(hDlg, IDC_LIMIT_ENABLED) == BST_CHECKED);
                BOOL translated = FALSE;
                UINT val = GetDlgItemInt(hDlg, IDC_LIMIT_VALUE, &translated, FALSE);
                if (!translated || val == 0) val = 10;

                HWND hCombo = GetDlgItem(hDlg, IDC_LIMIT_UNIT);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel < 0 || sel > 3) sel = 1;

                if (pApp) {
                    if (checked) {
                        uint64_t bps = 0;
                        switch (sel) {
                            case 0: // KB/s
                                bps = (uint64_t)val * 1024ULL * 8ULL;
                                break;
                            case 1: // MB/s
                                bps = (uint64_t)val * 1024ULL * 1024ULL * 8ULL;
                                break;
                            case 2: // Mbps
                                bps = (uint64_t)val * 1000000ULL;
                                break;
                            case 3: // Gbps
                                bps = (uint64_t)val * 1000000000ULL;
                                break;
                        }
                        pApp->SetSpeedLimit(bps, true, val, sel);
                    } else {
                        pApp->SetSpeedLimit(0, false, val, sel);
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

INT_PTR CALLBACK TrafficGateApp::UsageHistoryDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static TrafficGateApp* pApp = nullptr;

    switch (msg) {
        case WM_INITDIALOG: {
            pApp = (TrafficGateApp*)lParam;
            HWND hList = GetDlgItem(hDlg, IDC_HISTORY_LIST);
            SendMessageW(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            RECT lrc;
            GetClientRect(hList, &lrc);
            int total = (lrc.right - lrc.left);
            int wDate = total * 28 / 100;
            int wMid = total * 24 / 100;
            int wLast = total - wDate - 2 * wMid;

            LVCOLUMNW lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            lvc.iSubItem = 0;
            lvc.pszText = (LPWSTR)L"Date";
            lvc.cx = wDate;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

            lvc.iSubItem = 1;
            lvc.pszText = (LPWSTR)L"Download";
            lvc.cx = wMid;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

            lvc.iSubItem = 2;
            lvc.pszText = (LPWSTR)L"Upload";
            lvc.cx = wMid;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

            lvc.iSubItem = 3;
            lvc.pszText = (LPWSTR)L"Total";
            lvc.cx = wLast;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

            if (pApp) {
                auto history = pApp->GetUsageHistory();
                int idx = 0;
                for (const auto& rec : history) {
                    LVITEMW lvi = { 0 };
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = idx;
                    lvi.iSubItem = 0;
                    lvi.pszText = (LPWSTR)rec.date.c_str();
                    SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

                    std::wstring downStr = FormatBytes(rec.downBytes);
                    ListView_SetItemText(hList, idx, 1, (LPWSTR)downStr.c_str());

                    std::wstring upStr = FormatBytes(rec.upBytes);
                    ListView_SetItemText(hList, idx, 2, (LPWSTR)upStr.c_str());

                    std::wstring totStr = FormatBytes(rec.downBytes + rec.upBytes);
                    ListView_SetItemText(hList, idx, 3, (LPWSTR)totStr.c_str());

                    idx++;
                }
            }
            ListView_SetColumnWidth(hList, 3, LVSCW_AUTOSIZE_USEHEADER);
            return TRUE;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_HISTORY_RESET) {
                if (MessageBoxW(hDlg, L"Clear all recorded internet usage history?", L"TrafficGate", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (pApp) {
                        pApp->ClearUsageHistory();
                    }
                    HWND hList = GetDlgItem(hDlg, IDC_HISTORY_LIST);
                    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);
                    ListView_SetColumnWidth(hList, 3, LVSCW_AUTOSIZE_USEHEADER);
                }
                return TRUE;
            } else if (id == IDOK || id == IDCANCEL) {
                EndDialog(hDlg, IDOK);
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
    wcsncpy_s(nid.szTip, L"TrafficGate v1.2", _TRUNCATE);
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
                case IDM_USAGE_HISTORY:
                    app.OpenUsageHistoryDialog();
                    break;

                case IDM_SET_CUSTOM_LIMIT:
                    app.OpenCustomLimitDialog();
                    break;

                case IDM_SHOW_TASKBAR:
                    app.ToggleTaskbarVisible();
                    break;

                case IDM_STARTUP_RUN:
                    app.ToggleRunAtStartup();
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
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES;
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

    CreateTaskbarWindow();

    LoadSettings();
    InitTrayIcon();

    // One-time migration from v1.2: the old Windows QoS policy only throttled
    // uploads; remove it so it does not stack with the new limiter.
    {
        std::wstring ini = GetSettingsFilePath();
        if (GetPrivateProfileIntW(L"TrafficGate", L"LegacyQosRemoved", 0, ini.c_str()) == 0) {
            RunHidden(L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command "
                      L"\"Remove-NetQosPolicy -Name 'TrafficGate_Limit' -Confirm:$false -ErrorAction SilentlyContinue\"", false);
            WritePrivateProfileStringW(L"TrafficGate", L"LegacyQosRemoved", L"1", ini.c_str());
        }
    }

    // Migrate an old HKCU Run entry (it cannot launch an elevated app) to the task.
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hKey, L"TrafficGate", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                CreateStartupTask();
                RegDeleteValueW(hKey, L"TrafficGate");
            }
            RegCloseKey(hKey);
        }
    }
    RefreshStartupState();

    ApplySpeedLimit();

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
    shaper.Stop();
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
