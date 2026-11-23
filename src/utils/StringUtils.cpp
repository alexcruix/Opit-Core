// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "StringUtils.h"
#ifdef _WIN32
#include <windows.h>
#endif
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
#ifdef _WIN32
    if (utf8Str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), nullptr, 0);
    if (req <= 0) return L"";
    std::wstring out(req, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.length(), &out[0], req);
    return out;
#else
    // wchar_t tem 4 bytes fora do Windows: cada elemento e um ponto de codigo, nao uma
    // unidade UTF-16. Todo o texto do programa esta no plano basico, por isso a diferenca
    // de representacao so se veria em caracteres astrais.
    std::wstring out;
    out.reserve(utf8Str.size());
    for (size_t i = 0; i < utf8Str.size();) {
        unsigned char lead = static_cast<unsigned char>(utf8Str[i]);
        uint32_t cp = lead;
        size_t extra = 0;
        if (lead < 0x80) {
            ++i;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1Fu; extra = 1; ++i;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0Fu; extra = 2; ++i;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07u; extra = 3; ++i;
        } else {
            ++i;
            continue;
        }
        if (utf8Str.size() - i < extra) break;
        bool ok = true;
        for (size_t k = 0; k < extra; ++k) {
            unsigned char cont = static_cast<unsigned char>(utf8Str[i + k]);
            if ((cont & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cont & 0x3Fu);
        }
        if (!ok) continue;
        i += extra;
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
#endif
}

std::string StringUtils::ToUtf8(const std::wstring& wideStr) {
#ifdef _WIN32
    if (wideStr.empty()) return "";
    int req = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), nullptr, 0, nullptr, nullptr);
    if (req <= 0) return "";
    std::string out(req, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), (int)wideStr.length(), &out[0], req, nullptr, nullptr);
    return out;
#else
    std::string out;
    out.reserve(wideStr.size());
    for (wchar_t w : wideStr) {
        uint32_t cp = static_cast<uint32_t>(w);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
#endif
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
