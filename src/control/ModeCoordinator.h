// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

#include "core/DatabaseManager.h"
#include "power/PowerManager.h"
#include "hardware/GpuControlManager.h"
#include <string>
#include <vector>
#include <set>
#include <mutex>

namespace opitcore {

enum class SystemOperatingMode {
    Normal = 0,
    MaxEconomy = 1,
    Gaming = 2
};

inline const char* SystemOperatingModeToString(SystemOperatingMode mode) {
    switch (mode) {
        case SystemOperatingMode::MaxEconomy: return "Economia Maxima";
        case SystemOperatingMode::Gaming: return "Modo Jogo";
        default: return "Normal";
    }
}

struct SavedSystemState {
    std::string powerSchemeGuid;
    uint32_t cpuMinPercent = 5;
    uint32_t cpuMaxPercent = 100;
    uint32_t cpuMinPercentDC = 5;
    uint32_t cpuMaxPercentDC = 100;
    uint32_t coreParkingAc = 50;
    uint32_t coreParkingDc = 50;
    GpuPowerProfile gpuProfile = GpuPowerProfile::Balanced;
    int gpuPowerLimitPercent = 100;
    bool diskPowerDownPrevented = false;
    bool systemSleepPrevented = false;
    bool isValid = false;
    // Cada campo so e restaurado se foi efetivamente lido do sistema: um valor chutado escreveria sobre a maquina.
    bool throttleCaptured = false;
    bool coreParkingCaptured = false;
};

class ModeCoordinator {
public:
    static ModeCoordinator& Instance();

    bool Initialize();

    // 1. Modo Economia Máxima
    bool SetDeepEcoMode(bool enable, std::string& outMsg);
    bool IsDeepEcoModeActive() const { return m_deepEcoActive; }

    // 2. Modo Jogo
    bool SetManualGameMode(bool enable, std::string& outMsg);
    bool IsGameModeActive() const { return m_gameModeActive || !m_activeGames.empty(); }

    // Eventos automáticos de jogos (GameDetector integration)
    bool OnGameStarted(const std::string& exeName, std::string& outMsg);
    bool OnGameStopped(const std::string& exeName, std::string& outMsg);
    size_t GetActiveGameCount() const;

    // Estado e modo corrente
    SystemOperatingMode GetCurrentMode() const;
    std::string GetCurrentModeDescription() const;

    // Restauração emergencial ou ao fechar aplicação
    void RestorePreviousState();

private:
    ModeCoordinator();
    ~ModeCoordinator();

    void CaptureCurrentState(SavedSystemState& state);
    void ApplySystemState(const SavedSystemState& state);

    mutable std::mutex m_mutex;
    bool m_initialized = false;

    bool m_deepEcoActive = false;
    bool m_gameModeActive = false;

    // Rastreamento de múltiplos jogos simultâneos
    std::set<std::string> m_activeGames;
    std::string m_primaryRunningGame;

    // Backups de estado
    SavedSystemState m_preDeepEcoState;
    SavedSystemState m_preGameState;

    PowerManager m_powerMgr;
};

} // namespace opitcore
