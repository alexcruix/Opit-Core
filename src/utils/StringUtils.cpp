#include "StringUtils.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace opitcore {

std::string StringUtils::ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return result;
}

std::string StringUtils::Trim(const std::string& str) {
    const auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    const auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> StringUtils::Split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::wstring StringUtils::ToWide(const std::string& utf8Str) {
    if (utf8Str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), nullptr, 0);
    if (req <= 0) return L"";
    std::wstring out(req, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), &out[0], req);
    return out;
}

std::string StringUtils::ToUtf8(const std::wstring& wideStr) {
    if (wideStr.empty()) return "";
    int req = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), nullptr, 0, nullptr, nullptr);
    if (req <= 0) return "";
    std::string out(req, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), &out[0], req, nullptr, nullptr);
    return out;
}

std::string StringUtils::FormatBytes(uint64_t bytes) {
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double tb = gb * 1024.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (bytes >= tb) {
        ss << (bytes / tb) << " TB";
    } else if (bytes >= gb) {
        ss << (bytes / gb) << " GB";
    } else if (bytes >= mb) {
        ss << (bytes / mb) << " MB";
    } else if (bytes >= kb) {
        ss << (bytes / kb) << " KB";
    } else {
        ss << bytes << " B";
    }
    return ss.str();
}

} // namespace opitcore
