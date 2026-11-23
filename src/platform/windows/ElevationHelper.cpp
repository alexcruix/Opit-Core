// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "ElevationHelper.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <sstream>

namespace opitcore {

bool ElevationHelper::IsElevated() {
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation{};
        DWORD dwSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return (isElevated != FALSE);
}

std::string ElevationHelper::GetOsVersionString() {
    // Ler versão via Registry para contornar shims do GetVersionEx
    HKEY hKey = nullptr;
    std::string productName = "Windows";
    std::string displayVersion = "";
    std::string currentBuild = "";

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR buf[256] = {0};
        DWORD sz = sizeof(buf);

        if (RegQueryValueExW(hKey, L"ProductName", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            productName = StringUtils::ToUtf8(buf);
        }

        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"DisplayVersion", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            displayVersion = StringUtils::ToUtf8(buf);
        }

        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"CurrentBuild", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            currentBuild = StringUtils::ToUtf8(buf);
        }

        RegCloseKey(hKey);
    }

    std::ostringstream ss;
    ss << productName;
    if (!displayVersion.empty()) ss << " (" << displayVersion << ")";
    if (!currentBuild.empty()) ss << " Build " << currentBuild;
    return ss.str();
}

bool ElevationHelper::Is64BitOs() {
#if defined(_WIN64)
    return true;
#elif defined(_WIN32)
    BOOL isWow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &isWow64);
    return (isWow64 != FALSE);
#else
    return false;
#endif
}

bool ElevationHelper::SetRunAtStartup(bool enable) {
    HKEY hKey = nullptr;
    const wchar_t* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* valName = L"OpitCore";
    bool success = false;
    if (enable) {
        WCHAR exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
        LSTATUS status = RegSetValueExW(hKey, valName, 0, REG_SZ, 
                                        reinterpret_cast<const BYTE*>(cmd.c_str()), 
                                        static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
        success = (status == ERROR_SUCCESS);
    } else {
        LSTATUS status = RegDeleteValueW(hKey, valName);
        success = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(hKey);
    return success;
}

bool ElevationHelper::IsRunAtStartup() {
    HKEY hKey = nullptr;
    const wchar_t* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* valName = L"OpitCore";
    DWORD type = 0;
    DWORD sz = 0;
    LSTATUS status = RegQueryValueExW(hKey, valName, nullptr, &type, nullptr, &sz);
    RegCloseKey(hKey);
    return (status == ERROR_SUCCESS);
}

bool ElevationHelper::SetRunAtStartupAdmin(bool enable) {
    WCHAR exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    const wchar_t* taskName = L"OpitCoreAdminStartup";
    std::wstring cmd;
    if (enable) {
        cmd = L"schtasks.exe /create /tn \"" + std::wstring(taskName) + 
              L"\" /tr \"\\\"" + std::wstring(exePath) + L"\\\" --minimized\" /sc onlogon /rl highest /f";
    } else {
        cmd = L"schtasks.exe /delete /tn \"" + std::wstring(taskName) + L"\" /f";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (enable && exitCode == 0) {
        SetRunAtStartup(false);
    }
    return (exitCode == 0);
}

bool ElevationHelper::IsRunAtStartupAdmin() {
    const wchar_t* taskName = L"OpitCoreAdminStartup";
    std::wstring cmd = L"schtasks.exe /query /tn \"" + std::wstring(taskName) + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 4000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

bool ElevationHelper::ApplySvchostGrouping(int groupingMode) {
    HKEY hKey = nullptr;
    const wchar_t* subKey = L"SYSTEM\\CurrentControlSet\\Control";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false; // Requer privilégios administrativos
    }

    const wchar_t* valName = L"SvcHostSplitThresholdInKB";
    DWORD val = 0x380000; // 3.5 GB (Mínimo / isolamento total de processos)
    if (groupingMode == 1) {
        // Padrão do Windows
        RegDeleteValueW(hKey, valName);
        RegCloseKey(hKey);
        return true;
    } else if (groupingMode == 2) {
        val = 0xFFFFFFFF; // Máximo agrupamento
    }

    LSTATUS st = RegSetValueExW(hKey, valName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS);
}

bool ElevationHelper::UnlockCoreParkingInRegistry() {
    const wchar_t* subKeys[] = {
        L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings\\54533251-82be-4824-96c1-47b60b740d00\\0cc5b647-c1df-4637-891a-dec35c318583",
        L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings\\54533251-82be-4824-96c1-47b60b740d00\\0cc5b647-c1df-4637-891a-dec35c318584",
        L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings\\54533251-82be-4824-96c1-47b60b740d00\\ea062031-0e34-4ff1-9b6d-eb1059334028",
        L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings\\54533251-82be-4824-96c1-47b60b740d00\\ea062031-0e34-4ff1-9b6d-eb1059334029"
    };

    DWORD attrVal = 0; // 0 = Visivel e totalmente desbloqueado no kernel/powrprof
    bool anyWritten = false;
    for (const auto* k : subKeys) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            if (RegSetValueExW(hKey, L"Attributes", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&attrVal), sizeof(attrVal)) == ERROR_SUCCESS) {
                anyWritten = true;
            }
            RegCloseKey(hKey);
        }
    }
    // Sem elevacao nenhuma chave abre: reportar false em vez de sucesso ficticio.
    return anyWritten;
}

} // namespace opitcore
