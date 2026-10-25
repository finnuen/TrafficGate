#pragma once
// Total (upload + download) bandwidth limiter built on WinDivert.
// Every non-loopback IP packet is diverted into a queue and re-injected by a
// token bucket at the configured rate. If the app exits or crashes the handle
// closes and traffic flows normally again.
#include <windows.h>
#include <shlobj.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include "resource.h"
#include <algorithm>
#include <sstream>
#include "third_party/windivert.h"
// windivert.h defines empty SAL-style macros that collide with libstdc++ internals.
#undef __in
#undef __in_opt
#undef __out
#undef __out_opt
#undef __inout
#undef __inout_opt

class TrafficShaper {
public:
    ~TrafficShaper() { Stop(); }

    bool Running() const { return hDivert != INVALID_HANDLE_VALUE; }

    // Cumulative bytes actually delivered (re-injected) per direction. While the
    // limiter runs, the speed indicator uses these instead of adapter counters,
    // which also count packets the limiter drops and can double-count traffic
    // that crosses several (virtual) adapters.
    uint64_t DeliveredInboundBytes() const { return (uint64_t)deliveredInBytes; }
    uint64_t DeliveredOutboundBytes() const { return (uint64_t)deliveredOutBytes; }

    bool Start(uint64_t bitsPerSec, HINSTANCE hInst, std::wstring& err) {
        Stop();
        if (bitsPerSec == 0) { err = L"Invalid rate."; return false; }
        if (!LoadDriver(hInst, err)) return false;

        hDivert = pOpen("!loopback", WINDIVERT_LAYER_NETWORK, 0, 0);
        if (hDivert == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            err = L"WinDivertOpen failed (error " + std::to_wstring(e) + L"). ";
            if (e == 577) err += L"Windows blocked the driver signature.";
            else if (e == 5) err += L"Administrator rights are required.";
            else if (e == 1275) err += L"The driver was blocked (security software or Memory Integrity).";
            return false;
        }

        bytesPerSec = (double)bitsPerSec / 8.0;
        maxQueueBytes = (size_t)(bytesPerSec * 0.3);
        if (maxQueueBytes < 131072) maxQueueBytes = 131072;
        queuedBytes = 0;
        running = true;
        InitializeCriticalSection(&cs);
        InitializeConditionVariable(&cv);
        csInit = true;
        hRecv = CreateThread(nullptr, 0, RecvThread, this, 0, nullptr);
        hSend = CreateThread(nullptr, 0, SendThread, this, 0, nullptr);
        if (!hRecv || !hSend) { err = L"Could not start worker threads."; Stop(); return false; }
        return true;
    }

    void Stop() {
        if (hDivert == INVALID_HANDLE_VALUE) return;
        running = false;
        pShutdown(hDivert, WINDIVERT_SHUTDOWN_BOTH);
        if (csInit) WakeAllConditionVariable(&cv);
        if (hRecv) { WaitForSingleObject(hRecv, 5000); CloseHandle(hRecv); hRecv = nullptr; }
        if (hSend) { WaitForSingleObject(hSend, 5000); CloseHandle(hSend); hSend = nullptr; }
        pClose(hDivert);
        hDivert = INVALID_HANDLE_VALUE;
        if (csInit) { DeleteCriticalSection(&cs); csInit = false; }
        queue.clear();
        queuedBytes = 0;
    }

private:
    struct Packet {
        std::vector<unsigned char> data;
        WINDIVERT_ADDRESS addr;
    };

    typedef HANDLE (WINAPI *PFN_Open)(const char*, WINDIVERT_LAYER, INT16, UINT64);
    typedef BOOL (WINAPI *PFN_Recv)(HANDLE, PVOID, UINT, UINT*, WINDIVERT_ADDRESS*);
    typedef BOOL (WINAPI *PFN_Send)(HANDLE, const VOID*, UINT, UINT*, const WINDIVERT_ADDRESS*);
    typedef BOOL (WINAPI *PFN_Shutdown)(HANDLE, WINDIVERT_SHUTDOWN);
    typedef BOOL (WINAPI *PFN_Close)(HANDLE);

    HANDLE hDivert = INVALID_HANDLE_VALUE;
    HMODULE hDll = nullptr;
    PFN_Open pOpen = nullptr;
    PFN_Recv pRecv = nullptr;
    PFN_Send pSend = nullptr;
    PFN_Shutdown pShutdown = nullptr;
    PFN_Close pClose = nullptr;

    HANDLE hRecv = nullptr, hSend = nullptr;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    bool csInit = false;
    volatile bool running = false;
    std::deque<Packet> queue;
    size_t queuedBytes = 0;
    size_t maxQueueBytes = 0;
    volatile LONG64 deliveredInBytes = 0;
    volatile LONG64 deliveredOutBytes = 0;
    double bytesPerSec = 0;

    static bool WriteResourceFile(HINSTANCE hInst, int id, const std::wstring& path) {
        HRSRC r = FindResourceW(hInst, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!r) return false;
        HGLOBAL g = LoadResource(hInst, r);
        DWORD size = SizeofResource(hInst, r);
        const void* p = g ? LockResource(g) : nullptr;
        if (!p || !size) return false;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) &&
            fad.nFileSizeHigh == 0 && fad.nFileSizeLow == size) {
            return true; // already extracted (and possibly loaded)
        }
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(f, p, size, &written, nullptr);
        CloseHandle(f);
        return ok && written == size;
    }

    bool LoadDriver(HINSTANCE hInst, std::wstring& err) {
        if (hDll) return true;
        // Program Files is writable by administrators only, so an elevated
        // process never loads a DLL that an unprivileged user could swap.
        wchar_t pf[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf))) {
            err = L"Could not locate Program Files.";
            return false;
        }
        std::wstring dir = std::wstring(pf) + L"\\TrafficGate";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring dll = dir + L"\\WinDivert.dll";
        std::wstring sys = dir + L"\\WinDivert64.sys";
        if (!WriteResourceFile(hInst, IDR_WINDIVERT_DLL, dll) ||
            !WriteResourceFile(hInst, IDR_WINDIVERT_SYS, sys)) {
            err = L"Could not extract the WinDivert driver to " + dir + L".";
            return false;
        }
        hDll = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!hDll) { err = L"Could not load WinDivert.dll."; return false; }
        pOpen = (PFN_Open)GetProcAddress(hDll, "WinDivertOpen");
        pRecv = (PFN_Recv)GetProcAddress(hDll, "WinDivertRecv");
        pSend = (PFN_Send)GetProcAddress(hDll, "WinDivertSend");
        pShutdown = (PFN_Shutdown)GetProcAddress(hDll, "WinDivertShutdown");
        pClose = (PFN_Close)GetProcAddress(hDll, "WinDivertClose");
        if (!pOpen || !pRecv || !pSend || !pShutdown || !pClose) {
            err = L"WinDivert.dll is missing expected functions.";
            FreeLibrary(hDll); hDll = nullptr;
            return false;
        }
        return true;
    }

    static DWORD WINAPI RecvThread(LPVOID param) {
        TrafficShaper* s = (TrafficShaper*)param;
        std::vector<unsigned char> buf(WINDIVERT_MTU_MAX + 64);
        while (s->running) {
            UINT len = 0;
            WINDIVERT_ADDRESS addr;
            if (!s->pRecv(s->hDivert, buf.data(), (UINT)buf.size(), &len, &addr)) {
                DWORD e = GetLastError();
                if (e == ERROR_NO_DATA || e == ERROR_INVALID_HANDLE || !s->running) break;
                continue;
            }
            EnterCriticalSection(&s->cs);
            if (s->queuedBytes + len <= s->maxQueueBytes) {
                Packet p;
                p.data.assign(buf.begin(), buf.begin() + len);
                p.addr = addr;
                s->queue.push_back(std::move(p));
                s->queuedBytes += len;
                WakeConditionVariable(&s->cv);
            } // else: drop the packet; TCP backs off, which is how the cap is enforced
            LeaveCriticalSection(&s->cs);
        }
        return 0;
    }

    static DWORD WINAPI SendThread(LPVOID param) {
        TrafficShaper* s = (TrafficShaper*)param;
        LARGE_INTEGER freq, last, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
        double tokens = 0;
        const double burst = (s->bytesPerSec / 10.0 > 16384.0) ? s->bytesPerSec / 10.0 : 16384.0;

        while (s->running) {
            EnterCriticalSection(&s->cs);
            while (s->queue.empty() && s->running) {
                SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
            }
            if (!s->running) { LeaveCriticalSection(&s->cs); break; }
            Packet p = std::move(s->queue.front());
            s->queue.pop_front();
            s->queuedBytes -= p.data.size();
            LeaveCriticalSection(&s->cs);

            // Refill, then wait while in debt. A packet may overdraw the bucket,
            // so oversized packets can never deadlock.
            for (;;) {
                QueryPerformanceCounter(&now);
                tokens += (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart * s->bytesPerSec;
                last = now;
                if (tokens > burst) tokens = burst;
                if (tokens > 0 || !s->running) break;
                DWORD ms = (DWORD)(-tokens / s->bytesPerSec * 1000.0) + 1;
                Sleep(ms > 50 ? 50 : ms);
            }
            tokens -= (double)p.data.size();
            UINT sent = 0;
            if (s->pSend(s->hDivert, p.data.data(), (UINT)p.data.size(), &sent, &p.addr)) {
                InterlockedExchangeAdd64(p.addr.Outbound ? &s->deliveredOutBytes : &s->deliveredInBytes,
                                         (LONG64)p.data.size());
            }
        }
        return 0;
    }
};
