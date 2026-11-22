#include "FileUtils.h"
#include "StringUtils.h"
#include <windows.h>
#include <shlobj.h>
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
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        std::string s = StringUtils::ToUtf8(path);
        s += "\\OpitCore";
        EnsureDirectory(s);
        return s;
    }
    return ".";
}

std::string FileUtils::GetExecutableDirectory() {
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
}

std::string FileUtils::CombinePath(const std::string& dir, const std::string& filename) {
    if (dir.empty()) return filename;
    char last = dir.back();
    if (last == '\\' || last == '/') {
        return dir + filename;
    }
    return dir + "\\" + filename;
}

} // namespace opitcore
