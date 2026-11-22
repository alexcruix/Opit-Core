#pragma once
#include "TelemetryValidator.h"
#include "AppProfileManager.h"
#include "BottleneckDiagnoser.h"
#include "RecoveryJournal.h"
#include "core/UnifiedSettingsModel.h"
#include "power/PowerManager.h"
#include "platform/windows/ServiceManager.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace opitcore {

enum class AdaptiveActionType {
    DoNothing = 0,
    CollectMoreData = 1,
    ApplyMildAdjustment = 2,
    RunControlledExperiment = 3,
    MaintainValidatedConfig = 4,
    RevertAdjustment = 5,
    BlockForSafety = 6
};

inline const char* AdaptiveActionTypeToString(AdaptiveActionType a) {
    switch (a) {
        case AdaptiveActionType::DoNothing: return "DoNothing";
        case AdaptiveActionType::CollectMoreData: return "CollectMoreData";
        case AdaptiveActionType::ApplyMildAdjustment: return "ApplyMildAdjustment";
        case AdaptiveActionType::RunControlledExperiment: return "RunControlledExperiment";
        case AdaptiveActionType::MaintainValidatedConfig: return "MaintainValidatedConfig";
        case AdaptiveActionType::RevertAdjustment: return "RevertAdjustment";
        default: return "BlockForSafety";
    }
}

struct AdaptiveDecision {
    AdaptiveActionType action = AdaptiveActionType::DoNothing;
    std::string targetResource;
    std::string targetValue;
    double confidence = 0.5; // 0.0 a 1.0
    std::string hypothesis;
    std::string justification;
    std::vector<std::string> evidence;
    bool safetyOverride = false;
    uint64_t timestampMs = 0;
};

struct SelfOverheadStats {
    uint64_t lastCycleDurationMicros = 0;
    uint64_t maxCycleDurationMicros = 0;
    uint64_t totalCyclesExecuted = 0;
    double estimatedSelfCpuPercent = 0.0;
};

class AdaptiveEngine {
public:
    static AdaptiveEngine& Instance();

    void Initialize();
    
    // Processamento do ciclo adaptativo inteligente
    AdaptiveDecision ProcessTick(const SystemMetricsSnapshot& rawSnapshot, PowerManager& powerMgr, ServiceManager& serviceMgr);

    // Getters de estado e diagnósticos
    BottleneckDiagnosis GetLatestDiagnosis() const { return m_latestDiagnosis; }
    AppIdentity GetCurrentApp() const { return m_currentApp; }
    AppProfile GetCurrentProfile() const { return m_currentProfile; }
    SelfOverheadStats GetSelfOverhead() const { return m_overhead; }

    // Bandas de plano. Nao sao constantes do codigo: sao os proprios limites Min/Max gravados na
    // janela "Configuracao Avancada: CPU Min/Max" para o equipamento detectado. O piso declarado de
    // cada plano e a borda da banda, entao a tela e o motor leem a mesma linha do mesmo registro:
    //   desktop/servidor 0-35 / 36-78 / 79-100 · notebook 0-25 / 26-50 / 51-75 · bateria 0-20 / 21-35 / 36-60
    struct PlanBands {
        double ecoMax = 35.0;        // topo da banda economica (balanced.min - 1)
        double balancedMin = 36.0;   // inicio da banda do meio
        double highMin = 79.0;       // inicio da banda de desempenho
        double balancedMax() const { return highMin - 1.0; }
    };

    // Recebe os dois pisos salvos (balanced.min e high.min) na fonte de energia do momento e devolve
    // bandas ordenadas e totalas: nenhum uso de CPU pode ficar sem plano, e uma edicao inconsistente
    // nao inverte as faixas.
    static PlanBands BandsFromLimits(uint32_t balancedPlanMin, uint32_t highPlanMin);

    // Banda de plano como funcao pura para poder ser conferida sem tocar no sistema:
    // 0 = Economico, 1 = Balanceado, 2 = Alto Desempenho.
    static int SchemeStageForCpuUsage(double cpuUsagePct, const PlanBands& bands);

private:
    AdaptiveEngine();

    enum class ExperimentState {
        Idle,
        GatheringBaseline,
        Applying,
        Observing,
        Evaluating
    };

    AdaptiveDecision EvaluateDecision(const ValidatedSystemTelemetry& telemetry, const BottleneckDiagnosis& diag,
                                      const AppProfile& profile, const UnifiedSettingsModel& settings);

    // wroteSomething (opcional) distingue "ja estava no alvo" de "escrevi no sistema" — o guarda nao declara nada que nao tenha acontecido
    bool ExecuteAction(const AdaptiveDecision& decision, PowerManager& powerMgr, ServiceManager& serviceMgr, bool* wroteSomething = nullptr);

    // Executa uma decisao direta (escada ou seguranca) respeitando o cooldown, e explica o resultado na decisao
    bool ApplyDirectAction(AdaptiveDecision& decision, PowerManager& powerMgr, ServiceManager& serviceMgr, bool ignoreCooldown);

    // Desfaz o experimento em curso quando a seguranca termica assume o controle
    void InterruptExperimentForSafety(PowerManager& powerMgr, ServiceManager& serviceMgr);

    // Eco rigido: fora do modo Economia o corredor e o proprio motor; dentro dele, so um app em foco
    // com objetivo explicito de desempenho autoriza subir nucleos ou sair do plano economico.
    bool EscalationAuthorized(const AppProfile& profile) const;

    // Confere o estado real do sistema contra o modo declarado e corrige a deriva.
    // Com emergencia termica ativa o guarda nao escreve: abrir nucleos para "corrigir o modo" esquentaria mais a maquina.
    void EnforceModeIntegrity(PowerManager& powerMgr, ServiceManager& serviceMgr, const AppProfile& profile,
                              const ValidatedSystemTelemetry& telemetry);

    // Orcamento anti-oscilacao: escritas de hardware tem intervalo minimo e teto por janela
    bool HwWriteAllowed(uint64_t nowSteadyMs, bool bypass, const std::string& reason);
    void RegisterHwWrite(uint64_t nowSteadyMs);

    // Alinha o espelho interno com o estado real do sistema (planos e core parking lidos do powrprof)
    // Devolve true apenas quando o core parking foi efetivamente lido; sem essa leitura o motor nao escreve.
    bool SyncRealityWithSystem(PowerManager& powerMgr, bool logOutcome = true);

    // Intervalo minimo entre escritas diretas de hardware: evita que a escada oscile a cada tick
    static constexpr uint64_t kDirectHwWriteCooldownMs = 20000;

    // Orcamento global: nenhuma janela de 5 minutos recebe mais de 8 escritas de hardware
    static constexpr uint64_t kHwWriteBudgetWindowMs = 300000;
    static constexpr int kMaxHwWritesPerWindow = 8;

    // Frequencia da conferencia do estado real contra o modo declarado
    static constexpr uint64_t kModeIntegrityIntervalMs = 10000;

    // Bandas vigentes no ultimo tick, montadas dos limites salvos na fonte de energia real.
    // Reconstrui-las a cada tick e o que faz arrancar da bateria trocar a tabela junto com o Windows.
    PlanBands m_bands;
    bool m_bandsFromBattery = false;
    void RefreshPlanBands(PowerManager& powerMgr);

    // A banda tem que se manter antes de o plano se mexer: subir e mais rapido que descer, e descer
    // devagar e o que impede o plano de ficar trocando atras de um pico de 1% (anti-thrashing).
    static constexpr int kBandUpHoldCycles = 4;
    static constexpr int kBandDownHoldCycles = 10;

    // Estatistica pareada das amostras de linha de base vs observacao
    static double MedianOf(std::vector<double> v);
    static double ScaledMadOf(const std::vector<double>& v, double median);

    TelemetryValidator m_validator;
    BottleneckDiagnoser m_diagnoser;
    
    AppIdentity m_currentApp;
    AppProfile m_currentProfile;
    BottleneckDiagnosis m_latestDiagnosis;
    SelfOverheadStats m_overhead;

    // Estado do motor de experimentação controlada
    ExperimentState m_expState = ExperimentState::Idle;
    std::string m_activeChangeId;
    std::string m_expVariable;
    AdaptiveDecision m_plannedExperiment;         // a decisao que sera executada na fase Applying
    bool m_hasPlannedExperiment = false;
    bool m_realitySynced = false;
    // Amostras brutos de score: decisao por estatistica pareada (mediana + MAD dos deltas),
    // com janelas de baseline e observacao de tamanho IGUAL.
    std::vector<double> m_baselineSamples;
    std::vector<double> m_observationSamples;
    static constexpr size_t k_experimentSamples = 4;
    uint64_t m_lastActionTimestampMs = 0;         // steady_clock ms da ultima escrita direta de hardware

    // Estado da orquestração adaptativa por núcleos e planos (preservando integralmente o throttle de CPU do usuário)
    // O espelho comeca desconhecido e so vale o que foi lido do sistema: uma suposta aqui escreveria sobre o plano da usuaria.
    static constexpr uint32_t kUnknownParking = 0xFFFFFFFFu;
    uint32_t m_currentCoreParking = kUnknownParking;
    int m_currentSchemeStage = 0; // 0 = Eco, 1 = Balanced, 2 = HighPerformance
    int m_consecutiveHighDemandCycles = 0;
    int m_consecutiveLowDemandCycles = 0;

    // Histerese da banda de plano: conta ciclos consecutivos na MESMA banda diferente do plano ativo.
    int m_bandHoldCycles = 0;
    int m_lastObservedBand = -1;

    // Orcamento e auditoria de escritas de hardware
    uint64_t m_hwWriteBudgetStartMs = 0;
    int m_hwWritesInWindow = 0;
    bool m_budgetWarningLogged = false;
    uint64_t m_nextIntegrityCheckMs = 0;

    // Rastreamento e auditoria de gargalos
    BottleneckType m_lastLoggedBottleneckType = BottleneckType::None;
    uint64_t m_lastBottleneckLogTimestampMs = 0;
};

} // namespace opitcore
