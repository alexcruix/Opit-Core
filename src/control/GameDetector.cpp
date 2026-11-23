// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "GameDetector.h"
#include "utils/StringUtils.h"
#include <algorithm>

namespace opitcore {

GameDetector::GameDetector() {
    InitDefaults();
}

void GameDetector::InitDefaults() {
    std::vector<std::string> defaults = {
        "cs2.exe", "csgo.exe", "valorant.exe", "dota2.exe", "gta5.exe",
        "fortniteclient-win64-shipping.exe", "league of legends.exe",
        "witcher3.exe", "cyberpunk2077.exe", "overwatch.exe",
        "r5apex.exe", "cod.exe", "rainbowsix.exe", "rocketleague.exe",
        "minecraft.exe", "javaw.exe", "fifa23.exe", "fc24.exe", "fc25.exe"
    };

    for (const auto& g : defaults) {
        m_games.push_back(StringUtils::ToLower(g));
    }
}

bool GameDetector::IsGameProcess(const std::string& exeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    for (const auto& g : m_games) {
        if (g == lower) return true;
    }
    return false;
}

void GameDetector::AddGame(const std::string& exeName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    if (lower.empty()) return;

    for (const auto& g : m_games) {
        if (g == lower) return; // Já existe
    }
    m_games.push_back(lower);
}

bool GameDetector::RemoveGame(const std::string& exeName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    auto it = std::find(m_games.begin(), m_games.end(), lower);
    if (it != m_games.end()) {
        m_games.erase(it);
        return true;
    }
    return false;
}

void GameDetector::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_games.clear();
    m_activeGames.clear();
    m_gameRunning = false;
    m_currentGame.clear();
}

std::vector<std::string> GameDetector::GetGames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_games;
}

std::vector<std::string> GameDetector::GetActiveGames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeGames;
}

std::string GameDetector::CheckRunningGame(const std::vector<std::string>& runningProcessNames) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeGames.clear();

    for (const auto& proc : runningProcessNames) {
        std::string lower = StringUtils::ToLower(StringUtils::Trim(proc));
        for (const auto& g : m_games) {
            if (g == lower) {
                // Adiciona se ainda nao estiver na lista de jogos ativos
                if (std::find(m_activeGames.begin(), m_activeGames.end(), proc) == m_activeGames.end()) {
                    m_activeGames.push_back(proc);
                }
            }
        }
    }

    m_gameRunning = !m_activeGames.empty();
    m_currentGame = m_activeGames.empty() ? "" : m_activeGames.front();
    return m_currentGame;
}

} // namespace opitcore
