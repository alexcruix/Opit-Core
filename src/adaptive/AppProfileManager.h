// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "TelemetryValidator.h"
#include "core/DatabaseManager.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace opitcore {

enum class AppObjective {
    MaxPerformance = 0, // Máximo desempenho mensurável (FPS / vazão de processamento)
    LowLatency = 1,     // Baixa latência (resposta imediata, escalonamento sem estacionamento de núcleos)
    Stability = 2,      // Estabilidade (redução de micro-stuttering, controle térmico estrito)
    Balance = 3,        // Equilíbrio (desempenho, consumo e temperatura equilibrados)
    Eco = 4             // Economia (redução de consumo e preservação de bateria)
};

inline const char* AppObjectiveToString(AppObjective obj) {
    switch (obj) {
        case AppObjective::MaxPerformance: return "MaxPerformance";
        case AppObjective::LowLatency: return "LowLatency";
        case AppObjective::Stability: return "Stability";
        case AppObjective::Balance: return "Balance";
        case AppObjective::Eco: return "Eco";
        default: return "Unknown";
    }
}

inline AppObjective StringToAppObjective(const std::string& str) {
    if (str == "MaxPerformance") return AppObjective::MaxPerformance;
    if (str == "LowLatency") return AppObjective::LowLatency;
    if (str == "Stability") return AppObjective::Stability;
    if (str == "Eco") return AppObjective::Eco;
    return AppObjective::Balance;
}

struct AppIdentity {
    uint32_t pid = 0;
    std::string processName;
    std::string normalizedPath;
    uint64_t fileSizeBytes = 0;
    uint64_t lastWriteTime = 0;
    std::string fileHash;
    bool hasDigitalSignature = false;
    uint32_t parentPid = 0;
    std::string windowTitle;

    bool IsValid() const { return pid > 0 && !processName.empty(); }
};

struct AppProfile {
    std::string appId;
    std::string normalizedPath;
    std::string processName;
    std::string fileHash;
    AppObjective objective = AppObjective::Balance;
    std::string authorizedModules;
    double confidenceScore = 0.5; // 0.0 a 1.0
    double baselineScore = 0.0;
    int experimentCount = 0;
    std::string lastValidatedTimestamp;
};

class AppProfileManager {
public:
    static AppProfileManager& Instance();

    // Identificação robusta de processo e aplicação
    AppIdentity IdentifyProcess(uint32_t pid);
    AppIdentity IdentifyForegroundWindow();

    // Gestão de perfis individuais
    AppProfile GetOrCreateProfile(const AppIdentity& app);
    bool SaveProfile(const AppProfile& profile);
    bool UpdateObjective(const std::string& appId, AppObjective objective);

    // Função de avaliação de objetivos matemáticos
    static double EvaluateObjectiveScore(AppObjective obj, const ValidatedSystemTelemetry& telemetry, double frameTimeMs = 0.0);

private:
    AppProfileManager() = default;

    std::string ComputeAppId(const std::string& normalizedPath, const std::string& processName, const std::string& hash);
    std::string ComputeFileIdentityHash(const std::wstring& path, uint64_t fileSize, uint64_t lastWrite);

    // Persiste o perfil assumindo que m_mutex JA esta tomada; evita a reentrada que congela a janela de perfis.
    bool SaveProfileLocked(const AppProfile& profile);

    std::mutex m_mutex;
    std::unordered_map<std::string, AppProfile> m_profilesCache;
};

} // namespace opitcore
