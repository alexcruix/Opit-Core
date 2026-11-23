// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include <deque>

namespace opitcore {

enum class LogLevel { Debug, Info, Warning, Error, Critical };

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void Log(LogLevel level, const std::string& message) = 0;
    virtual void Log(LogLevel level, const std::string& category, const std::string& message, const std::string& metaJson = "") = 0;
    virtual void SetLevel(LogLevel level) = 0;
};

class Logger : public ILogger {
public:
    static Logger& Instance();
    void Log(LogLevel level, const std::string& message) override;
    void Log(LogLevel level, const std::string& category, const std::string& message, const std::string& metaJson = "") override;
    void SetLevel(LogLevel level) override;
    bool Init(const std::string& logFile);
    std::vector<std::string> GetRecentLogs(size_t maxCount = 100);

private:
    Logger() = default;
    void RotateIfNeededLocked();
    std::mutex m_mutex;
    std::ofstream m_file;
    std::string m_logPath;
    uint64_t m_bytesWritten = 0;
    static constexpr uint64_t k_max_log_bytes = 5ull * 1024 * 1024;
    LogLevel m_level = LogLevel::Info;
    std::deque<std::string> m_recentLogs;
};

#define OPIT_LOG_CAT(lvl, cat, msg) opitcore::Logger::Instance().Log(lvl, cat, msg)
#define OPIT_LOG_POWER(lvl, msg)    opitcore::Logger::Instance().Log(lvl, "power", msg)
#define OPIT_LOG_DISK(lvl, msg)     opitcore::Logger::Instance().Log(lvl, "disk", msg)
#define OPIT_LOG_ADAPTIVE(lvl, msg) opitcore::Logger::Instance().Log(lvl, "adaptive", msg)

#define OPIT_LOG_DEBUG(msg) opitcore::Logger::Instance().Log(opitcore::LogLevel::Debug, "runtime", msg)
#define OPIT_LOG_INFO(msg)  opitcore::Logger::Instance().Log(opitcore::LogLevel::Info,  "runtime", msg)
#define OPIT_LOG_WARN(msg)  opitcore::Logger::Instance().Log(opitcore::LogLevel::Warning, "runtime", msg)
#define OPIT_LOG_ERROR(msg) opitcore::Logger::Instance().Log(opitcore::LogLevel::Error, "runtime", msg)
#define OPIT_LOG_CRIT(msg)  opitcore::Logger::Instance().Log(opitcore::LogLevel::Critical, "runtime", msg)

} // namespace opitcore
