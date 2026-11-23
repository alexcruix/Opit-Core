// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace opitcore {

struct SystemMetricsSnapshot;

enum class AiProvider {
    Gemini = 0,
    OpenAi = 1,
    Anthropic = 2,
    DeepSeek = 3,
    Groq = 4,
    Custom = 5
};

struct AiModelInfo {
    std::string id;
    std::string displayName;
};

// Pergunta de esclarecimento que a IA fez ao usuario (protocolo opit_ask). A UI mostra
// as opcoes como botoes; a escolhida volta como nova mensagem do usuario.
struct AiAsk {
    std::string question;
    std::vector<std::string> options;
};

class AiAssistantManager {
public:
    static AiAssistantManager& Instance();

    void Initialize();

    void SetApiKey(const std::string& key);
    std::string GetApiKey() const;
    bool HasApiKey() const;

    void SetProvider(AiProvider provider);
    AiProvider GetProvider() const;
    std::string GetProviderName() const;
    static std::string ProviderToString(AiProvider prov);
    static AiProvider StringToProvider(const std::string& str);

    void SetCustomEndpoint(const std::string& endpoint);
    std::string GetCustomEndpoint() const;

    void SetSelectedModel(const std::string& model);
    std::string GetSelectedModel() const;

    void SaveSettings();
    void LoadSettings();

    // Otimização Estratégica Periódica por IA em segundo plano
    void SetPeriodicEnabled(bool enabled);
    bool IsPeriodicEnabled() const;

    void SetPeriodicIntervalMinutes(int minutes);
    int GetPeriodicIntervalMinutes() const;

    std::string GetLastRecommendation() const;

    // Executado periodicamente pelo CoreRuntime (não bloqueante)
    void TickPeriodicOptimization(const SystemMetricsSnapshot& snap);

    // Consulta a API do provedor com a chave atual e retorna a lista real de modelos disponíveis
    bool FetchAvailableModels(std::vector<AiModelInfo>& outModels, std::string& outError);

    // Despachante de comandos da IA (Tool Calling / Action Dispatcher)
    bool HasPendingAiAction() const;
    std::string GetPendingAiActionJson() const;
    void ClearPendingAiAction();

    // Pergunta de esclarecimento pendente (protocolo opit_ask); consumida pela UI
    bool HasPendingAiAsk() const;
    AiAsk GetPendingAiAsk() const;
    void ClearPendingAiAsk();
    bool ExecuteAiAction(const std::string& actionJson, std::string& outSummary, std::string& outError);
    bool ExecuteAiAction(std::string& outFeedback);
    bool CanRollbackAiAction() const;
    bool RollbackLastAiAction(std::string& outSummary, std::string& outError);
    bool RollbackLastAiAction(std::string& outFeedback);
    std::string GetLastActionSummary() const;

    // Envio direto de prompt livre para o modelo configurado
    bool Query(const std::string& prompt, std::string& outResponse, std::string& outError);

    // Gera um diagnóstico inteligente do hardware em tempo real com sugestões personalizadas de ajustes
    bool GenerateSystemOptimizationReport(const SystemMetricsSnapshot& snap, std::string& outReport, std::string& outError);

private:
    AiAssistantManager();
    ~AiAssistantManager() = default;

    mutable std::mutex m_mutex;
    std::string m_apiKey;
    AiProvider m_provider = AiProvider::Gemini;
    std::string m_customEndpoint = "https://openrouter.ai/api/v1";
    std::string m_selectedModel = "gemini-2.0-flash";
    bool m_periodicEnabled = false;
    int m_periodicIntervalMinutes = 30;
    std::string m_lastRecommendation;
    std::string m_pendingAiActionJson;
    std::string m_lastActionSummary;
    AiAsk m_pendingAiAsk;
    bool m_hasPendingAiAsk = false;

    struct AiActionSnapshot {
        bool hasSnapshot = false;
        std::string previousProfile;
        uint32_t prevCpuMinAC = 5;
        uint32_t prevCpuMaxAC = 100;
        uint32_t prevCpuMinDC = 5;
        uint32_t prevCpuMaxDC = 75;
        bool prevAllowTurbo = true;
        uint32_t prevCoreParkingAC = 50;
        uint32_t prevCoreParkingDC = 75;
        int prevGpuPref = 2;
        bool prevDxgiLowLatency = true;
        double prevThermalLimit = 80.0;
        std::unordered_map<std::string, std::string> prevServiceRules;
        std::string actionDescription;
        std::string timestamp;
    } m_lastSnapshot;

    bool m_initialized = false;
};

} // namespace opitcore
