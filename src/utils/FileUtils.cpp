// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "FileUtils.h"
#include "StringUtils.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif
#include <filesystem>

namespace opitcore {

bool FileUtils::EnsureDirectory(const std::string& path) {
    try {
        if (path.empty()) return true;
        std::filesystem::create_directories(path);
        return true;
    } catch (...) {
        return false;
    }
}

bool FileUtils::FileExists(const std::string& path) {
    try {
        return std::filesystem::exists(path);
    } catch (...) {
        return false;
    }
}

std::string FileUtils::GetAppDataDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        std::string s = StringUtils::ToUtf8(path);
        s += "\\OpitCore";
        EnsureDirectory(s);
        return s;
    }
    return ".";
#else
    // Gemelo do %LOCALAPPDATA% no modelo XDG: XDG_DATA_HOME, ou ~/.local/share quando
    // nao esta definido. A pasta propria do programa mantem-se o nome OpitCore.
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base = ".";
    if (xdg != nullptr && *xdg != '\0') {
        base = xdg;
    } else if (home != nullptr && *home != '\0') {
        base = std::string(home) + "/.local/share";
    }
    std::string s = base + "/OpitCore";
    EnsureDirectory(s);
    return s;
#endif
}

std::string FileUtils::GetExecutableDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
        std::wstring ws(path);
        size_t pos = ws.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            ws = ws.substr(0, pos);
            return StringUtils::ToUtf8(ws);
        }
    }
    return ".";
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::filesystem::path p(buf);
        if (p.has_parent_path()) return p.parent_path().string();
    }
    return ".";
#endif
}

std::string FileUtils::CombinePath(const std::string& dir, const std::string& filename) {
    if (dir.empty()) return filename;
    char last = dir.back();
    if (last == '\\' || last == '/') {
        return dir + filename;
    }
#ifdef _WIN32
    return dir + "\\" + filename;
#else
    return dir + "/" + filename;
#endif
}

} // namespace opitcore
