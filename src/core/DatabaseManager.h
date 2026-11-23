// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"
#include <string>
#include <vector>
#include <mutex>

struct sqlite3;

namespace opitcore {

struct GameRuleRecord {
    std::string exeName;
    std::string profile;
    bool enabled = true;
};

struct GameProfileRecord {
    std::string exeName;                        // ex: "cyberpunk2077.exe"
    std::string gameName;                       // ex: "Cyberpunk 2077"
    std::string fullPath;                       // ex: "C:\Games\Cyberpunk2077\bin\x64\Cyberpunk2077.exe"
    std::string cpuProfile = "HighPerformance"; // "Eco", "Balanced", "HighPerformance", "Custom"
    uint32_t cpuMinPercent = 5;                 // 0 a 100
    uint32_t cpuMaxPercent = 100;               // 0 a 100
    std::string gpuProfile = "HighPerformance"; // "Eco", "Balanced", "HighPerformance", "Custom"
    int gpuPowerLimitPercent = 100;             // ex: 100%
    int gpuCoreOffsetMHz = 0;
    int gpuMemOffsetMHz = 0;
    std::string powerSchemeGuid;                // GUID do plano do Windows
    bool preventDiskSleep = true;
    bool preventSystemSleep = true;
    bool enabled = true;
};

struct MetricHistoryRecord {
    int id = 0;
    std::string timestamp;
    double cpuUsage = 0.0;
    double ramUsage = 0.0;
    double tempC = 0.0;
};

struct RecoveryJournalRecord {
    int64_t id = 0;
    std::string changeId;
    std::string resourceType;
    std::string resourceKey;
    std::string originalValue;
    std::string appliedValue;
    std::string status;
    std::string timestamp;
};

struct AppProfileRecord {
    std::string appId;
    std::string normalizedPath;
    std::string processName;
    std::string fileHash;
    int objective = 3; // Balance
    std::string authorizedModules;
    double confidenceScore = 0.5;
    double baselineScore = 0.0;
    int experimentCount = 0;
    std::string lastValidated;
};

class DatabaseManager {
public:
    static DatabaseManager& Instance();

    bool Open(const std::string& path = "");
    void Close();
    bool IsOpen() const;

    // Configurações chave-valor
    bool SetSetting(const std::string& key, const std::string& value);
    std::string GetSetting(const std::string& key, const std::string& defaultValue = "");

    // Regras e perfis avancados de jogos
    bool AddGameRule(const std::string& exeName, const std::string& profile = "HighPerformance");
    bool RemoveGameRule(const std::string& exeName);
    std::vector<GameRuleRecord> GetGameRules();

    bool SaveGameProfile(const GameProfileRecord& profile);
    bool GetGameProfile(const std::string& exeName, GameProfileRecord& outProfile);
    std::vector<GameProfileRecord> GetAllGameProfiles();
    bool DeleteGameProfile(const std::string& exeName);

    // Logs e histórico de auditoria
    bool LogEvent(const std::string& level, const std::string& message);
    bool RecordMetric(double cpuUsage, double ramUsage, double tempC);
    std::vector<MetricHistoryRecord> GetMetricsHistory(int limit = 50);
    bool ClearMetricsHistory();

    // Base de conhecimento de hardware documentada
    DocumentedHardwareSpec LookupHardwareSpec(const std::string& modelName, const std::string& categoryPrefix = "");
    bool SeedHardwareKnowledgeBase();

    // Diário persistente de recuperação (Recovery Journal)
    bool AddRecoveryJournalEntry(const RecoveryJournalRecord& entry);
    bool UpdateRecoveryJournalStatus(const std::string& changeId, const std::string& status);
    bool LoadRecoveryJournalEntry(const std::string& changeId, RecoveryJournalRecord& outEntry);
    std::vector<RecoveryJournalRecord> GetPendingRecoveryEntries();
    int PruneRecoveryJournalHistory(int keepLast);

    // Perfis adaptativos por aplicação
    bool SaveAppProfile(const AppProfileRecord& profile);
    bool LoadAppProfile(const std::string& appId, AppProfileRecord& outProfile);
    std::vector<AppProfileRecord> GetAllAppProfiles();

    // Memória adaptativa e histórico de decisões
    bool RecordAdaptiveExperiment(const std::string& appId, const std::string& varName, double baseScore, double resultScore, double delta, const std::string& verdict, const std::string& justification);
    bool RecordDecisionHistory(const std::string& appId, int actionType, const std::string& targetResource, const std::string& hypothesis, const std::string& justification, double confidence, bool safetyOverride);

    std::string GetDatabasePath() const { return m_dbPath; }
    int GetSchemaVersion() const { return m_schemaVersion; }

private:
    DatabaseManager();
    ~DatabaseManager();

    bool ApplyMigrations();

    mutable std::mutex m_mutex;
    sqlite3* m_db = nullptr;
    std::string m_dbPath;
    int m_schemaVersion = 0;
};

} // namespace opitcore
