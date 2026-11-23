// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "CoreRuntime.h"
#include "Logger.h"
#include "DatabaseManager.h"
#include "UnifiedSettingsModel.h"
#include "ModuleAdvancedSettings.h"
#include "hardware/HardwareDetection.h"
#include "hardware/GpuControlManager.h"
#include "platform/windows/ProcessManager.h"
#include "platform/windows/ProcessGovernor.h"
#include "platform/windows/ServiceManager.h"
#include "platform/windows/ElevationHelper.h"
#include "fans/FanControlManager.h"
#include "adaptive/AdaptiveEngine.h"
#include "adaptive/RecoveryJournal.h"
#include "security/OpitSecurityManager.h"
#include "control/ModeCoordinator.h"
#include "ai/AiAssistantManager.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

#ifndef _WIN32
// getpid(): o equivalente do GetCurrentProcessId, usado no ciclo de processos.
#include <unistd.h>
#endif

namespace opitcore {

CoreRuntime& CoreRuntime::Instance() {
    static CoreRuntime instance;
    return instance;
}

CoreRuntime::CoreRuntime() = default;

CoreRuntime::~CoreRuntime() {
    Shutdown();
}

bool CoreRuntime::Initialize() {
    if (m_state == State::Running) return true;

    OPIT_LOG_INFO("CoreRuntime: Inicializando arquitetura Opit-Core...");
    m_state = State::Initializing;

    // 1. Logger
    std::string logPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "opitcore.log");
    Logger::Instance().Init(logPath);

    // 2. Configuração JSON
    std::string cfgPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "opitcore.json");
    Config::Instance().Load(cfgPath);

    // 3. Banco de dados SQLite nativo
    std::string dbPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "opitcore.db");
    DatabaseManager::Instance().Open(dbPath);
    DatabaseManager::Instance().LogEvent("INFO", "CoreRuntime inicializado");

    // 4. Detecção de hardware
    m_hardwareInfo = HardwareDetection::DetectAll();
    m_hardwareInfo.documentedCpu = DatabaseManager::Instance().LookupHardwareSpec(m_hardwareInfo.cpu.brand, "CPU_");
    if (!m_hardwareInfo.gpus.empty()) {
        m_hardwareInfo.documentedGpu = DatabaseManager::Instance().LookupHardwareSpec(m_hardwareInfo.gpus[0].description, "GPU_");
    }
    OPIT_LOG_INFO("Hardware detectado: CPU " + m_hardwareInfo.cpu.brand + " (" + 
                  std::to_string(m_hardwareInfo.cpu.physicalCores) + "C/" + 
                  std::to_string(m_hardwareInfo.cpu.logicalThreads) + "T)");
    if (m_hardwareInfo.documentedCpu.found) {
        OPIT_LOG_INFO("Especificacao documentada da CPU identificada no banco: " + m_hardwareInfo.documentedCpu.model + 
                      " (Base: " + m_hardwareInfo.documentedCpu.clockBase + ", Boost: " + m_hardwareInfo.documentedCpu.clockBoost + 
                      ", TDP: " + m_hardwareInfo.documentedCpu.tdp + ")");
    }

    // 5. Subsistemas
    m_systemMonitor.Initialize();
    m_powerMgr.Initialize();
    AdaptiveEngine::Instance().Initialize();
    FanControlManager::Instance().Initialize();

    // 6. Sincronizar jogos estritamente salvos pelo usuario
    m_gameDetector.Clear();
    auto dbGames = DatabaseManager::Instance().GetAllGameProfiles();
    for (const auto& r : dbGames) {
        m_gameDetector.AddGame(r.exeName);
    }

    // 7. Carregar modelo unificado e configuracoes avancadas por modulo
    UnifiedSettingsModel::Instance().Load();
    ModuleAdvancedSettings::Instance().Load();

    // 7.2. Recuperacao pos-queda: apos o PowerManager inicializado e as configuracoes carregadas,
    // e antes de o ModeCoordinator congelar o estado que ele proprio vira a restaurar.
    int recoveredChanges = RecoveryJournal::Instance().RecoverPendingChanges(m_powerMgr, ServiceManager::Instance());
    if (recoveredChanges > 0) {
        OPIT_LOG_WARN("CoreRuntime: " + std::to_string(recoveredChanges) +
                      " alteracoes pendentes de sessoes anteriores foram restauradas no sistema.");
    }

    ModeCoordinator::Instance().Initialize();

    // 7.5. Sem licenciamento nem ativacao: o motor aplica as politicas assim que o nucleo esta pronto.
    if (UnifiedSettingsModel::Instance().enableOrchestrator) {
        ApplyUnifiedSettings();
    } else {
        ApplyProfileSettings(Config::Instance().GetProfile());
    }

    // O programa nao tem canal de saida de dados: nao ha upload de telemetria, diagnostico nem
    // validacao remota. A frase e registrada no log porque ausencia de envio precisa ser auditavel,
    // e nao apenas inferida da ausencia de linhas de rede.
    OPIT_LOG_INFO("CoreRuntime: nenhuma rota de saida de dados neste build - nada e enviado a site nenhum.");

    // 9. Snapshot inicial de metricas para apresentacao imediata na interface
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestSnapshot = m_systemMonitor.Sample();
        double fanTemp = 0.0;
        std::string fanSrc;
        uint32_t fanRpm = 0;
        bool hasFanRpm = false;
        if (FanControlManager::Instance().QueryBestAvailableTemperature(fanTemp, fanSrc, &fanRpm, &hasFanRpm)) {
            if (!m_latestSnapshot.hasTempSensor) {
                m_latestSnapshot.cpuTemperatureC = fanTemp;
                m_latestSnapshot.hasTempSensor = true;
                m_latestSnapshot.tempSensorSource = fanSrc;
            }
        }
        m_latestSnapshot.fanRpm = fanRpm;
        m_latestSnapshot.hasFanRpm = hasFanRpm;
        {
            auto gpu = GpuControlManager::Instance().GetStatus(0);
            if (gpu.measuredPowerWatts > 0) {
                m_latestSnapshot.gpuPowerWatts = static_cast<double>(gpu.measuredPowerWatts);
                m_latestSnapshot.hasGpuPowerW = true;
            }
        }
        PowerScheme activeScheme = m_powerMgr.GetActiveScheme();
        m_latestSnapshot.activePowerSchemeName = activeScheme.name;
        m_latestSnapshot.activePowerSchemeGuid = activeScheme.guid;
        m_latestSnapshot.activeProfileName = Config::ProfileToString(Config::Instance().GetProfile());
    }

    OPIT_LOG_INFO("CoreRuntime: Inicializacao concluida com sucesso");
    Start();
    return true;
}

void CoreRuntime::Start() {
    if (m_running.load()) return;

    m_running.store(true);
    m_state = State::Running;
    m_workerThread = std::thread(&CoreRuntime::WorkerLoop, this);
    OPIT_LOG_INFO("CoreRuntime: Motor em execucao em segundo plano");
}

void CoreRuntime::Stop() {
    if (!m_running.load()) return;

    OPIT_LOG_INFO("CoreRuntime: Parando servico de monitoramento...");
    m_running.store(false);
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_state = State::Stopping;
}

void CoreRuntime::Shutdown() {
    if (m_state == State::Stopped) return;

    Stop();
    OPIT_LOG_INFO("CoreRuntime: Restaurando configuracoes de energia e encerrando...");

    // Restaurar plano e limites originais
    ModeCoordinator::Instance().RestorePreviousState();
    m_powerMgr.RestoreOriginalSettings();

    // Restaurar controle de ventoinhas ao firmware
    FanControlManager::Instance().Shutdown();

    // Restaurar serviços modificados
    ServiceManager::Instance().RestoreModifiedServices();

    // Salvar configuração
    std::string cfgPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "opitcore.json");
    Config::Instance().Save(cfgPath);

    DatabaseManager::Instance().LogEvent("INFO", "CoreRuntime encerrado");
    DatabaseManager::Instance().Close();

    m_state = State::Stopped;
    OPIT_LOG_INFO("CoreRuntime: Encerrado.");
}

bool CoreRuntime::ApplyProfile(Config::Profile profile) {
    Config::Instance().SetProfile(profile);
    ApplyProfileSettings(profile);
    std::string cfgPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "opitcore.json");
    Config::Instance().Save(cfgPath);
    return true;
}

void CoreRuntime::ApplyUnifiedSettings() {
    auto& m = UnifiedSettingsModel::Instance();
    auto& adv = ModuleAdvancedSettings::Instance();

    if (!m.enableOrchestrator) {
        OPIT_LOG_INFO("CoreRuntime: Orquestrador desativado no Painel Unificado.");
        ServiceManager::Instance().RestoreModifiedServices();
        // Desligar o orquestrador para de gerenciar a maquina; nao reescreve o plano da usuaria
        // com os padroes de fabrica (5-100% / estacionamento 50%).
        OPIT_LOG_INFO("CoreRuntime: orquestrador desligado - plano ativo e limites de CPU mantem os valores do sistema.");
        return;
    }

    OPIT_LOG_INFO("CoreRuntime: Aplicando politicas ativas do Painel Unificado e Modulos Avancados...");

    // 1. Agrupamento svchost no Registro do Windows
    ElevationHelper::ApplySvchostGrouping(m.svchostGrouping);

    // 2. Planos de energia
    if (m.modPowerPlans) {
        if (adv.powerPlans.forceEcoPlan) {
            std::string appliedName, statusMsg;
            m_powerMgr.ForceEcoPlan(true, appliedName, statusMsg);
            OPIT_LOG_INFO("CoreRuntime: 'Forcar plano economico' ativo com precedencia maxima: " + statusMsg);
        } else {
            auto schemes = m_powerMgr.ListSchemes();
            std::string targetGuid = "";
            
            if (m.levelPowerPlans.autoSmartMode) {
                // Modo inteligente de planos: o plano e escolhido pelas bandas de uso de CPU declaradas pela
                // usuaria (0-35% Economico, 36-78% Balanceado, 79-100% Alto Desempenho), que o motor adaptativo
                // aplica a cada ciclo. Aqui nada e chutado por nivel, para nao atropelar a banda nem o plano
                // que ela escolheu na tela - inclusive quando o plano escolhido e o Economico.
                if (!adv.powerPlans.preferredSchemeAC.empty()) {
                    targetGuid = adv.powerPlans.preferredSchemeAC;
                } else {
                    OPIT_LOG_INFO("CoreRuntime: modo inteligente ativo - o plano segue as bandas de uso de CPU do motor adaptativo; nada foi forcado aqui.");
                }
            } else if (!adv.powerPlans.preferredSchemeAC.empty()) {
                // Manual: o plano escolhido por ela na tela avancada E o plano da maquina. Chutar um plano
                // por nivel por cima dessa escolha era o que fazia a janela de planos nao funcionar de acordo.
                targetGuid = adv.powerPlans.preferredSchemeAC;
            } else {
                std::string level = m.levelPowerPlans.level;
                for (const auto& s : schemes) {
                    std::string lower = StringUtils::ToLower(s.name);
                    if ((level == "min" || level == "med") && (lower.find("equilibrado") != std::string::npos || lower.find("balanced") != std::string::npos)) {
                        targetGuid = s.guid;
                        break;
                    } else if ((level == "max" || level == "ultra") && (lower.find("alto desempenho") != std::string::npos || lower.find("high") != std::string::npos)) {
                        targetGuid = s.guid;
                        break;
                    }
                }
            }
            if (!targetGuid.empty()) {
                m_powerMgr.SetActiveScheme(targetGuid);
            }
        }
    }

    // 3. Min/Max CPU via Powrprof (Preserva integralmente a configuracao individual salva de cada plano)
    if (m.modCpuMinMax) {
        std::string ecoGuid = m_powerMgr.FindEcoScheme();
        std::string balGuid = m_powerMgr.FindBalancedScheme();
        std::string highGuid = m_powerMgr.FindHighPerformanceScheme();
        if (!ecoGuid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(ecoGuid,
                adv.cpuMinMax.ecoPlan.minPercentAC, adv.cpuMinMax.ecoPlan.maxPercentAC,
                adv.cpuMinMax.ecoPlan.minPercentDC, adv.cpuMinMax.ecoPlan.maxPercentDC);
        }
        if (!balGuid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(balGuid,
                adv.cpuMinMax.balancedPlan.minPercentAC, adv.cpuMinMax.balancedPlan.maxPercentAC,
                adv.cpuMinMax.balancedPlan.minPercentDC, adv.cpuMinMax.balancedPlan.maxPercentDC);
        }
        if (!highGuid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(highGuid,
                adv.cpuMinMax.highPerfPlan.minPercentAC, adv.cpuMinMax.highPerfPlan.maxPercentAC,
                adv.cpuMinMax.highPerfPlan.minPercentDC, adv.cpuMinMax.highPerfPlan.maxPercentDC);
        }

        PowerScheme activeSch = m_powerMgr.GetActiveScheme();
        const PlanThrottleSettings activeThrottle = adv.cpuMinMax.GetPlanThrottle(activeSch.guid.empty() ? activeSch.name : activeSch.guid);
        // Escreve o plano ativo pelas duas linhas, como fez com os outros tres acima. O caminho antigo
        // usava SetProcessorThrottleRange, que grava o par AC em cima da linha da bateria e desfazia
        // na mesma passada o teto de bateria que a usuaria acabou de salvar.
        if (!activeSch.guid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(activeSch.guid,
                activeThrottle.minPercentAC, activeThrottle.maxPercentAC,
                activeThrottle.minPercentDC, activeThrottle.maxPercentDC);
        }
        const BatteryStatusInfo batAtivo = m_powerMgr.GetBatteryStatus();
        const bool onBattery = batAtivo.hasBattery && !batAtivo.isAcOnline;
        Config::Instance().SetProcessorThrottleCap(activeThrottle.Max(onBattery));
        OPIT_LOG_INFO("CoreRuntime: Limites Min/Max CPU aplicados no plano ativo [" +
                      std::to_string(activeThrottle.minPercentAC) + "%-" + std::to_string(activeThrottle.maxPercentAC) +
                      "% na tomada, " + std::to_string(activeThrottle.minPercentDC) + "%-" + std::to_string(activeThrottle.maxPercentDC) +
                      "% na bateria]; vigentes agora " + std::to_string(activeThrottle.Min(onBattery)) + "%-" +
                      std::to_string(activeThrottle.Max(onBattery)) + "% (" + (onBattery ? "bateria" : "tomada") + ")");
    }

    // 3.5. Dispositivos ociosos no modo Economico (USB Suspend e PCIe ASPM)
    m_powerMgr.SetIdleDeviceSuspensionEnabled(m.pauseIdleDevicesEco);
    std::string ecoSchemeGuid = m_powerMgr.FindEcoScheme();
    if (!ecoSchemeGuid.empty()) {
        m_powerMgr.ConfigureIdleDeviceSuspension(ecoSchemeGuid, m.pauseIdleDevicesEco);
    }

    // 4. Core Parking (Respeita a configuracao do usuario para pausar nucleos)
    if (m.modCoreParking) {
        uint32_t parkingPct = adv.coreParking.minCoresActiveAC;
        if (parkingPct < 5) parkingPct = 5;
        if (parkingPct > 100) parkingPct = 100;
        m_powerMgr.SetCoreParking(parkingPct);
        OPIT_LOG_INFO("CoreRuntime: Core Parking aplicado (" + std::to_string(parkingPct) + "% de nucleos ativos)");
    }

    // 4.5. Telemetria da Microsoft (Opt-in do usuário)
    ServiceManager::Instance().SetMicrosoftTelemetryDisabled(m.disableMicrosoftTelemetry);

    // 5. Economia profunda
    if (m.modDeepEco) {
        BatteryStatusInfo bat = m_powerMgr.GetBatteryStatus();
        if (bat.hasBattery && !bat.isAcOnline) {
            uint32_t dcMax = (m.levelCpuMinMax.autoSmartMode) ? adv.cpuMinMax.maxPercentDC : 60;
            uint32_t dcParking = (m.levelCoreParking.autoSmartMode) ? adv.coreParking.minCoresActiveDC : 100;
            // No login sob bateria, SO a linha da bateria muda: SetProcessorThrottleRange gravava
            // (5, dcMax) tambem na linha da tomada do plano ativo e achatava o teto AC da usuaria.
            PowerScheme activeSch = m_powerMgr.GetActiveScheme();
            if (!activeSch.guid.empty()) {
                uint32_t minAc = 0, maxAc = 0, minDc = 0, maxDc = 0;
                if (m_powerMgr.ReadSchemeProcessorThrottle(activeSch.guid, minAc, maxAc, minDc, maxDc)) {
                    m_powerMgr.WriteSchemeProcessorThrottle(activeSch.guid, minAc, maxAc, 5, dcMax);
                } else {
                    OPIT_LOG_WARN("CoreRuntime: Economia Profunda sem leitura do plano ativo; aplicando (5-" +
                                  std::to_string(dcMax) + "%) nas duas linhas como ultimo recurso.");
                    m_powerMgr.WriteSchemeProcessorThrottle(activeSch.guid, 5, dcMax, 5, dcMax);
                }
                uint32_t parkAc = 0, parkDc = 0;
                if (m_powerMgr.ReadCoreParking(activeSch.guid, parkAc, parkDc)) {
                    m_powerMgr.SetCoreParkingLines(activeSch.guid, parkAc, dcParking);
                } else {
                    m_powerMgr.SetCoreParking(dcParking);
                }
            } else {
                m_powerMgr.SetProcessorThrottleRange(5, dcMax);
                m_powerMgr.SetCoreParking(dcParking);
            }
            OPIT_LOG_INFO("CoreRuntime: Economia Profunda ativada sob bateria (CPU bateria 5-" + std::to_string(dcMax) +
                          "%; linha da tomada preservada; nucleos na bateria " + std::to_string(dcParking) + "%).");
        }
    }

    // 6. Guardião térmico
    if (m.modThermalGuardian) {
        double limitC = 80.0;
        if (m.levelThermalGuardian.autoSmartMode) {
            limitC = adv.thermal.alertLimitC;
        } else {
            if (m.levelThermalGuardian.level == "min") limitC = 90.0;
            else if (m.levelThermalGuardian.level == "med") limitC = 85.0;
            else if (m.levelThermalGuardian.level == "max") limitC = 80.0;
            else if (m.levelThermalGuardian.level == "ultra") limitC = 75.0;
        }
        Config::Instance().SetThermalLimitC(limitC);
    }
}

void CoreRuntime::ApplyProfileSettings(Config::Profile profile) {
    auto schemes = m_powerMgr.ListSchemes();
    std::string targetSchemeGuid = "";

    // Mapeamento determinístico de perfil para planos nativos do Windows
    for (const auto& s : schemes) {
        std::string lower = StringUtils::ToLower(s.name);
        if (profile == Config::Profile::Eco && (lower.find("economia") != std::string::npos || lower.find("saver") != std::string::npos)) {
            targetSchemeGuid = s.guid;
            break;
        } else if (profile == Config::Profile::HighPerformance && (lower.find("alto desempenho") != std::string::npos || lower.find("high") != std::string::npos)) {
            targetSchemeGuid = s.guid;
            break;
        } else if (profile == Config::Profile::Balanced && (lower.find("equilibrado") != std::string::npos || lower.find("balanced") != std::string::npos)) {
            targetSchemeGuid = s.guid;
            break;
        }
    }

    if (!targetSchemeGuid.empty()) {
        m_powerMgr.SetActiveScheme(targetSchemeGuid);
    }

    // Aplica o limite de frequencia da CPU configurado pelo usuario especificamente para o plano correspondente
    auto& adv = ModuleAdvancedSettings::Instance();
    PlanThrottleSettings throttle;
    if (profile == Config::Profile::Eco) {
        throttle = adv.cpuMinMax.ecoPlan;
    } else if (profile == Config::Profile::Balanced) {
        throttle = adv.cpuMinMax.balancedPlan;
    } else {
        throttle = adv.cpuMinMax.highPerfPlan;
    }
    // Reaplica no plano que foi ativado, cada linha com seu valor. SetProcessorThrottleRange grava o
    // par AC tambem na linha DC, o que na bateria punha o teto da tomada no lugar do teto mais baixo.
    const std::string throttleTarget = !targetSchemeGuid.empty() ? targetSchemeGuid : m_powerMgr.GetActiveScheme().guid;
    if (!throttleTarget.empty()) {
        m_powerMgr.WriteSchemeProcessorThrottle(throttleTarget, throttle.minPercentAC, throttle.maxPercentAC,
                                                throttle.minPercentDC, throttle.maxPercentDC);
    }
    const BatteryStatusInfo batPerfil = m_powerMgr.GetBatteryStatus();
    const bool onBatteryPerfil = batPerfil.hasBattery && !batPerfil.isAcOnline;
    const uint32_t minPct = throttle.Min(onBatteryPerfil);
    const uint32_t maxPct = throttle.Max(onBatteryPerfil);
    Config::Instance().SetProcessorThrottleCap(maxPct);

    // ALTERA APENAS O QUE DESLIGA E LIGA DE NUCLEOS (CORE PARKING)
    uint32_t parkingPct = 100;
    if (profile == Config::Profile::Eco) {
        parkingPct = (adv.coreParking.minCoresActiveAC > 0 && adv.coreParking.minCoresActiveAC <= 25) ?
                      adv.coreParking.minCoresActiveAC : 10;
    } else if (profile == Config::Profile::Balanced) {
        parkingPct = 50;
    } else {
        parkingPct = 100;
    }
    m_powerMgr.SetCoreParking(parkingPct);

    OPIT_LOG_INFO("Perfil aplicado: " + Config::ProfileToString(profile) +
                  " (CPU: " + std::to_string(minPct) + "% - " + std::to_string(maxPct) + "% na " +
                  (onBatteryPerfil ? "bateria" : "tomada") +
                  ", Nucleos Minimos Desestacionados: " + std::to_string(parkingPct) + "%)");
}

static uint64_t SteadyNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void CoreRuntime::WorkerLoop() {
    int sampleCounter = 0;
    uint64_t lastProcessCycleMs = 0;
    uint64_t lastShieldAuditMs = 0;
    uint64_t lastManifestReverifyMs = 0;
    uint64_t frameLogLastMs = 0;

    while (m_running.load()) {
        int interval = Config::Instance().GetSampleIntervalMs();
        if (interval < 250) interval = 250;
        if (interval > 5000) interval = 5000;

        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        if (!m_running.load()) break;
        // Rede de seguranca do motor: uma excecao escapando desta thread abortava o processo
        // silenciosamente (terminate->abort, fail-fast 0xC0000409 sem WER). Capturar, logar o what()
        // e continuar mantem o motor vivo e da o nome real da excecao para fechar o bug de origem.
        try {
        DatabaseManager::Instance().LogEvent("DEBUG", "RL:s"); // temporario: localizacao de morte silenciosa do worker

        // 0. Seguranca apenas informativa, a cada ~60 s: o escudo anti-depurador e, a cada ~10 min,
        // a re-verificacao do manifesto assinado em disco (cobre DLLs e arquivos alterados depois do
        // arranque — o escudo de .text so cobre o EXE em memoria). Os dois lados registram no log o
        // que veem e NAO bloqueiam nada: o programa e donationware e roda sempre.
        uint64_t nowSteadyMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (lastShieldAuditMs == 0 || nowSteadyMs - lastShieldAuditMs >= 60000) {
            lastShieldAuditMs = nowSteadyMs;
            if (!security::OpitSecurityManager::Instance().AuditRuntimeShield()) {
                Logger::Instance().Log(LogLevel::Warning, "security",
                    "CoreRuntime: escudo registrou depurador ou patch na .text - nada foi bloqueado, o programa continua funcionando.");
            }
            if (lastManifestReverifyMs == 0 || nowSteadyMs - lastManifestReverifyMs >= 600000) {
                lastManifestReverifyMs = nowSteadyMs;
                if (!security::OpitSecurityManager::Instance().VerifyInstallationIntegrity()) {
                    Logger::Instance().Log(LogLevel::Warning, "security",
                        "CoreRuntime: manifesto de integridade nao confirmou os arquivos da instalacao - registrado, nada foi bloqueado.");
                }
            }
        }

        // 1. Amostrar métricas de hardware
        SystemMetricsSnapshot snap = m_systemMonitor.Sample();
        PowerScheme activeScheme = m_powerMgr.GetActiveScheme();
        snap.activePowerSchemeName = activeScheme.name;
        snap.activePowerSchemeGuid = activeScheme.guid;
        snap.activeProfileName = Config::ProfileToString(Config::Instance().GetProfile());

        auto& unified = UnifiedSettingsModel::Instance();
        auto& adv = ModuleAdvancedSettings::Instance();

        // 1.5. Atualizar telemetria de ventoinhas e sensores de hardware (LHM / NVAPI / ADL / ACPI)
        bool activeFanControl = (unified.enableOrchestrator && unified.modFanCurve);
        if (activeFanControl) {
            FanControlManager::Instance().ProcessPeriodicTick();
        } else {
            FanControlManager::Instance().RefreshTelemetryOnly();
        }
        {
            double fanTemp = 0.0;
            std::string fanSrc;
            uint32_t fanRpm = 0;
            bool hasFanRpm = false;
            if (FanControlManager::Instance().QueryBestAvailableTemperature(fanTemp, fanSrc, &fanRpm, &hasFanRpm)) {
                if (!snap.hasTempSensor) {
                    snap.cpuTemperatureC = fanTemp;
                    snap.hasTempSensor = true;
                    snap.tempSensorSource = fanSrc;
                }
            }
            snap.fanRpm = fanRpm;
            snap.hasFanRpm = hasFanRpm;
        }

        // Potencia real da GPU medida pelo driver do fabricante (onde houver sensor)
        {
            auto gpu = GpuControlManager::Instance().GetStatus(0);
            if (gpu.measuredPowerWatts > 0) {
                snap.gpuPowerWatts = static_cast<double>(gpu.measuredPowerWatts);
                snap.hasGpuPowerW = true;
            }
        }

        // Evidencia periodica (1/min) da funcao-objetivo em execucao: valor medido ou
        // indisponibilidade declarada — nunca valor inventado.
        if (frameLogLastMs == 0 || nowSteadyMs - frameLogLastMs >= 60000) {
            frameLogLastMs = nowSteadyMs;
            std::ostringstream obj;
            obj << "Metrics funcao-objetivo: frameTime=";
            if (snap.hasFrameTime && snap.frameTimeMs > 0.0) {
                obj << std::fixed << std::setprecision(1) << snap.frameTimeMs << "ms ("
                    << std::setprecision(0) << (1000.0 / snap.frameTimeMs) << " fps)";
            } else {
                obj << "indisponivel";
            }
            obj << " gpuPowerW=";
            if (snap.hasGpuPowerW) {
                obj << std::fixed << std::setprecision(1) << snap.gpuPowerWatts;
            } else {
                obj << "indisponivel";
            }
            OPIT_LOG_INFO(obj.str());
        }

        // 2. Execução ativa do orquestrador (quando ligado no Painel Unificado)
        if (unified.enableOrchestrator) {

            // A. Detecção de Jogos / Modo Inteligente Integrado ao ModeCoordinator
            if (unified.smartOrchestrator || Config::Instance().IsAutoGameModeEnabled()) {
                auto processes = ProcessManager::EnumerateProcesses();
                std::vector<std::string> procNames;
                procNames.reserve(processes.size());
                for (const auto& p : processes) {
                    procNames.push_back(p.name);
                }

                std::string detectedGame = m_gameDetector.CheckRunningGame(procNames);
                auto currentActiveGames = m_gameDetector.GetActiveGames();
                std::set<std::string> currentSet;
                for (const auto& g : currentActiveGames) {
                    currentSet.insert(StringUtils::ToLower(StringUtils::Trim(g)));
                }

                // Notifica início de novos jogos
                for (const auto& g : currentSet) {
                    if (m_lastActiveGames.find(g) == m_lastActiveGames.end()) {
                        std::string outMsg;
                        ModeCoordinator::Instance().OnGameStarted(g, outMsg);
                        DatabaseManager::Instance().LogEvent("INFO", outMsg);
                    }
                }

                // Notifica encerramento de jogos (com preservação de sessões simultâneas)
                for (const auto& g : m_lastActiveGames) {
                    if (currentSet.find(g) == currentSet.end()) {
                        std::string outMsg;
                        ModeCoordinator::Instance().OnGameStopped(g, outMsg);
                        DatabaseManager::Instance().LogEvent("INFO", outMsg);
                    }
                }
                m_lastActiveGames = currentSet;

                if (ModeCoordinator::Instance().IsGameModeActive()) {
                    snap.isGameRunning = true;
                    snap.runningGameName = !detectedGame.empty() ? detectedGame : "Modo Jogo Ativo";
                } else {
                    snap.isGameRunning = false;
                    snap.runningGameName = "";
                }
            }
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:a"); // temporario

            // B. Guardião Térmico Ativo
            if (unified.modThermalGuardian || Config::Instance().IsThermalProtectionEnabled()) {
                double limitC = (unified.levelThermalGuardian.autoSmartMode) ? adv.thermal.alertLimitC :
                                (unified.levelThermalGuardian.level == "min") ? 90.0 :
                                (unified.levelThermalGuardian.level == "med") ? 85.0 :
                                (unified.levelThermalGuardian.level == "max") ? 80.0 : 75.0;

                ThermalProtection::Action action = m_thermalProtection.Evaluate(snap.cpuTemperatureC, snap.hasTempSensor, limitC);

                if (action == ThermalProtection::Action::Throttle) {
                    snap.isThermalThrottling = true;
                    uint32_t cap = (unified.levelThermalGuardian.autoSmartMode) ? adv.thermal.throttleActionCap : 70;
                    m_powerMgr.SetProcessorThrottleLimit(cap);
                    OPIT_LOG_WARN("PROTECAO TERMICA ATIVADA! Temp: " + std::to_string((int)snap.cpuTemperatureC) + 
                                  "C >= Limite: " + std::to_string((int)limitC) + "C. CPU reduzida para " + std::to_string(cap) + "%.");
                    DatabaseManager::Instance().LogEvent("WARN", "Protecao termica ativada: CPU reduzida");
                } else if (action == ThermalProtection::Action::Restore) {
                    snap.isThermalThrottling = false;
                    ApplyUnifiedSettings();
                    OPIT_LOG_INFO("Temperatura normalizada. Limite de CPU restaurado.");
                    DatabaseManager::Instance().LogEvent("INFO", "Temperatura normalizada");
                } else {
                    snap.isThermalThrottling = m_thermalProtection.IsThrottling();
                }
            }
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:b"); // temporario

            // C. Serviços Windows em 2o plano (avaliado a cada 10 ciclos para evitar thrashing no SCM)
            if (unified.modWindowsServices && (sampleCounter % 10 == 0)) {
                DatabaseManager::Instance().LogEvent("DEBUG", "RL:c1"); // temporario
                // UMA porta para a automacao de servicos. Antes havia tres atores no mesmo tick
                // (EvaluateAdvancedTriggers + ApplyServiceLevel + ApplySmartGamingAndEcoServices) mais o par
                // do modPauseIndexingIdle: um parava o Spooler, outro o restaurava e a restauracao apagava o
                // cooldown - vaivem medido de 15 eventos em 3 minutos. O reconciliador decide tudo de uma vez.
                BatteryStatusInfo bat = m_powerMgr.GetBatteryStatus();
                DatabaseManager::Instance().LogEvent("DEBUG", "RL:c1b"); // temporario
                bool isBatteryOrEco = (bat.hasBattery && !bat.isAcOnline) || unified.modDeepEco;
                ServiceManager::Instance().RunServiceCycleFromSystem(
                    snap.isGameRunning, isBatteryOrEco, snap.cpuUsagePercent,
                    unified.levelServices.level, unified.subSmartServices);
                DatabaseManager::Instance().LogEvent("DEBUG", "RL:c1c"); // temporario
            }
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:c"); // temporario
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:d1"); // temporario

            // D. Prioridade de Apps e Controle de Processos
            if (unified.modAppPriority || unified.modProcessControl) {
                if (adv.processes.autoProcessManagement) {
                    // O mapa de janelas passou a ser UMA passada de EnumWindows para todos os PIDs (antes
                    // era uma varredura por processo, 177 por ciclo), entao o auto cabe a cada 3 amostras.
                    if (sampleCounter % 3 == 0) {
                        auto procs = ProcessManager::EnumerateProcesses();
                        auto windowStates = ProcessManager::BuildWindowStateMap();

                        const uint64_t cycleMs = SteadyNowMs();
                        ProcessGovernorInput gin;
                        gin.nowMs = cycleMs;
                        gin.elapsedMs = (lastProcessCycleMs == 0)
                                            ? static_cast<uint64_t>(interval)
                                            : (cycleMs - lastProcessCycleMs);
                        lastProcessCycleMs = cycleMs;
                        gin.isGameRunning = snap.isGameRunning;
                        auto activeGames = m_gameDetector.GetActiveGames();
                        gin.gameProcessName = activeGames.empty() ? std::string() : activeGames.front();
#ifdef _WIN32
                        gin.selfPid = GetCurrentProcessId();
#else
                        gin.selfPid = static_cast<uint32_t>(getpid());
#endif
                        gin.excludedList = adv.processes.excludedProcesses;

                        ProcessGovernorSettings gs;
                        gs.prioMaximized = adv.processes.prioMaximized;
                        gs.prioNormal = adv.processes.prioNormal;
                        gs.prioMinimized = adv.processes.prioMinimized;
                        gs.prioBackground = adv.processes.prioBackground;
                        gs.prioSystemTray = adv.processes.prioSystemTray;
                        gs.prioNoVisibleWindow = adv.processes.prioNoVisibleWindow;
                        gs.prioGame = adv.processes.gamePriorityClass;
                        gs.protectCriticalProcesses = adv.processes.protectCriticalProcesses;
                        gs.preserveAudioVideoTasks = adv.processes.preserveAudioVideoTasks;
                        gs.trimWorkingSetIdle = adv.processes.trimWorkingSetIdle;

                        ProcessGovernor::Instance().RunCycle(procs, windowStates, gin, gs);
                    }
                } else {
                    // Modo manual ou foco primario
#ifdef _WIN32
                    HWND fgWnd = GetForegroundWindow();
                    if (fgWnd) {
                        DWORD fgPid = 0;
                        GetWindowThreadProcessId(fgWnd, &fgPid);
                        if (fgPid > 4 && fgPid != GetCurrentProcessId()) {
#else
                    // A janela frontal em Linux vem do X11 (XGetInputFocus + _NET_WM_PID), pelo
                    // mesmo caminho que o motor adaptativo ja usa; sem X disponivel o pid vem 0
                    // e esta rota nao escreve prioridade nenhuma, em vez de chutar um processo.
                    AppIdentity fgApp = AppProfileManager::Instance().IdentifyForegroundWindow();
                    if (fgApp.pid > 4) {
                        uint32_t fgPid = fgApp.pid;
                        if (fgPid != static_cast<uint32_t>(getpid())) {
#endif
                            auto procId = AppProfileManager::Instance().IdentifyProcess(fgPid);
                            if (!ProcessManager::IsCriticalProcess(procId.processName) && !adv.processes.IsProcessExcluded(procId.processName)) {
                                uint32_t prio = ABOVE_NORMAL_PRIORITY_CLASS;
                                if (unified.levelProcesses.autoSmartMode) {
                                    prio = snap.isGameRunning ? adv.processes.gamePriorityClass : adv.processes.foregroundPriorityClass;
                                } else {
                                    if (snap.isGameRunning || unified.levelProcesses.level == "max" || unified.levelProcesses.level == "ultra") {
                                        prio = HIGH_PRIORITY_CLASS;
                                    } else if (unified.levelProcesses.level == "min") {
                                        prio = NORMAL_PRIORITY_CLASS;
                                    }
                                }
                                ProcessManager::SetPriority(fgPid, prio);
                                ProcessManager::SetEcoQoS(fgPid, false);
                            }
                        }
                    }
                }
            }
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:d2"); // temporario

            // E. Motor de Inteligência Adaptativa e Otimização Controlada
            AdaptiveEngine::Instance().ProcessTick(snap, m_powerMgr, ServiceManager::Instance());
            DatabaseManager::Instance().LogEvent("DEBUG", "RL:e"); // temporario

            // F. Otimizacao Estrategica Periodica por IA em segundo plano
            if (sampleCounter % 10 == 0) {
                AiAssistantManager::Instance().TickPeriodicOptimization(snap);
            }
        }

        // 3. Atualizar snapshot thread-safe
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestSnapshot = snap;
        }

        // 4. Gravação periódica de métricas no SQLite (a cada 10 amostras)
        sampleCounter++;
        if (sampleCounter >= 10) {
            sampleCounter = 0;
            DatabaseManager::Instance().RecordMetric(snap.cpuUsagePercent, snap.ramUsagePercent, snap.cpuTemperatureC);
        }
        DatabaseManager::Instance().LogEvent("DEBUG", "RL:f"); // temporario
        } catch (const std::exception& e) {
            OPIT_LOG_ERROR(std::string("WorkerLoop: excecao do ciclo capturada (motor continua vivo): ") + e.what());
            DatabaseManager::Instance().LogEvent("ERROR", std::string("WorkerLoop: excecao do ciclo capturada: ") + e.what());
        } catch (...) {
            OPIT_LOG_ERROR("WorkerLoop: excecao desconhecida do ciclo capturada (motor continua vivo).");
            DatabaseManager::Instance().LogEvent("ERROR", "WorkerLoop: excecao desconhecida do ciclo capturada");
        }
    }
}

SystemMetricsSnapshot CoreRuntime::GetMetricsSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestSnapshot;
}

SystemHardwareInfo CoreRuntime::GetHardwareInfo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hardwareInfo;
}

} // namespace opitcore
