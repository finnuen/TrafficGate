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

    std::wstring cmd;
    if (enable && bps > 0) {
        cmd = L"-WindowStyle Hidden -Command \"Remove-NetQosPolicy -Name 'TrafficGate_Limit' -Confirm:$false -ErrorAction SilentlyContinue; New-NetQosPolicy -Name 'TrafficGate_Limit' -ThrottleRateActionBitsPerSecond " + std::to_wstring(bps) + L"\"";
    } else {
        cmd = L"-WindowStyle Hidden -Command \"Remove-NetQosPolicy -Name 'TrafficGate_Limit' -Confirm:$false -ErrorAction SilentlyContinue\"";
    }

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = cmd.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    ShellExecuteExW(&sei);
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

void TrafficGateApp::PositionTaskbarWindow() {
    if (!hTaskbarWnd) return;

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray || !IsWindowVisible(hTray)) {
        ShowWindow(hTaskbarWnd, SW_HIDE);
        return;
    }

    HWND curOwner = GetWindow(hTaskbarWnd, GW_OWNER);
    if (curOwner != hTray) {
        SetWindowLongPtrW(hTaskbarWnd, GWLP_HWNDPARENT, (LONG_PTR)hTray);
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
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    memset(pBits, 0, width * height * 4);

    {
        Graphics g(hdcMem);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        float dpiScale = (float)taskbarDpi / 96.0f;
        // Same bold weight and size as taskbar date and time clock
        float fontSize = 11.5f * dpiScale;

        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, fontSize, FontStyleBold, UnitPixel);

        // All elements in crisp white matching date and time
        SolidBrush whiteBrush(Color(255, 255, 255, 255));

        // Fixed column positions:
        // Column 1: Static arrows (xArrow)
        // Column 2: Speed number right-aligned to (xNumRight)
        // Column 3: Static unit (xUnit)
        float xArrow = 2.0f * dpiScale;
        float xNumRight = 46.0f * dpiScale;
        float xUnit = 49.0f * dpiScale;

        float line1Y = (float)height * 0.10f;
        float line2Y = (float)height * 0.52f;

        // Line 1: Upload (↑    <num> <unit>)
        PointF ptArrow1(xArrow, line1Y);
        g.DrawString(L"\x2191", -1, &font, ptArrow1, &whiteBrush);

        std::wstring upNum, upUnit;
        FormatSpeedComponents(stats.currentUpBps, upNum, upUnit);

        RectF rcNum1;
        g.MeasureString(upNum.c_str(), -1, &font, PointF(0, 0), &rcNum1);
        PointF ptNum1(xNumRight - rcNum1.Width, line1Y);
        g.DrawString(upNum.c_str(), -1, &font, ptNum1, &whiteBrush);

        PointF ptUnit1(xUnit, line1Y);
        g.DrawString(upUnit.c_str(), -1, &font, ptUnit1, &whiteBrush);

        // Line 2: Download (↓    <num> <unit>)
        PointF ptArrow2(xArrow, line2Y);
        g.DrawString(L"\x2193", -1, &font, ptArrow2, &whiteBrush);

        std::wstring downNum, downUnit;
        FormatSpeedComponents(stats.currentDownBps, downNum, downUnit);

        RectF rcNum2;
        g.MeasureString(downNum.c_str(), -1, &font, PointF(0, 0), &rcNum2);
        PointF ptNum2(xNumRight - rcNum2.Width, line2Y);
        g.DrawString(downNum.c_str(), -1, &font, ptNum2, &whiteBrush);

        PointF ptUnit2(xUnit, line2Y);
        g.DrawString(downUnit.c_str(), -1, &font, ptUnit2, &whiteBrush);
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

            LVCOLUMNW lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            lvc.iSubItem = 0;
            lvc.pszText = (LPWSTR)L"Date";
            lvc.cx = 120;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

            lvc.iSubItem = 1;
            lvc.pszText = (LPWSTR)L"Download";
            lvc.cx = 110;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

            lvc.iSubItem = 2;
            lvc.pszText = (LPWSTR)L"Upload";
            lvc.cx = 110;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

            lvc.iSubItem = 3;
            lvc.pszText = (LPWSTR)L"Total";
            lvc.cx = 110;
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
