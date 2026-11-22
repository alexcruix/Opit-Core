#pragma once
#include "FanTypes.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>

namespace opitcore {

struct FanDiagnosticEvent {
    uint64_t timestampMs = 0;
    std::string level; // "INFO", "WARN", "ERROR"
    std::string deviceId;
    std::string message;
};

class FanDiagnostics {
public:
    FanDiagnostics();
    ~FanDiagnostics();

    void Log(const std::string& level, const std::string& deviceId, const std::string& message);
    std::vector<FanDiagnosticEvent> GetRecentEvents(size_t maxCount = 50);
    void Clear();

private:
    std::deque<FanDiagnosticEvent> m_events;
    std::mutex m_mutex;
    static const size_t kMaxEvents = 100;
};

} // namespace opitcore
