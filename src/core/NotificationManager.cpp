// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "NotificationManager.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include "utils/StringUtils.h"

#include <chrono>

namespace opitcore {

NotificationManager& NotificationManager::Instance() {
    static NotificationManager instance;
    return instance;
}

void NotificationManager::SetSink(Sink sink) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sink = std::move(sink);
}

void NotificationManager::LoadSettings() {
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = db.GetSetting("ai_notify_events", "1") != "0";
}

void NotificationManager::SetEnabled(bool enable) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = enable;
    }
    auto& db = DatabaseManager::Instance();
    if (db.IsOpen()) {
        db.SetSetting("ai_notify_events", enable ? "1" : "0");
    }
    OPIT_LOG_INFO(std::string("NotificationManager: avisos do tray ") +
                  (enable ? "ativados" : "desativados") + " pela usuaria.");
}

// Cooldown por categoria (ms): eventos de modo sao raros e urgentes; servicos/processos acontecem
// em rajada e precisam ser agregados; a recomendacao da IA e periodica por natureza.
namespace {
constexpr uint64_t kCooldownMs[] = {4000, 30000, 60000, 60000, 60000, 300000};
}

void NotificationManager::Notify(Category cat, const std::string& titleUtf8, const std::string& messageUtf8) {
    if (cat < Category::ModeChange || cat >= Category::Count) return;

    Sink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_enabled || !m_sink) return;
        const int idx = static_cast<int>(cat);
        const uint64_t nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (m_lastNotifyMs[idx] != 0 && nowMs - m_lastNotifyMs[idx] < kCooldownMs[idx]) return;
        m_lastNotifyMs[idx] = nowMs;
        sinkCopy = m_sink;
    }

    // Fora do lock: o sink mexe com janelas (Shell_NotifyIconW).
    sinkCopy(StringUtils::ToWide(titleUtf8), StringUtils::ToWide(messageUtf8));
}

} // namespace opitcore
