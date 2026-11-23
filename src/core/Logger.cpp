// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace opitcore {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

bool Logger::Init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.close();
    }
    m_logPath = logFile;
    m_file.open(logFile, std::ios::app);
    if (!m_file.is_open()) {
        std::cerr << "[OPIT] Falha ao abrir arquivo de log: " << logFile << std::endl;
        return false;
    }
    m_file.seekp(0, std::ios::end);
    m_bytesWritten = (m_file.tellp() > 0) ? static_cast<uint64_t>(m_file.tellp()) : 0;
    return true;
}

void Logger::RotateIfNeededLocked() {
    if (!m_file.is_open() || m_bytesWritten < k_max_log_bytes) return;
    m_file.close();
    std::error_code ec;
    const std::filesystem::path oldPath = m_logPath + ".old";
    std::filesystem::remove(oldPath, ec);
    std::filesystem::rename(m_logPath, oldPath, ec);
    if (ec) {
        // Arquivo travado por outro processo: continua no mesmo arquivo (nao perde linhas)
        m_file.open(m_logPath, std::ios::app);
        m_bytesWritten = 0;
        return;
    }
    m_file.open(m_logPath, std::ios::app);
    m_bytesWritten = 0;
    if (m_file.is_open()) {
        m_file << "[OPIT] Log rotacionado: arquivo anterior movido para " << oldPath.string() << std::endl;
    }
}

void Logger::SetLevel(LogLevel level) {
    m_level = level;
}

void Logger::Log(LogLevel level, const std::string& message) {
    Log(level, "runtime", message, "");
}

void Logger::Log(LogLevel level, const std::string& category, const std::string& message, const std::string& metaJson) {
    if (level < m_level) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << (ms % 1000) << "] ";

    switch (level) {
        case LogLevel::Debug: oss << "[DEBUG] "; break;
        case LogLevel::Info:  oss << "[INFO]  "; break;
        case LogLevel::Warning: oss << "[WARN]  "; break;
        case LogLevel::Error: oss << "[ERROR] "; break;
        case LogLevel::Critical: oss << "[CRIT]  "; break;
    }
    
    std::string catUpper = category;
    for (auto& c : catUpper) c = (char)::toupper(c);
    oss << "[" << catUpper << "] " << message;
    std::string line = oss.str();

    if (m_file.is_open()) {
        m_file << line << std::endl;
        // Se for aviso, erro ou crítico, força a gravação imediata em disco para não perder se o Windows travar
        if (level >= LogLevel::Warning) {
            m_file.flush();
        }
        m_bytesWritten += line.size() + 1;
        RotateIfNeededLocked();
    }
    std::cout << line << std::endl;

    m_recentLogs.push_back(line);
    if (m_recentLogs.size() > 500) {
        m_recentLogs.pop_front();
    }
}

std::vector<std::string> Logger::GetRecentLogs(size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    size_t count = (m_recentLogs.size() < maxCount) ? m_recentLogs.size() : maxCount;
    auto it = m_recentLogs.end() - count;
    for (; it != m_recentLogs.end(); ++it) {
        result.push_back(*it);
    }
    return result;
}

} // namespace opitcore
