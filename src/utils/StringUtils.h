#pragma once
#include <string>
#include <vector>

namespace opitcore {

class StringUtils {
public:
    static std::string ToLower(const std::string& str);
    static std::string Trim(const std::string& str);
    static std::vector<std::string> Split(const std::string& str, char delimiter);
    static std::wstring ToWide(const std::string& utf8Str);
    static std::string ToUtf8(const std::wstring& wideStr);
    static std::string FormatBytes(uint64_t bytes);
};

} // namespace opitcore
