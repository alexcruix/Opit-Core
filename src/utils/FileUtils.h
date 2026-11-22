#pragma once
#include <string>
#include <filesystem>

namespace opitcore {

class FileUtils {
public:
    static bool EnsureDirectory(const std::string& path);
    static bool FileExists(const std::string& path);
    static std::string GetAppDataDirectory();
    static std::string GetExecutableDirectory();
    static std::string CombinePath(const std::string& dir, const std::string& filename);
};

} // namespace opitcore
