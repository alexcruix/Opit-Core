#include "FanDiagnostics.h"
#include <chrono>

namespace opitcore {

FanDiagnostics::FanDiagnostics() = default;
FanDiagnostics::~FanDiagnostics() = default;

void FanDiagnostics::Log(const std::string& level, const std::string& deviceId, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FanDiagnosticEvent ev;
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.level = level;
    ev.deviceId = deviceId;
    ev.message = message;

    m_events.push_back(ev);
    if (m_events.size() > kMaxEvents) {
        m_events.pop_front();
    }
}

std::vector<FanDiagnosticEvent> FanDiagnostics::GetRecentEvents(size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FanDiagnosticEvent> out;
    size_t count = (maxCount < m_events.size()) ? maxCount : m_events.size();
    for (auto it = m_events.rbegin(); it != m_events.rbegin() + count; ++it) {
        out.push_back(*it);
    }
    return out;
}

void FanDiagnostics::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
}

} // namespace opitcore
