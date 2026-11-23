// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace opitcore {

class GameDetector {
public:
    GameDetector();

    bool IsGameProcess(const std::string& exeName) const;
    void AddGame(const std::string& exeName);
    bool RemoveGame(const std::string& exeName);
    void Clear();
    std::vector<std::string> GetGames() const;

    // Varre processos e retorna o executavel do jogo prioritario (ou todos os ativos)
    std::string CheckRunningGame(const std::vector<std::string>& runningProcessNames);

    bool IsGameRunning() const { return m_gameRunning; }
    std::string GetCurrentGame() const { return m_currentGame; }
    std::vector<std::string> GetActiveGames() const;
    size_t GetActiveGameCount() const { return m_activeGames.size(); }

private:
    void InitDefaults();

    mutable std::mutex m_mutex;
    std::vector<std::string> m_games;
    std::vector<std::string> m_activeGames;
    bool m_gameRunning = false;
    std::string m_currentGame;
};

} // namespace opitcore
