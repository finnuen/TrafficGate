#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#undef WINVER
#define WINVER 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <cstdint>

#include "resource.h"

// Version definition
#define APP_NAME            L"TrafficGate"
#define APP_VERSION         L"1.2"
#define APP_VERSION_STR     "1.2"
#define APP_MUTEX_NAME      L"Global\\TrafficGate_SingleInstance_Mutex"
#define APP_WNDCLASS_DAEMON L"TrafficGate_DaemonWnd"
#define APP_WNDCLASS_TASKBAR L"TrafficGate_TaskbarWnd"

#define WM_TRAYICON         (WM_USER + 101)
#define IDT_TIMER_POLL      2001
#define IDT_TIMER_WATCH     2002

struct NetworkUsageStats {
    uint64_t currentUpBps = 0;
    uint64_t currentDownBps = 0;
    uint64_t sessionInBytes = 0;
    uint64_t sessionOutBytes = 0;
    uint64_t todayInBytes = 0;
    uint64_t todayOutBytes = 0;
    std::wstring todayDateStr;
};

struct UsageRecord {
    std::wstring date;
    uint64_t downBytes = 0;
    uint64_t upBytes = 0;
};

class TrafficGateApp {
public:
    static TrafficGateApp& Instance();

    bool Initialize(HINSTANCE hInstance);
    int Run();
    void Shutdown();

    // Settings & History
    void LoadSettings();
    void SaveSettings();
    std::vector<UsageRecord> GetUsageHistory();
    void ClearUsageHistory();
    void SaveTodayHistory();

    // Network stats
    void UpdateNetworkStats();
    void ResetTodayStats();

    // Speed Limiter
    void SetSpeedLimit(uint64_t bps, bool enable, UINT inputVal = 10, int inputUnit = 1);
    bool IsSpeedLimitEnabled() const { return speedLimitEnabled; }
    uint64_t GetSpeedLimitBps() const { return speedLimitBps; }
    UINT GetSpeedLimitInputVal() const { return speedLimitInputVal; }
    int GetSpeedLimitInputUnit() const { return speedLimitInputUnit; }

    // Startup run
    bool IsRunAtStartupEnabled() const;
    void ToggleRunAtStartup();

    // Taskbar display toggle
    bool IsTaskbarVisible() const { return showTaskbar; }
    void ToggleTaskbarVisible();

    // UI & Windows
    void PositionTaskbarWindow();
    void RenderTaskbarWindow();
    void ShowContextMenu(POINT pt);
    void OpenCustomLimitDialog();
    void OpenUsageHistoryDialog();

    // Formatting helpers
    static void FormatSpeedComponents(uint64_t bytesPerSec, std::wstring& outNum, std::wstring& outUnit);
    static std::wstring FormatSpeed(uint64_t bytesPerSec);
    static std::wstring FormatBytes(uint64_t bytes);

private:
    TrafficGateApp() = default;
    ~TrafficGateApp() = default;

    static LRESULT CALLBACK DaemonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TaskbarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK CustomLimitDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK UsageHistoryDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    std::wstring GetSettingsFilePath() const;
    std::wstring GetCurrentDateString() const;
    void InitTrayIcon();
    void UpdateTrayTooltip();
    void RemoveTrayIcon();

    HINSTANCE hInst = nullptr;
    HWND hDaemonWnd = nullptr;
    HWND hTaskbarWnd = nullptr;
    HANDLE hMutex = nullptr;
    NOTIFYICONDATAW nid = { 0 };
    HICON hAppIcon = nullptr;
    ULONG_PTR gdiplusToken = 0;
    UINT wmTaskbarCreated = 0;

    // Network tracking state
    NetworkUsageStats stats;
    uint64_t prevInOctets = 0;
    uint64_t prevOutOctets = 0;
    ULONGLONG prevTickMs = 0;
    bool hasBaseline = false;

    // Config options
    bool showTaskbar = true;
    bool speedLimitEnabled = false;
    uint64_t speedLimitBps = 0; // 0 = unlimited
    UINT speedLimitInputVal = 10;
    int speedLimitInputUnit = 1; // 0=KB/s, 1=MB/s, 2=Mbps, 3=Gbps

    // Taskbar window state
    RECT lastTaskbarRect = { 0 };
    int taskbarDpi = 96;
};
