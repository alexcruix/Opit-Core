#include "AdaptiveEngine.h"
#include "core/Logger.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/NotificationManager.h"
#include "control/ModeCoordinator.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace opitcore {
namespace {

// Relogio monotono proprio: o cooldown entre escritas nao pode ser enganado por ajuste de relogio do sistema.
uint64_t SteadyNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

const char* SchemeNameForStage(int stage) {
    switch (stage) {
    case 0: return "ECO";
    case 1: return "BALANCED";
    case 2: return "HIGH_PERFORMANCE";
    default: return "UNKNOWN";
    }
}

const char* SchemeLabelForStage(int stage) {
    switch (stage) {
    case 0: return "Economico";
    case 1: return "Balanceado";
    case 2: return "Alto Desempenho";
    default: return "desconhecido";
    }
}

} // namespace

AdaptiveEngine::PlanBands AdaptiveEngine::BandsFromLimits(uint32_t balancedPlanMin, uint32_t highPlanMin) {
    PlanBands b;
    b.balancedMin = static_cast<double>(std::clamp(balancedPlanMin, 1u, 100u));
    b.highMin = std::max(b.balancedMin + 1.0, static_cast<double>(std::clamp(highPlanMin, 1u, 100u)));
    if (b.highMin > 100.0) b.highMin = 100.0;
    b.ecoMax = b.balancedMin - 1.0;
    return b;
}

int AdaptiveEngine::SchemeStageForCpuUsage(double cpuUsagePct, const PlanBands& bands) {
    if (cpuUsagePct >= bands.highMin) return 2;
    if (cpuUsagePct >= bands.balancedMin) return 1;
    return 0;
}

// As faixas vem dos limites salvos na janela de CPU Min/Max, na linha da fonte de energia do momento:
// arrancar da bateria tem que trocar a tabela junto com o Windows, nao deixar o motor lendo a faixa
// da tomada num notebook que acabou de perder o plugue.
void AdaptiveEngine::RefreshPlanBands(PowerManager& powerMgr) {
    const BatteryStatusInfo bat = powerMgr.GetBatteryStatus();
    const bool onBattery = bat.hasBattery && !bat.isAcOnline;
    const auto& adv = ModuleAdvancedSettings::Instance();
    m_bandsFromBattery = onBattery;
    m_bands = BandsFromLimits(adv.cpuMinMax.balancedPlan.Min(onBattery),
                              adv.cpuMinMax.highPerfPlan.Min(onBattery));
}

AdaptiveEngine& AdaptiveEngine::Instance() {
    static AdaptiveEngine instance;
    return instance;
}

AdaptiveEngine::AdaptiveEngine()
    : m_diagnoser(5) {
}

void AdaptiveEngine::Initialize() {
    m_diagnoser.Reset();
    m_expState = ExperimentState::Idle;
    m_baselineSamples.clear();
    m_observationSamples.clear();
    m_lastActionTimestampMs = 0;
    m_currentCoreParking = kUnknownParking;
    m_currentSchemeStage = 0;
    m_consecutiveHighDemandCycles = 0;
    m_consecutiveLowDemandCycles = 0;
    m_bandHoldCycles = 0;
    m_lastObservedBand = -1;
    m_activeChangeId.clear();
    m_plannedExperiment = AdaptiveDecision();
    m_hasPlannedExperiment = false;
    m_realitySynced = false;
    m_hwWriteBudgetStartMs = 0;
    m_hwWritesInWindow = 0;
    m_budgetWarningLogged = false;
    m_nextIntegrityCheckMs = 0;
}

double AdaptiveEngine::MedianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

double AdaptiveEngine::ScaledMadOf(const std::vector<double>& v, double median) {
    if (v.empty()) return 0.0;
    std::vector<double> dev;
    dev.reserve(v.size());
    for (double x : v) dev.push_back(std::fabs(x - median));
    return MedianOf(std::move(dev)) * 1.4826;
}

bool AdaptiveEngine::SyncRealityWithSystem(PowerManager& powerMgr, bool logOutcome) {
    PowerScheme active = powerMgr.GetActiveScheme();

    uint32_t parkingAc = 0, parkingDc = 0;
    bool parkingRead = (!active.guid.empty() && powerMgr.ReadCoreParking(active.guid, parkingAc, parkingDc));
    m_currentCoreParking = parkingRead ? parkingAc : kUnknownParking;

    std::string eco = powerMgr.FindEcoScheme();
    std::string balanced = powerMgr.FindBalancedScheme();
    std::string high = powerMgr.FindHighPerformanceScheme();
    if (!active.guid.empty()) {
        if (active.guid == high) m_currentSchemeStage = 2;
        else if (active.guid == balanced) m_currentSchemeStage = 1;
        else if (active.guid == eco) m_currentSchemeStage = 0;
    }

    if (parkingRead) {
        if (logOutcome) {
            OPIT_LOG_INFO("AdaptiveEngine: estado real lido do sistema - plano=" + active.name +
                          " (estagio " + std::to_string(m_currentSchemeStage) + "), core parking AC=" +
                          std::to_string(m_currentCoreParking) + "%");
        }
        m_realitySynced = true;
    } else if (logOutcome) {
        OPIT_LOG_WARN("AdaptiveEngine: core parking do plano ativo ilegivel; o motor nao escreve sobre o que ainda nao consegue ler.");
    }
    return parkingRead;
}

AdaptiveDecision AdaptiveEngine::ProcessTick(const SystemMetricsSnapshot& rawSnapshot, PowerManager& powerMgr, ServiceManager& serviceMgr) {
    auto tStart = std::chrono::high_resolution_clock::now();

    // 1. Identificar aplicação ativa em primeiro plano
    m_currentApp = AppProfileManager::Instance().IdentifyForegroundWindow();
    if (m_currentApp.IsValid()) {
        m_currentProfile = AppProfileManager::Instance().GetOrCreateProfile(m_currentApp);
    } else {
        m_currentProfile.appId = "system_default";
        m_currentProfile.processName = "desktop";
        m_currentProfile.objective = AppObjective::Balance;
    }

    // 2. Validar e normalizar telemetria recebida
    ValidatedSystemTelemetry valTelemetry = m_validator.Validate(rawSnapshot, m_currentApp.pid);

    // 3. Alimentar diagnóstico temporal de gargalos
    m_diagnoser.FeedSample(valTelemetry);
    m_latestDiagnosis = m_diagnoser.Evaluate();

    // Auditoria de gargalo para logs e inteligência artificial
    if (m_latestDiagnosis.primaryType != BottleneckType::None) {
        if (m_latestDiagnosis.primaryType != m_lastLoggedBottleneckType ||
            (valTelemetry.sampleTimestampMs - m_lastBottleneckLogTimestampMs >= 30000)) {
            
            std::ostringstream ssLog;
            ssLog << "[ALERTA DE GARGALO]: " << BottleneckTypeToString(m_latestDiagnosis.primaryType)
                  << " (Confianca: " << DiagnosisConfidenceToString(m_latestDiagnosis.confidence) << ") — "
                  << m_latestDiagnosis.justification;
            if (!m_latestDiagnosis.supportingEvidence.empty()) {
                ssLog << " | Evidencia: " << m_latestDiagnosis.supportingEvidence[0];
            }
            Logger::Instance().Log(m_latestDiagnosis.confidence >= DiagnosisConfidence::Probable ? LogLevel::Warning : LogLevel::Info,
                                   "bottleneck", ssLog.str());
            m_lastLoggedBottleneckType = m_latestDiagnosis.primaryType;
            m_lastBottleneckLogTimestampMs = valTelemetry.sampleTimestampMs;
        }
    } else if (m_lastLoggedBottleneckType != BottleneckType::None) {
        Logger::Instance().Log(LogLevel::Info, "bottleneck",
                               "[GARGALO RESOLVIDO]: Telemetria normalizada. Sistema operando sem restricoes ou gargalos criticos de hardware.");
        m_lastLoggedBottleneckType = BottleneckType::None;
        m_lastBottleneckLogTimestampMs = valTelemetry.sampleTimestampMs;
    }

    // 4. Espelho interno e integridade de modo ANTES de avaliar: uma decisao tomada sobre valores
    // desconhecidos ou sobre um plano que saiu do modo pedido nunca poderia ser confiavel.
    if (!m_realitySynced) {
        SyncRealityWithSystem(powerMgr);
    }
    RefreshPlanBands(powerMgr);
    EnforceModeIntegrity(powerMgr, serviceMgr, m_currentProfile, valTelemetry);

    // 5. Calcular pontuação objetiva do cenário atual
    double currentScore = AppProfileManager::EvaluateObjectiveScore(m_currentProfile.objective, valTelemetry,
                                                                    valTelemetry.frameTimeMs.IsUsable() ? valTelemetry.frameTimeMs.value : 0.0);

    // 6. Avaliar e arbitrar decisão adaptativa
    auto& settings = UnifiedSettingsModel::Instance();
    AdaptiveDecision decision = EvaluateDecision(valTelemetry, m_latestDiagnosis, m_currentProfile, settings);
    decision.timestampMs = valTelemetry.sampleTimestampMs;

    // 7. Gerenciar ciclo de experimentação controlada
    if (decision.safetyOverride) {
        // Precedencia absoluta da seguranca: um experimento em curso e revertido antes da acao termica.
        if (m_expState != ExperimentState::Idle) {
            InterruptExperimentForSafety(powerMgr, serviceMgr);
        }
        ApplyDirectAction(decision, powerMgr, serviceMgr, true);
    } else if (m_expState == ExperimentState::Idle) {
        // A escada direta existia apenas como decisao; aqui ela ganha executor.
        if (decision.action == AdaptiveActionType::ApplyMildAdjustment ||
            decision.action == AdaptiveActionType::BlockForSafety) {
            ApplyDirectAction(decision, powerMgr, serviceMgr, false);
        }
    } else if (m_expState == ExperimentState::GatheringBaseline) {
        m_baselineSamples.push_back(currentScore);
        if (m_baselineSamples.size() >= k_experimentSamples) {
            m_expState = ExperimentState::Applying;
            decision.action = AdaptiveActionType::RunControlledExperiment;
            decision.hypothesis = "Alteracao controlada de variavel unica para alcancar objetivo " +
                                  std::string(AppObjectiveToString(m_currentProfile.objective));
        } else {
            decision.action = AdaptiveActionType::CollectMoreData;
            decision.justification = "Capturando linha de base (amostra " + std::to_string(m_baselineSamples.size()) + "/" + std::to_string(k_experimentSamples) + ")";
        }
    } else if (m_expState == ExperimentState::Applying) {
        // Executa a decisao PLANEJADA (a mesma que abriu o experimento), nao a decisao deste tick.
        if (!m_hasPlannedExperiment) {
            m_expState = ExperimentState::Idle;
            decision.action = AdaptiveActionType::DoNothing;
            decision.justification = "Experimento abortado: nenhuma alteracao foi planejada para aplicar.";
        } else {
            bool applied = ExecuteAction(m_plannedExperiment, powerMgr, serviceMgr);
            if (applied) {
                m_expState = ExperimentState::Observing;
                m_observationSamples.clear();
                m_lastActionTimestampMs = SteadyNowMs();
                decision.action = AdaptiveActionType::RunControlledExperiment;
                decision.justification = "Alteracao controlada aplicada (" + m_plannedExperiment.targetResource + "=" +
                                         m_plannedExperiment.targetValue + "). Iniciando janela de observacao.";
            } else {
                m_expState = ExperimentState::Idle;
                m_hasPlannedExperiment = false;
                decision.action = AdaptiveActionType::DoNothing;
                decision.justification = "Falha ao aplicar alteracao controlada com seguranca.";
            }
        }
    } else if (m_expState == ExperimentState::Observing) {
        m_observationSamples.push_back(currentScore);
        if (m_observationSamples.size() >= k_experimentSamples) {
            m_expState = ExperimentState::Evaluating;
        } else {
            decision.action = AdaptiveActionType::CollectMoreData;
            decision.justification = "Observando impacto da alteracao (" + std::to_string(m_observationSamples.size()) + "/" + std::to_string(k_experimentSamples) + ")";
        }
    } else if (m_expState == ExperimentState::Evaluating) {
        // Estatistica pareada: janelas iguais, deltas por par (observacao - baseline).
        // Mediana + MAD tornam o veredito imune a picos isolados que a media engolia.
        std::vector<double> diffs;
        diffs.reserve(m_observationSamples.size());
        for (size_t i = 0; i < m_observationSamples.size() && i < m_baselineSamples.size(); ++i) {
            diffs.push_back(m_observationSamples[i] - m_baselineSamples[i]);
        }
        double medDelta = MedianOf(diffs);
        double noise = ScaledMadOf(diffs, medDelta);

        if (medDelta >= 3.0 && medDelta > 2.0 * noise) {
            double medianBase = MedianOf(m_baselineSamples);
            double medianObs = MedianOf(m_observationSamples);
            // Sucesso comprovado por evidências: manter e aprender
            decision.action = AdaptiveActionType::MaintainValidatedConfig;
            decision.justification = "Melhoria comprovada pareada: mediana +" + std::to_string(medDelta) +
                                     " pts (MAD " + std::to_string(noise) + " pts, acima do dobro do ruido)";
            DatabaseManager::Instance().RecordAdaptiveExperiment(m_currentProfile.appId, m_expVariable, medianBase, medianObs, medDelta, "SUCCESS", decision.justification);
            if (!RecoveryJournal::Instance().FinalizeKept(m_activeChangeId)) {
                OPIT_LOG_WARN("AdaptiveEngine: experimento validado mas o diario nao marcou KEPT: " + m_activeChangeId);
            }
        } else {
            double medianBase = MedianOf(m_baselineSamples);
            double medianObs = MedianOf(m_observationSamples);
            // Ineficaz ou regressão: reverter imediatamente via diário de recuperação
            decision.action = AdaptiveActionType::RevertAdjustment;
            bool reverted = RecoveryJournal::Instance().RevertChange(m_activeChangeId, powerMgr, serviceMgr);
            if (reverted) {
                decision.justification = "Resultado inconclusivo ou regressao (mediana " + std::to_string(medDelta) +
                                         " pts, MAD " + std::to_string(noise) + " pts). Valores originais restaurados no sistema.";
            } else {
                decision.justification = "Resultado inconclusivo (mediana " + std::to_string(medDelta) +
                                         " pts) e a RESTAURACAO FALHOU: o sistema pode continuar com o valor experimentado.";
                OPIT_LOG_ERROR("AdaptiveEngine: reversao do diario falhou para: " + m_activeChangeId);
            }
            DatabaseManager::Instance().RecordAdaptiveExperiment(m_currentProfile.appId, m_expVariable, medianBase,
                                                                medianObs, medDelta, reverted ? "REVERTED" : "FAILED_RECOVERY",
                                                                decision.justification);
            // O sistema voltou ao valor de origem: o espelho interno precisa voltar a ser verdade.
            SyncRealityWithSystem(powerMgr);
        }
        m_expState = ExperimentState::Idle;
        m_baselineSamples.clear();
        m_observationSamples.clear();
        m_activeChangeId.clear();
        m_hasPlannedExperiment = false;
    }

    // 8. Persistir histórico de decisão no SQLite
    DatabaseManager::Instance().RecordDecisionHistory(
        m_currentProfile.appId,
        static_cast<int>(decision.action),
        decision.targetResource,
        decision.hypothesis,
        decision.justification,
        decision.confidence,
        decision.safetyOverride
    );

    // 9. Medir auto-sobrecarga (overhead profiling)
    auto tEnd = std::chrono::high_resolution_clock::now();
    uint64_t durMicros = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
    m_overhead.lastCycleDurationMicros = durMicros;
    if (durMicros > m_overhead.maxCycleDurationMicros) {
        m_overhead.maxCycleDurationMicros = durMicros;
    }
    m_overhead.totalCyclesExecuted++;
    // Ciclo típico de ~500 micros a cada 1000ms = ~0.05% de CPU
    m_overhead.estimatedSelfCpuPercent = (durMicros / 10000.0);

    return decision;
}

bool AdaptiveEngine::ApplyDirectAction(AdaptiveDecision& decision, PowerManager& powerMgr, ServiceManager& serviceMgr, bool ignoreCooldown) {
    uint64_t nowSteadyMs = SteadyNowMs();
    if (!ignoreCooldown && m_lastActionTimestampMs != 0 &&
        (nowSteadyMs - m_lastActionTimestampMs) < kDirectHwWriteCooldownMs) {
        decision.action = AdaptiveActionType::CollectMoreData;
        decision.justification += " | Ajuste adiado: cooldown de " + std::to_string(kDirectHwWriteCooldownMs / 1000) +
                                  "s entre escritas de hardware evita oscilacao.";
        return false;
    }
    // Somente a seguranca termica fura o orcamento; o guarda de modo fura apenas o cooldown, porque a
    // varredura de integridade ja tem cadencia propria e precisa devolver o corredor inteiro de uma vez.
    const bool bypassBudget = ignoreCooldown && decision.safetyOverride;
    if (!HwWriteAllowed(nowSteadyMs, bypassBudget, decision.targetResource + "=" + decision.targetValue)) {
        decision.action = AdaptiveActionType::CollectMoreData;
        decision.justification += " | Ajuste adiado: orcamento de escritas de hardware desta janela de 5 minutos ja foi consumido.";
        return false;
    }

    bool wrote = false;
    if (ExecuteAction(decision, powerMgr, serviceMgr, &wrote)) {
        if (wrote) {
            m_lastActionTimestampMs = nowSteadyMs;
            RegisterHwWrite(nowSteadyMs);
            decision.justification += " | Aplicado e registrado no diario de recuperacao.";
        } else {
            decision.justification += " | Nenhuma escrita foi necessaria: o sistema ja estava exatamente no valor alvo.";
        }
        return true;
    }

    decision.action = AdaptiveActionType::DoNothing;
    decision.justification += " | Nao aplicado: o registro previo falhou ou o sistema recusou a escrita.";
    return false;
}

void AdaptiveEngine::InterruptExperimentForSafety(PowerManager& powerMgr, ServiceManager& serviceMgr) {
    double medianBase = MedianOf(m_baselineSamples);
    double medianObs = m_observationSamples.empty() ? medianBase : MedianOf(m_observationSamples);

    if (m_expState == ExperimentState::Applying || m_expState == ExperimentState::Observing) {
        bool reverted = m_activeChangeId.empty() || RecoveryJournal::Instance().RevertChange(m_activeChangeId, powerMgr, serviceMgr);
        DatabaseManager::Instance().RecordAdaptiveExperiment(m_currentProfile.appId, m_expVariable, medianBase, medianObs,
                                                             medianObs - medianBase, "INTERRUPTED_BY_SAFETY",
                                                             reverted ? "Experimento interrompido por seguranca termica; valores originais restaurados."
                                                                      : "Experimento interrompido por seguranca termica, mas a RESTAURACAO FALHOU.");
        OPIT_LOG_WARN("AdaptiveEngine: experimento interrompido por seguranca termica (reversao: " +
                      std::string(reverted ? "ok" : "FALHOU") + ")");
    }

    m_expState = ExperimentState::Idle;
    m_activeChangeId.clear();
    m_hasPlannedExperiment = false;
    m_baselineSamples.clear();
    m_observationSamples.clear();
}

AdaptiveDecision AdaptiveEngine::EvaluateDecision(const ValidatedSystemTelemetry& telemetry, const BottleneckDiagnosis& diag,
                                                 const AppProfile& profile, const UnifiedSettingsModel& settings) {
    AdaptiveDecision dec;
    dec.confidence = 0.85;

    // Invariante de Segurança 1: Se o orquestrador global estiver desativado, NADA pode ser alterado
    if (!settings.enableOrchestrator) {
        dec.action = AdaptiveActionType::DoNothing;
        dec.justification = "Orquestrador global desativado pelo usuario no Painel Unificado.";
        return dec;
    }

    // Invariante de Segurança 2: Guardião Térmico tem precedência absoluta sobre otimização
    if (telemetry.isThermalThrottling || (telemetry.cpuTemperatureC.IsUsable() && telemetry.cpuTemperatureC.value >= 88.0)) {
        if (m_currentCoreParking == kUnknownParking) {
            dec.action = AdaptiveActionType::BlockForSafety;
            dec.safetyOverride = true;
            dec.justification = "Seguranca termica com estado de nucleos ilegivel: nenhuma escrita e feita porque um alvo chutado poderia AUMENTAR os nucleos ativos em plena subida termica.";
            dec.evidence.push_back("Temperatura detectada: " + std::to_string((int)telemetry.cpuTemperatureC.value) + "°C");
            return dec;
        }

        // Arrefecer nunca pode abrir mais núcleos: o alvo térmico jamais supera o que já está ativo.
        uint32_t coolTarget = std::min(25u, m_currentCoreParking);
        dec.action = AdaptiveActionType::BlockForSafety;
        dec.safetyOverride = true;
        dec.targetResource = "CORE_PARKING";
        dec.targetValue = std::to_string(coolTarget);
        dec.justification = "Precedencia de Seguranca Termica: teto de nucleos ativos em " + std::to_string(coolTarget) + "% para arrefecimento imediato";
        dec.evidence.push_back("Temperatura detectada: " + std::to_string((int)telemetry.cpuTemperatureC.value) + "°C");
        return dec;
    }

    // Escalada de consumo é a única coisa que o modo Economia Máxima proíbe por si só.
    const bool mayEscalate = EscalationAuthorized(profile);

    // 1. Banda de plano = os limites Min/Max salvos na janela de CPU Min/Max, escolhidos pela classe
    //    de hardware e pela fonte de energia do momento (desktop 0-35/36-78/79-100, notebook na tomada
    //    0-25/26-50/51-75, notebook na bateria 0-20/21-35/36-60). O PLANO segue a banda antes de
    //    qualquer escada de nucleos: dentro de um plano com teto de CPU, abrir nucleos nao da
    //    capacidade nova e so queima a janela de
    //    escritas. A mudanca exige a banda mantida por alguns ciclos (histerese anti-thrashing).
    //    Quem liga isto e o interruptor "Inteligente" da linha Planos de energia: sem ele o programa
    //    nao troca plano nenhum - a escolha feita por ela na tela ou no Windows permanece.
    //    Com "Forcar plano economico" ligado a banda deixa de mandar no plano: a imposicao da janela
    //    avancada vale mais que o motor, e as outras alavancas (nucleos, parking, GPU, processos) seguem.
    if (settings.levelPowerPlans.autoSmartMode && !ModuleAdvancedSettings::Instance().powerPlans.forceEcoPlan) {
        int bandStage = SchemeStageForCpuUsage(telemetry.cpuUsage.value, m_bands);
        // Jogo em execucao e demanda explicita de desempenho: nunca cai para uma banda mais baixa no meio.
        // O interruptor "alternar para Alto Desempenho ao iniciar jogo" da tela avancada e o que manda aqui.
        if (telemetry.isGameRunning && ModuleAdvancedSettings::Instance().powerPlans.autoSwitchOnGame) {
            bandStage = std::max(bandStage, 2);
        }

        if (bandStage == m_currentSchemeStage) {
            m_bandHoldCycles = 0;
            m_lastObservedBand = bandStage;
        } else {
            if (bandStage != m_lastObservedBand) {
                m_lastObservedBand = bandStage;
                m_bandHoldCycles = 0;
            }
            m_bandHoldCycles++;

            const bool goingUp = bandStage > m_currentSchemeStage;
            // Subir de plano e escalada de consumo: na Economia Maxima rigida so um app em foco com
            // objetivo de desempenho manda subir. Descer para a banda de baixo e sempre permitido.
            if (!goingUp || mayEscalate) {
                const int holdNeeded = goingUp ? kBandUpHoldCycles : kBandDownHoldCycles;
                if (m_bandHoldCycles >= holdNeeded) {
                    dec.action = AdaptiveActionType::ApplyMildAdjustment;
                    dec.targetResource = "POWER_SCHEME";
                    dec.targetValue = SchemeNameForStage(bandStage);
                    dec.hypothesis = std::string("Plano de energia segue a banda de uso de CPU: ") + SchemeLabelForStage(bandStage);
                    dec.justification = "Uso de CPU em " + std::to_string((int)telemetry.cpuUsage.value) + "% pertence a banda " +
                                        std::string(SchemeLabelForStage(bandStage)) + " ha " + std::to_string(m_bandHoldCycles) +
                                        " ciclos; o plano ativo (" + std::string(SchemeLabelForStage(m_currentSchemeStage)) +
                                        ") passa para " + std::string(SchemeLabelForStage(bandStage)) + ".";
                    dec.evidence.push_back("Bandas vigentes (limites salvos na janela de CPU Min/Max, fonte " +
                                           std::string(m_bandsFromBattery ? "bateria" : "tomada") + "): 0-" +
                                           std::to_string((int)m_bands.ecoMax) + "% Economico, " +
                                           std::to_string((int)m_bands.balancedMin) + "-" + std::to_string((int)m_bands.balancedMax()) +
                                           "% Balanceado, " + std::to_string((int)m_bands.highMin) + "-100% Alto Desempenho");
                    m_bandHoldCycles = 0;
                    return dec;
                }
            }
        }
    }

    // 2. Deteccao de demanda (FPS, Frametime e Saturacao de Nucleos), dentro do plano da banda.
    //    Os limites sao as mesmas faixas da janela de CPU Min/Max: acima do piso do plano de
    //    desempenho e alta demanda, ate o topo do plano economico e repouso.
    bool isHighDemand = (diag.primaryType == BottleneckType::CpuBound && diag.confidence >= DiagnosisConfidence::Probable) ||
                        (telemetry.cpuUsage.value >= m_bands.highMin) ||
                        (telemetry.isGameRunning);

    bool isLowDemand = (!telemetry.isGameRunning && telemetry.cpuUsage.value <= m_bands.ecoMax && diag.primaryType == BottleneckType::None);

    if (isHighDemand) {
        m_consecutiveHighDemandCycles++;
        m_consecutiveLowDemandCycles = 0;

        if (!mayEscalate) {
            const bool forcedEco = ModuleAdvancedSettings::Instance().powerPlans.forceEcoPlan;
            dec.action = AdaptiveActionType::DoNothing;
            dec.justification = std::string(forcedEco ? "Plano economico forcado rigido: " : "Economia Maxima rigida: ") +
                                "demanda alta registrada, mas os limites do plano economico permanecem porque a aplicacao em foco ('" +
                                profile.processName + "') nao exige maxima performance.";
            dec.evidence.push_back("Nucleos ativos atuais: " + std::to_string(m_currentCoreParking) + "%");
            return dec;
        }

        // Se o núcleo atingiu carga alta, vai ativando outros núcleos gradativamente (25% -> 50% -> 75% -> 100%)
        if (m_currentCoreParking != kUnknownParking && m_currentCoreParking < 100) {
            uint32_t nextCores = std::min(100u, m_currentCoreParking + 25);
            dec.action = AdaptiveActionType::ApplyMildAdjustment;
            dec.targetResource = "CORE_PARKING";
            dec.targetValue = std::to_string(nextCores);
            dec.hypothesis = "Ativando nucleos adicionais para suprir demanda mantendo o plano e o throttle do usuario";
            dec.justification = "Demanda elevada detectada. Elevando nucleos ativos de " + std::to_string(m_currentCoreParking) + "% para " + std::to_string(nextCores) + "%";
            return dec;
        }

        // Dentro do plano da banda, o lever fino e a escada de nucleos.
    } else if (isLowDemand) {
        m_consecutiveLowDemandCycles++;
        m_consecutiveHighDemandCycles = 0;

        // Reducao gradual dos nucleos de volta ao minimo economico quando a demanda cessa,
        // dentro do plano que a banda de uso ja escolheu.
        if (m_consecutiveLowDemandCycles >= 3) {
            if (m_currentCoreParking != kUnknownParking && m_currentCoreParking > 25) {
                // Vai reduzindo os núcleos ativos de volta ao mínimo econômico (ex: 25%)
                uint32_t nextCores = (m_currentCoreParking > 25) ? m_currentCoreParking - 25 : 25;
                dec.action = AdaptiveActionType::ApplyMildAdjustment;
                dec.targetResource = "CORE_PARKING";
                dec.targetValue = std::to_string(nextCores);
                dec.hypothesis = "Repouso gradual de nucleos inativos para economia de energia";
                dec.justification = "Reduzindo nucleos desparqueados para " + std::to_string(nextCores) + "% em estado ocioso";
                m_consecutiveLowDemandCycles = 0;
                return dec;
            }
        }
    }

    // Diagnóstico de Saturação de Disco em Modo Jogo ou Estabilidade
    if (diag.primaryType == BottleneckType::DiskBound && diag.confidence >= DiagnosisConfidence::Probable) {
        if (settings.modWindowsServices && m_expState == ExperimentState::Idle) {
            dec.action = AdaptiveActionType::RunControlledExperiment;
            dec.targetResource = "SERVICE";
            dec.targetValue = "WSearch";
            dec.hypothesis = "Pausar indexacao do Windows mitigara picos de I/O em disco";
            dec.justification = "Contencao de I/O em disco detectada concorrendo com aplicacao em foco";
            dec.evidence = diag.supportingEvidence;
            m_expState = ExperimentState::GatheringBaseline;
            m_baselineSamples.clear();
            m_expVariable = "PAUSE_WSEARCH";
            m_plannedExperiment = dec;
            m_hasPlannedExperiment = true;
            return dec;
        }
    }

    dec.action = AdaptiveActionType::DoNothing;
    dec.justification = "Politicas de intervencao existentes suficientes para o contexto atual";
    return dec;
}

bool AdaptiveEngine::ExecuteAction(const AdaptiveDecision& decision, PowerManager& powerMgr, ServiceManager& serviceMgr, bool* wroteSomething) {
    if (wroteSomething) *wroteSomething = false;
    if (decision.targetResource.empty()) return false;

    // Um experimento controlado permanece APPLIED ate a validacao, para que uma queda o desfache no proximo boot.
    // Ja a acao direta e terminal (KEPT): ela expressa a politica vigente e nao deve ser desfeita pela recuperacao.
    const bool isControlledExperiment = (decision.action == AdaptiveActionType::RunControlledExperiment);
    const std::string changeId = RecoveryJournal::Instance().GenerateChangeId(decision.targetResource);

    auto commitOrRollback = [&](bool applied) {
        if (!applied) {
            RecoveryJournal::Instance().RevertChange(changeId, powerMgr, serviceMgr);
            return;
        }
        if (isControlledExperiment) {
            RecoveryJournal::Instance().ConfirmApplied(changeId);
            m_activeChangeId = changeId;
        } else {
            RecoveryJournal::Instance().FinalizeKept(changeId);
        }
    };

    if (decision.targetResource == "CORE_PARKING") {
        uint32_t targetCores = 0;
        try {
            targetCores = static_cast<uint32_t>(std::stoul(decision.targetValue));
        } catch (...) {
            OPIT_LOG_ERROR("AdaptiveEngine: alvo de core parking ilegivel: " + decision.targetValue);
            return false;
        }

        PowerScheme active = powerMgr.GetActiveScheme();
        uint32_t origAc = 0, origDc = 0;
        if (active.guid.empty() || !powerMgr.ReadCoreParking(active.guid, origAc, origDc)) {
            OPIT_LOG_ERROR("AdaptiveEngine: core parking atual ilegivel; nada foi alterado para nao perder o valor de origem.");
            return false;
        }
        if (targetCores == origAc) {
            m_currentCoreParking = origAc;
            return true;
        }
        if (!RecoveryJournal::Instance().PrepareChange(changeId, "CORE_PARKING", active.guid,
                                                       std::to_string(origAc) + ":" + std::to_string(origDc),
                                                       std::to_string(targetCores))) {
            return false;
        }

        bool applied = powerMgr.SetCoreParking(targetCores);
        if (applied) {
            if (wroteSomething) *wroteSomething = true;
            m_currentCoreParking = targetCores;
            OPIT_LOG_INFO("AdaptiveEngine: core parking ajustado de " + std::to_string(origAc) + "% para " +
                          std::to_string(targetCores) + "% dos nucleos minimos ativos.");
        }
        commitOrRollback(applied);
        return applied;
    }

    if (decision.targetResource == "POWER_SCHEME") {
        std::string targetSchemeGuid;
        int targetStage = m_currentSchemeStage;
        if (decision.targetValue == "ECO") {
            targetSchemeGuid = powerMgr.FindEcoScheme();
            targetStage = 0;
        } else if (decision.targetValue == "BALANCED") {
            targetSchemeGuid = powerMgr.FindBalancedScheme();
            targetStage = 1;
        } else if (decision.targetValue == "HIGH_PERFORMANCE") {
            targetSchemeGuid = powerMgr.FindHighPerformanceScheme();
            targetStage = 2;
        } else {
            OPIT_LOG_ERROR("AdaptiveEngine: plano de energia desconhecido: " + decision.targetValue);
            return false;
        }

        if (targetSchemeGuid.empty()) {
            OPIT_LOG_ERROR("AdaptiveEngine: o plano '" + decision.targetValue + "' nao existe neste sistema; nada foi alterado.");
            return false;
        }

        PowerScheme current = powerMgr.GetActiveScheme();
        if (!current.guid.empty() && current.guid == targetSchemeGuid) {
            m_currentSchemeStage = targetStage;
            return true;
        }

        if (current.guid.empty() ||
            !RecoveryJournal::Instance().PrepareChange(changeId, "POWER_SCHEME", "ACTIVE_SCHEME", current.guid, targetSchemeGuid)) {
            return false;
        }

        bool applied = powerMgr.SetActiveScheme(targetSchemeGuid);
        if (applied) {
            m_currentSchemeStage = targetStage;

            // Re-aplica o throttle individual salvo para o plano correspondente. Escrever pelas duas
            // linhas e obrigatorio aqui: SetProcessorThrottleRange grava o mesmo numero em AC e DC,
            // e era assim que o teto da tomada apagava o teto mais baixo pedido para a bateria.
            auto& adv = ModuleAdvancedSettings::Instance();
            const PlanThrottleSettings t = adv.cpuMinMax.GetPlanThrottle(targetSchemeGuid);
            if (!powerMgr.WriteSchemeProcessorThrottle(targetSchemeGuid, t.minPercentAC, t.maxPercentAC,
                                                       t.minPercentDC, t.maxPercentDC)) {
                OPIT_LOG_ERROR("AdaptiveEngine: plano alternado, mas o throttle do plano nao foi reaplicado (" +
                               std::to_string(t.Min(m_bandsFromBattery)) + "%-" + std::to_string(t.Max(m_bandsFromBattery)) + "%).");
            }

            uint32_t newAc = 0, newDc = 0;
            if (powerMgr.ReadCoreParking(targetSchemeGuid, newAc, newDc)) {
                m_currentCoreParking = newAc;
            }

            OPIT_LOG_INFO("AdaptiveEngine: Plano de energia alternado dinamicamente para: " + decision.targetValue);
            NotificationManager::Instance().Notify(NotificationManager::Category::PlanSwitch,
                                                   "Plano de energia alterado",
                                                   decision.justification);
        }
        commitOrRollback(applied);
        return applied;
    }

    if (decision.targetResource == "SERVICE") {
        bool isRunning = serviceMgr.IsServiceRunning(decision.targetValue);
        std::string origState = isRunning ? "RUNNING" : "STOPPED";

        if (!RecoveryJournal::Instance().PrepareChange(changeId, "SERVICE", decision.targetValue, origState, "PAUSED")) {
            return false;
        }

        bool applied = serviceMgr.PauseOrStopService(decision.targetValue);
        if (applied) {
            OPIT_LOG_INFO("AdaptiveEngine: Servico pausado controladamente para teste adaptativo: " + decision.targetValue);
        }
        commitOrRollback(applied);
        return applied;
    }

    OPIT_LOG_ERROR("AdaptiveEngine: nenhum executor para o recurso: " + decision.targetResource);
    return false;
}

bool AdaptiveEngine::EscalationAuthorized(const AppProfile& profile) const {
    // Eco rigido: subir consumo so com app em foco de objetivo explicitamente desempenho/latencia. Vale para
    // os dois caminhos que prometem rigidez - o modo Economia Maxima do coordenador e a trava "Forcar plano
    // economico" da janela avancada. Antes so o primeiro contava, entao a trava devolvia true para qualquer app
    // em primeiro plano (explorer, editor) e o plano forcado escapava sem nenhuma prova no log.
    // Sessao de jogo ativa e uma aplicacao de desempenho configurada explicitamente pelo usuario
    // (perfil de jogo com CPU/GPU Alto Desempenho): a hierarquia do coordenador e jogo > economia,
    // e a trava "Forcar plano economico" nao pode brigar com o perfil que o proprio usuario definiu.
    // Sem isto o fixador devolvia o plano para ECO ~1 s apos o OnGameStarted aplicar Alto Desempenho
    // (medido 2026-11-18: aplicado 11:28:15.585, revertido 11:28:16.728).
    if (ModeCoordinator::Instance().IsGameModeActive()) return true;

    const bool rigidEco = ModeCoordinator::Instance().GetCurrentMode() == SystemOperatingMode::MaxEconomy ||
                          ModuleAdvancedSettings::Instance().powerPlans.forceEcoPlan;
    if (!rigidEco) return true;

    return profile.objective == AppObjective::MaxPerformance || profile.objective == AppObjective::LowLatency;
}

bool AdaptiveEngine::HwWriteAllowed(uint64_t nowSteadyMs, bool bypass, const std::string& reason) {
    // A seguranca termica nunca espera orcamento: adiar um arrefecimento e o que pode travar o sistema.
    if (bypass) return true;

    if (m_hwWriteBudgetStartMs == 0 || (nowSteadyMs - m_hwWriteBudgetStartMs) >= kHwWriteBudgetWindowMs) {
        m_hwWriteBudgetStartMs = nowSteadyMs;
        m_hwWritesInWindow = 0;
        m_budgetWarningLogged = false;
    }

    if (m_hwWritesInWindow >= kMaxHwWritesPerWindow) {
        if (!m_budgetWarningLogged) {
            m_budgetWarningLogged = true;
            OPIT_LOG_WARN("AdaptiveEngine: teto de " + std::to_string(kMaxHwWritesPerWindow) +
                          " escritas de hardware na janela de 5 minutos atingido; '" + reason +
                          "' adiado. Se algo mais no sistema continua mudando o plano, isso nao e o motor.");
        }
        return false;
    }
    return true;
}

void AdaptiveEngine::RegisterHwWrite(uint64_t nowSteadyMs) {
    if (m_hwWriteBudgetStartMs == 0 || (nowSteadyMs - m_hwWriteBudgetStartMs) >= kHwWriteBudgetWindowMs) {
        m_hwWriteBudgetStartMs = nowSteadyMs;
        m_hwWritesInWindow = 0;
    }
    m_hwWritesInWindow++;
}

void AdaptiveEngine::EnforceModeIntegrity(PowerManager& powerMgr, ServiceManager& serviceMgr, const AppProfile& profile,
                                          const ValidatedSystemTelemetry& telemetry) {
    uint64_t nowSteadyMs = SteadyNowMs();
    if (nowSteadyMs < m_nextIntegrityCheckMs) return;
    m_nextIntegrityCheckMs = nowSteadyMs + kModeIntegrityIntervalMs;

    const SystemOperatingMode mode = ModeCoordinator::Instance().GetCurrentMode();

    // O espelho volta a ser o sistema a cada conferencia; memoria do motor nunca e prova do que esta ativo.
    SyncRealityWithSystem(powerMgr, false);

    // "Forcar plano economico" e uma trava da janela avancada, independente do modo do coordenador: enquanto
    // estiver ligada nada pode tirar o plano ativo da economia. Sem este bloco o botao prometia "precedencia
    // absoluta sobre o modo automatico" e o plano escapava na volta da esquina, sem registro nenhum.
    const bool ecoForced = ModuleAdvancedSettings::Instance().powerPlans.forceEcoPlan;
    if (mode == SystemOperatingMode::Normal && !ecoForced) return;

    // Calor extremo (>=88 C ou throttle termico): proibir ESCALADA — empurrar potencia na maquina
    // quente piora exatamente o problema. Porem a trava de economia CONTINUA valendo: puxar para eco
    // e o que alivia o calor. Antes o return aqui desligava tambem a trava, e o plano ficava elevado
    // no pico do calor sem nenhuma correcao.
    const bool thermalBlocked =
        telemetry.isThermalThrottling ||
        (telemetry.cpuTemperatureC.IsUsable() && telemetry.cpuTemperatureC.value >= 88.0);

    PowerScheme active = powerMgr.GetActiveScheme();
    if (active.guid.empty()) return;

    auto& adv = ModuleAdvancedSettings::Instance();
    // Sessao de jogo ativa mantem a elevacao mesmo quente (hierarquia do coordenador: jogo supera
    // economia); fora de jogo, calor extremo suprime escalada e a trava de economia vale.
    const bool mayEscalate = EscalationAuthorized(profile) &&
                             (!thermalBlocked || mode == SystemOperatingMode::Gaming);

    if (ecoForced && mode != SystemOperatingMode::MaxEconomy) {
        const std::string ecoGuid = powerMgr.FindEcoScheme();
        if (!ecoGuid.empty() && active.guid != ecoGuid) {
            if (mayEscalate) {
                OPIT_LOG_INFO("AdaptiveEngine: plano economico forcado com aplicacao de desempenho em foco ('" +
                              profile.processName + "'): elevacao mantida por regra do programa.");
            } else if (HwWriteAllowed(nowSteadyMs, false, "trava de plano economico")) {
                AdaptiveDecision fix;
                fix.action = AdaptiveActionType::ApplyMildAdjustment;
                fix.targetResource = "POWER_SCHEME";
                fix.targetValue = "ECO";
                fix.justification = "Trava 'Forcar plano economico': o plano ativo saiu da economia sem aplicacao de desempenho em foco.";
                ApplyDirectAction(fix, powerMgr, serviceMgr, true);
                if (SyncRealityWithSystem(powerMgr, false)) active = powerMgr.GetActiveScheme();
            }
        }
    }

    if (mode == SystemOperatingMode::MaxEconomy) {
        const std::string ecoGuid = powerMgr.FindEcoScheme();
        if (!ecoGuid.empty() && active.guid != ecoGuid) {
            if (mayEscalate) {
                OPIT_LOG_INFO("AdaptiveEngine: Economia Maxima com aplicacao de desempenho em foco ('" + profile.processName +
                              "'): plano elevado mantido por regra do programa, nao por deriva.");
            } else {
                AdaptiveDecision fix;
                fix.action = AdaptiveActionType::ApplyMildAdjustment;
                fix.targetResource = "POWER_SCHEME";
                fix.targetValue = "ECO";
                fix.justification = "Guardiao de modo: o plano ativo saiu da Economia Maxima sem aplicacao de desempenho em foco.";
                ApplyDirectAction(fix, powerMgr, serviceMgr, true);
                // O plano mudou de mao: o teto e o piso conferidos abaixo tem que ser os do plano agora ativo,
                // nao os do plano que acabou de ser corrigido para fora do caminho.
                if (SyncRealityWithSystem(powerMgr, false)) active = powerMgr.GetActiveScheme();
            }
        }

        // Teto de CPU: um maximo acima do que a usuaria declarou na Economia nao e decisao do programa.
        // Cada linha confere com o teto da propria linha — na bateria o teto declarado e outro (mais baixo).
        const uint32_t ecoCeilingAc = adv.cpuMinMax.ecoPlan.maxPercentAC;
        const uint32_t ecoCeilingDc = adv.cpuMinMax.ecoPlan.maxPercentDC;
        uint32_t minAc = 0, maxAc = 0, minDc = 0, maxDc = 0;
        if (!mayEscalate && ecoCeilingAc > 0 && ecoCeilingAc < 100 &&
            powerMgr.ReadSchemeProcessorThrottle(active.guid, minAc, maxAc, minDc, maxDc) &&
            (maxAc > ecoCeilingAc || maxDc > ecoCeilingDc)) {
            const uint32_t targetAc = std::min(maxAc, ecoCeilingAc);
            const uint32_t targetDc = std::min(maxDc, ecoCeilingDc);
            if (HwWriteAllowed(nowSteadyMs, false, "teto de CPU do plano economico")) {
                const std::string changeId = RecoveryJournal::Instance().GenerateChangeId("CPU_CEILING");
                if (RecoveryJournal::Instance().PrepareChange(changeId, "CPU_THROTTLE", active.guid,
                                                              std::to_string(minAc) + ":" + std::to_string(maxAc) + "/" +
                                                              std::to_string(minDc) + ":" + std::to_string(maxDc),
                                                              std::to_string(minAc) + ":" + std::to_string(targetAc) + "/" +
                                                              std::to_string(minDc) + ":" + std::to_string(targetDc))) {
                    if (powerMgr.WriteSchemeProcessorThrottle(active.guid, minAc, targetAc, minDc, targetDc)) {
                        RecoveryJournal::Instance().FinalizeKept(changeId);
                        RegisterHwWrite(nowSteadyMs);
                        OPIT_LOG_WARN("Guardiao de modo: teto de CPU do plano economico estava em " + std::to_string(maxAc) +
                                      "% (tomada) / " + std::to_string(maxDc) + "% (bateria) e voltou aos " +
                                      std::to_string(ecoCeilingAc) + "% / " + std::to_string(ecoCeilingDc) +
                                      "% escolhidos pela usuaria.");
                        NotificationManager::Instance().Notify(NotificationManager::Category::Thermal,
                                                               "Teto de CPU restaurado",
                                                               "O teto do plano economico tinha subido para " +
                                                               std::to_string(maxAc) + "% (tomada) / " + std::to_string(maxDc) +
                                                               "% (bateria) e voltou aos " + std::to_string(ecoCeilingAc) + "% / " +
                                                               std::to_string(ecoCeilingDc) + "% que voce escolheu.");
                    } else {
                        RecoveryJournal::Instance().RevertChange(changeId, powerMgr, serviceMgr);
                    }
                }
            }
        }

        // Piso de nucleos: acima dele a maquina deixou de estar em economia, qualquer que tenha sido a origem.
        const uint32_t ecoFloor = (adv.coreParking.minCoresActiveAC > 0 && adv.coreParking.minCoresActiveAC <= 25) ?
                                  adv.coreParking.minCoresActiveAC : 10;
        if (!mayEscalate && m_currentCoreParking != kUnknownParking && m_currentCoreParking > ecoFloor) {
            AdaptiveDecision fix;
            fix.action = AdaptiveActionType::ApplyMildAdjustment;
            fix.targetResource = "CORE_PARKING";
            fix.targetValue = std::to_string(ecoFloor);
            fix.justification = "Guardiao de modo: nucleos ativos em " + std::to_string(m_currentCoreParking) +
                                "% acima do piso de " + std::to_string(ecoFloor) + "% da Economia Maxima.";
            ApplyDirectAction(fix, powerMgr, serviceMgr, true);
            SyncRealityWithSystem(powerMgr, false);
        }
    } else if (mode == SystemOperatingMode::Gaming) {
        if (m_currentCoreParking != kUnknownParking && m_currentCoreParking < 100) {
            AdaptiveDecision fix;
            fix.action = AdaptiveActionType::ApplyMildAdjustment;
            fix.targetResource = "CORE_PARKING";
            fix.targetValue = "100";
            fix.justification = "Guardiao de modo: jogo em execucao com nucleos estacionados em " +
                                std::to_string(m_currentCoreParking) + "%; todos reativados.";
            ApplyDirectAction(fix, powerMgr, serviceMgr, true);
            SyncRealityWithSystem(powerMgr, false);
        }
    }
}

} // namespace opitcore
