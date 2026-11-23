// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "ModeCoordinator.h"
#include "core/Logger.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/NotificationManager.h"
#include "utils/StringUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

// Wake lock do modo de desempenho. No Windows e a mesma chamada de sempre; em Linux o
// equivalente e um inibidor do logind: systemd-inhibit segura o fd de Inhibit() enquanto o
// seu processo vive, por isso o pedido existe enquanto o coordernador o quiser e desaparece
// com ele. Sem binario, sem bus ou sem permissao, avisamos e nao fingimos que bloqueamos —
// o mesmo criterio dos outros botoes que o kernel nao expoe.
void ApplyKeepAwake(bool on) {
#ifdef _WIN32
    const DWORD keepAwake = ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED;
    const DWORD release = ES_CONTINUOUS;
    if (on) { SetThreadExecutionState(keepAwake); } else { SetThreadExecutionState(release); }
#else
    static std::mutex s_mtx;
    static pid_t s_inhibitPid = 0;
    std::lock_guard<std::mutex> lock(s_mtx);
    if (on) {
        if (s_inhibitPid > 0) {
            int status = 0;
            if (waitpid(s_inhibitPid, &status, WNOHANG) == 0) return; // inibidor ainda de pe
            s_inhibitPid = 0; // morreu (logind recusou ou o bus caiu): tenta de novo e avisa
        }
        int fds[2];
        if (pipe(fds) != 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "Wake lock indisponivel: pipe falhou (%s).", strerror(errno));
            OPIT_LOG_WARN(msg);
            return;
        }
        // O exec fecha a ponta de escrita; sem esta bandeira o pai esperaria ate a saida do
        // systemd-inhibit em vez de receber EOF no instante em que o lancamento correu bem.
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        pid_t child = fork();
        if (child < 0) {
            close(fds[0]);
            close(fds[1]);
            char msg[160];
            snprintf(msg, sizeof(msg), "Wake lock indisponivel: fork falhou (%s).", strerror(errno));
            OPIT_LOG_WARN(msg);
            return;
        }
        if (child == 0) {
            close(fds[0]);
            setpgid(0, 0); // o sleep que o systemd-inhibit lanca herda o grupo: fechar o wake
                           // lock tem de matar os dois, senao fica um sleep 65h pendurado
            execlp("systemd-inhibit", "systemd-inhibit",
                   "--who=Opit-Core", "--why=Modo de desempenho", "--what=idle:sleep",
                   "--mode=block", "sleep", "infinity", static_cast<char*>(nullptr));
            int e = errno;
            ssize_t wrote = write(fds[1], &e, sizeof(e));
            (void)wrote;
            _exit(127);
        }
        close(fds[1]);
        int childErrno = 0;
        ssize_t rd = read(fds[0], &childErrno, sizeof(childErrno));
        close(fds[0]);
        if (rd == static_cast<ssize_t>(sizeof(childErrno))) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "Wake lock indisponivel: systemd-inhibit nao lancou (%s).", strerror(childErrno));
            OPIT_LOG_WARN(msg);
            int status = 0;
            waitpid(child, &status, 0);
            return;
        }
        s_inhibitPid = child;
        OPIT_LOG_INFO("Wake lock (logind) ativado: suspensao e inatividade bloqueadas.");
        return;
    }
    if (s_inhibitPid > 0) {
        kill(-s_inhibitPid, SIGTERM);
        kill(s_inhibitPid, SIGTERM);
        int status = 0;
        waitpid(s_inhibitPid, &status, 0);
        s_inhibitPid = 0;
        OPIT_LOG_INFO("Wake lock (logind) liberado: estado anterior restaurado.");
    }
#endif
}

} // namespace

namespace opitcore {

ModeCoordinator& ModeCoordinator::Instance() {
    static ModeCoordinator instance;
    return instance;
}

ModeCoordinator::ModeCoordinator() = default;
ModeCoordinator::~ModeCoordinator() {
    RestorePreviousState();
}

bool ModeCoordinator::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    m_powerMgr.Initialize();
    GpuControlManager::Instance().Initialize();
    m_initialized = true;
    return true;
}

void ModeCoordinator::CaptureCurrentState(SavedSystemState& state) {
    auto currentScheme = m_powerMgr.GetActiveScheme();
    state.powerSchemeGuid = currentScheme.guid;

    auto& adv = ModuleAdvancedSettings::Instance();
    state.cpuMinPercent = adv.cpuMinMax.minPercentAC;
    state.cpuMaxPercent = adv.cpuMinMax.maxPercentAC;

    // O snapshot que devolve a maquina ao estado anterior tem que vir DA MAQUINA. O valor da tela de
    // configuracoes so vale como fallback, porque a tela pode estar mais antiga que o powrprof.
    uint32_t minAc = 0, maxAc = 0, minDc = 0, maxDc = 0;
    if (!currentScheme.guid.empty() &&
        m_powerMgr.ReadSchemeProcessorThrottle(currentScheme.guid, minAc, maxAc, minDc, maxDc)) {
        state.cpuMinPercent = minAc;
        state.cpuMaxPercent = maxAc;
        state.cpuMinPercentDC = minDc;
        state.cpuMaxPercentDC = maxDc;
        state.throttleCaptured = true;
    }

    uint32_t parkingAc = 0, parkingDc = 0;
    if (!currentScheme.guid.empty() && m_powerMgr.ReadCoreParking(currentScheme.guid, parkingAc, parkingDc)) {
        state.coreParkingAc = parkingAc;
        state.coreParkingDc = parkingDc;
        state.coreParkingCaptured = true;
    }

    auto gpuStatus = GpuControlManager::Instance().GetStatus(0);
    state.gpuProfile = gpuStatus.currentProfile;
    state.gpuPowerLimitPercent = gpuStatus.appliedPowerLimitPercent;

    state.diskPowerDownPrevented = m_powerMgr.IsDiskPowerDownPrevented();
    state.systemSleepPrevented = false;
    state.isValid = true;
}

void ModeCoordinator::ApplySystemState(const SavedSystemState& state) {
    if (!state.isValid) return;

    if (!state.powerSchemeGuid.empty()) {
        m_powerMgr.SetActiveScheme(state.powerSchemeGuid);
    }

    auto& adv = ModuleAdvancedSettings::Instance();
    if (state.throttleCaptured && !state.powerSchemeGuid.empty()) {
        m_powerMgr.WriteSchemeProcessorThrottle(state.powerSchemeGuid, state.cpuMinPercent, state.cpuMaxPercent,
                                                state.cpuMinPercentDC, state.cpuMaxPercentDC);
    } else {
        m_powerMgr.SetProcessorThrottleRange(adv.cpuMinMax.minPercentAC, adv.cpuMinMax.maxPercentAC);
    }

    if (state.coreParkingCaptured) {
        // As APIs de escrita gravam AC e DC juntos: com os dois diferentes, devolver só o AC destruiria o DC
        // escolhido pela usuaria. Nesse caso o caminho honesto e avisar em vez de escrever um valor chutado.
        if (state.coreParkingAc != state.coreParkingDc) {
            OPIT_LOG_WARN("[COORDINATOR] core parking assimetrico (AC=" + std::to_string(state.coreParkingAc) +
                          "% DC=" + std::to_string(state.coreParkingDc) + "%): restauracao individual nao suportada pela escrita atual.");
        } else if (!state.powerSchemeGuid.empty()) {
            m_powerMgr.SetCoreParkingForScheme(state.powerSchemeGuid, state.coreParkingAc);
        } else {
            m_powerMgr.SetCoreParking(state.coreParkingAc);
        }
    } else {
        uint32_t restoreParking = (adv.coreParking.minCoresActiveAC > 0) ? adv.coreParking.minCoresActiveAC : 50;
        m_powerMgr.SetCoreParking(restoreParking);
    }

    std::string gpuMsg;
    GpuControlManager::Instance().ApplyProfile(0, state.gpuProfile, gpuMsg);

    m_powerMgr.PreventDiskPowerDown(state.diskPowerDownPrevented);

    if (state.systemSleepPrevented) {
        ApplyKeepAwake(true);
    } else {
        ApplyKeepAwake(false);
    }
}

bool ModeCoordinator::SetDeepEcoMode(bool enable, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Hierarquia de prioridade: Sessão de jogo supera Economia Máxima
    if (enable && (!m_activeGames.empty() || m_gameModeActive)) {
        outMsg = "Economia Maxima rejeitada: sessao de jogo ativa possui prioridade sobre economia de energia.";
        return false;
    }

    if (enable) {
        if (m_deepEcoActive) {
            outMsg = "Modo Economia Maxima ja se encontra ativo.";
            return true;
        }

        CaptureCurrentState(m_preDeepEcoState);

        // 1. Aplica plano de economia do Windows
        std::string ecoGuid = m_powerMgr.FindEcoScheme();
        if (!ecoGuid.empty()) {
            m_powerMgr.SetActiveScheme(ecoGuid);
        }

        // 2. Preserva rigorosamente limites de CPU do plano de Economia configurado pelo usuario.
        //    Escreve AC e DC cada qual com sua linha: SetProcessorThrottleRange grava o mesmo numero
        //    nas duas, e na bateria isso poria o teto da tomada no lugar do teto mais baixo pedido.
        auto& adv = ModuleAdvancedSettings::Instance();
        const PlanThrottleSettings ecoLimits = adv.cpuMinMax.ecoPlan;
        if (!ecoGuid.empty()) {
            if (!m_powerMgr.WriteSchemeProcessorThrottle(ecoGuid, ecoLimits.minPercentAC, ecoLimits.maxPercentAC,
                                                         ecoLimits.minPercentDC, ecoLimits.maxPercentDC)) {
                OPIT_LOG_WARN("[COORDENADOR] Limites de CPU do plano economico nao puderam ser reaplicados em " + ecoGuid + ".");
            }
        } else {
            OPIT_LOG_WARN("[COORDENADOR] Esta maquina nao tem plano economico; os limites de CPU nao foram escritos em outro plano.");
        }

        // 2.5. Pausa/desliga núcleos ociosos no modo Economia (Core parking em 10% - 90% podem pausar)
        uint32_t ecoParking = (adv.coreParking.minCoresActiveAC > 0 && adv.coreParking.minCoresActiveAC <= 25) ?
                              adv.coreParking.minCoresActiveAC : 10;
        m_powerMgr.SetCoreParking(ecoParking);

        // 3. Aplica perfil de economia na GPU
        std::string gpuMsg;
        GpuControlManager::Instance().ApplyProfile(0, GpuPowerProfile::Eco, gpuMsg);

        // 4. Preserva integralmente discos, controladores, USBs e servicos essenciais
        // (NUNCA desliga HDs sem necessidade nem suspende dispositivos arbitrariamente)
        m_deepEcoActive = true;
        outMsg = "Modo Economia Maxima ativado. Consumo reduzido sem interrupcao de perifericos ou seguranca.";
        OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
        NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                               "Economia Maxima ativada", outMsg);
        return true;
    } else {
        if (!m_deepEcoActive) {
            outMsg = "Modo Economia Maxima nao estava ativo.";
            return true;
        }

        ApplySystemState(m_preDeepEcoState);
        m_deepEcoActive = false;
        outMsg = "Modo Economia Maxima desativado. Estado anterior restaurado com sucesso.";
        OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
        NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                               "Economia Maxima desativada", outMsg);
        return true;
    }
}

bool ModeCoordinator::SetManualGameMode(bool enable, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (enable) {
        if (m_gameModeActive) {
            outMsg = "Modo Jogo ja esta ativo.";
            return true;
        }

        if (m_activeGames.empty()) {
            CaptureCurrentState(m_preGameState);
        }

        // Se a Economia Máxima estava ativa, ela é sobreposta pela prioridade do jogo
        m_gameModeActive = true;

        // Ativa plano de alto desempenho
        auto schemes = m_powerMgr.ListSchemes();
        std::string gameSchemeGuid;
        for (const auto& s : schemes) {
            std::string lower = StringUtils::ToLower(s.name);
            if (lower.find("alto") != std::string::npos || lower.find("high") != std::string::npos) {
                m_powerMgr.SetActiveScheme(s.guid);
                gameSchemeGuid = s.guid;
                break;
            }
        }

        auto& adv = ModuleAdvancedSettings::Instance();
        const PlanThrottleSettings& hp = adv.cpuMinMax.highPerfPlan;
        uint32_t minPct = hp.minPercentAC;
        uint32_t maxPct = hp.maxPercentAC;
        if (minPct > maxPct) minPct = 5;
        // Escreve no plano que o jogo ativou, cada linha com o seu valor. SetProcessorThrottleRange
        // gravava o par AC em cima da linha da bateria e podia despejar numeros de jogo no plano
        // que estava ativo antes (inclusive o Economico).
        if (!gameSchemeGuid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(gameSchemeGuid, minPct, maxPct,
                                                    hp.minPercentDC, hp.maxPercentDC);
            m_powerMgr.SetCoreParkingForScheme(gameSchemeGuid, 100);
        } else {
            OPIT_LOG_WARN("[COORDINATOR] Modo Jogo manual sem plano de alto desempenho instalado: nenhum limite escrito em outro plano.");
        }

        std::string gpuMsg;
        GpuControlManager::Instance().ApplyProfile(0, GpuPowerProfile::HighPerformance, gpuMsg);
        if (!gpuMsg.empty()) OPIT_LOG_INFO("[COORDINATOR] GPU (modo jogo manual): " + gpuMsg);

        // Proteção essencial do jogo: impede suspensão de discos e do sistema operacional
        m_powerMgr.PreventDiskPowerDown(true);
        ApplyKeepAwake(true);

        outMsg = "Modo Jogo ativado. Discos e perifericos essenciais protegidos contra suspensao.";
        OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
        NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                               "Modo Jogo ativado", outMsg);
        return true;
    } else {
        if (!m_gameModeActive) {
            outMsg = "Modo Jogo nao estava ativo.";
            return true;
        }

        m_gameModeActive = false;

        // Se ainda houver jogos cadastrados em execução, preserva configurações de jogo!
        if (!m_activeGames.empty()) {
            outMsg = "Modo Jogo manual desativado, mas existem jogos ativos em execucao (" +
                     std::to_string(m_activeGames.size()) + "). Configuracoes de jogo mantidas.";
            return true;
        }

        ApplySystemState(m_preGameState);
        ApplyKeepAwake(false);

        outMsg = "Modo Jogo desativado. Estado anterior restaurado com sucesso.";
        OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
        NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                               "Modo Jogo desativado", outMsg);
        return true;
    }
}

bool ModeCoordinator::OnGameStarted(const std::string& exeName, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    if (lower.empty()) return false;

    // Se for o primeiro jogo a iniciar, salva o estado anterior da área de trabalho
    if (m_activeGames.empty() && !m_gameModeActive) {
        CaptureCurrentState(m_preGameState);
    }

    m_activeGames.insert(lower);
    m_primaryRunningGame = lower;

    // Localiza perfil individual do jogo no banco SQLite
    GameProfileRecord profile;
    bool hasProfile = DatabaseManager::Instance().GetGameProfile(lower, profile);

    if (hasProfile && profile.enabled) {
        // 1. Aplica plano de energia específico ou busca Alto Desempenho
        std::string gameSchemeGuid;
        if (!profile.powerSchemeGuid.empty()) {
            m_powerMgr.SetActiveScheme(profile.powerSchemeGuid);
            gameSchemeGuid = profile.powerSchemeGuid;
        } else {
            auto schemes = m_powerMgr.ListSchemes();
            for (const auto& s : schemes) {
                std::string sLow = StringUtils::ToLower(s.name);
                if (sLow.find("alto") != std::string::npos || sLow.find("high") != std::string::npos) {
                    m_powerMgr.SetActiveScheme(s.guid);
                    gameSchemeGuid = s.guid;
                    break;
                }
            }
        }

        // 2. Aplica limites de CPU (se perfil especificar, senao preserva os do usuario). Sempre no
        // plano que o jogo ativou e com as duas linhas explicitas; SetProcessorThrottleRange achataria
        // a linha da bateria e escreveria no plano que estava ativo antes do jogo.
        if (profile.cpuMaxPercent > 0) {
            if (!gameSchemeGuid.empty()) {
                m_powerMgr.WriteSchemeProcessorThrottle(gameSchemeGuid,
                                                        profile.cpuMinPercent, profile.cpuMaxPercent,
                                                        profile.cpuMinPercent, profile.cpuMaxPercent);
            } else {
                m_powerMgr.SetProcessorThrottleRange(profile.cpuMinPercent, profile.cpuMaxPercent);
            }
        } else {
            auto& adv = ModuleAdvancedSettings::Instance();
            const PlanThrottleSettings& hp = adv.cpuMinMax.highPerfPlan;
            uint32_t minPct = hp.minPercentAC;
            uint32_t maxPct = hp.maxPercentAC;
            if (minPct > maxPct) minPct = 5;
            if (!gameSchemeGuid.empty()) {
                m_powerMgr.WriteSchemeProcessorThrottle(gameSchemeGuid, minPct, maxPct,
                                                        hp.minPercentDC, hp.maxPercentDC);
            } else {
                m_powerMgr.SetProcessorThrottleRange(minPct, maxPct);
            }
        }
        // Ativa todos os nucleos para o jogo (sem parking)
        m_powerMgr.SetCoreParking(100);

        // 3. Aplica perfil de GPU
        std::string gpuMsg;
        if (profile.gpuProfile == "Custom") {
            GpuControlManager::Instance().ApplyCustomSettings(
                0, profile.gpuCoreOffsetMHz, profile.gpuMemOffsetMHz, profile.gpuPowerLimitPercent, gpuMsg);
        } else {
            GpuPowerProfile gProf = StringToGpuPowerProfile(profile.gpuProfile);
            GpuControlManager::Instance().ApplyProfile(0, gProf, gpuMsg);
        }
        if (!gpuMsg.empty()) OPIT_LOG_INFO("[COORDINATOR] GPU (" + lower + "): " + gpuMsg);

        // 4. Prevenção de desligamento de discos e suspensão do Windows
        if (profile.preventDiskSleep) {
            m_powerMgr.PreventDiskPowerDown(true);
        }
        if (profile.preventSystemSleep) {
            ApplyKeepAwake(true);
        }

        outMsg = "Perfil individual aplicado para o jogo: " + lower +
                 " (CPU: " + std::to_string(profile.cpuMinPercent) + "%-" + std::to_string(profile.cpuMaxPercent) +
                 "%, GPU: " + profile.gpuProfile + ").";
    } else {
        // Perfil genérico de jogo: Alto Desempenho. Ativa o plano de alto desempenho e escreve NELE;
        // sem isso os numeros de jogo iam para o plano ativo (que pode ser o Economico da usuaria).
        auto& adv = ModuleAdvancedSettings::Instance();
        std::string gameSchemeGuid;
        auto schemes = m_powerMgr.ListSchemes();
        for (const auto& s : schemes) {
            std::string sLow = StringUtils::ToLower(s.name);
            if (sLow.find("alto") != std::string::npos || sLow.find("high") != std::string::npos) {
                m_powerMgr.SetActiveScheme(s.guid);
                gameSchemeGuid = s.guid;
                break;
            }
        }
        const PlanThrottleSettings& hp = adv.cpuMinMax.highPerfPlan;
        uint32_t minPct = hp.minPercentAC;
        uint32_t maxPct = hp.maxPercentAC;
        if (minPct > maxPct) minPct = 5;
        if (!gameSchemeGuid.empty()) {
            m_powerMgr.WriteSchemeProcessorThrottle(gameSchemeGuid, minPct, maxPct,
                                                    hp.minPercentDC, hp.maxPercentDC);
            m_powerMgr.SetCoreParkingForScheme(gameSchemeGuid, 100);
        } else {
            OPIT_LOG_WARN("[COORDINATOR] Jogo '" + lower + "' sem plano de alto desempenho instalado: nenhum limite de CPU foi escrito em outro plano.");
        }
        std::string gpuMsg;
        GpuControlManager::Instance().ApplyProfile(0, GpuPowerProfile::HighPerformance, gpuMsg);
        if (!gpuMsg.empty()) OPIT_LOG_INFO("[COORDINATOR] GPU (" + lower + "): " + gpuMsg);
        m_powerMgr.PreventDiskPowerDown(true);
        ApplyKeepAwake(true);

        outMsg = "Jogo detectado (" + lower + "). Perfil padrao de Alto Desempenho ativado.";
    }

    OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
    NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                           "Jogo detectado: " + lower, outMsg);
    return true;
}

bool ModeCoordinator::OnGameStopped(const std::string& exeName, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));

    m_activeGames.erase(lower);

    // POLÍTICA EXPLÍCITA DE JOGOS SIMULTÂNEOS:
    // Se outro jogo ainda estiver em execução, NÃO restaura para área de trabalho!
    if (!m_activeGames.empty()) {
        m_primaryRunningGame = *m_activeGames.begin();
        outMsg = "Jogo " + lower + " finalizado. Outro jogo continua em execucao (" +
                 m_primaryRunningGame + "); mantendo politicas de jogo ativas.";
        OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
        return true;
    }

    // Se o modo jogo manual ainda estiver ativado pelo usuário, preserva
    if (m_gameModeActive) {
        outMsg = "Jogo " + lower + " finalizado. Modo Jogo manual permanece ativo.";
        return true;
    }

    // Último jogo encerrado: restaura o estado anterior preservado
    ApplySystemState(m_preGameState);
    ApplyKeepAwake(false);

    outMsg = "Ultimo jogo encerrado (" + lower + "). Estado anterior de energia e hardware restaurado com sucesso.";
    OPIT_LOG_INFO("[COORDINATOR] " + outMsg);
    NotificationManager::Instance().Notify(NotificationManager::Category::ModeChange,
                                           "Jogo encerrado: " + lower, outMsg);
    return true;
}

size_t ModeCoordinator::GetActiveGameCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeGames.size();
}

SystemOperatingMode ModeCoordinator::GetCurrentMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeGames.empty() || m_gameModeActive) return SystemOperatingMode::Gaming;
    if (m_deepEcoActive) return SystemOperatingMode::MaxEconomy;
    return SystemOperatingMode::Normal;
}

std::string ModeCoordinator::GetCurrentModeDescription() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeGames.empty()) {
        return "Modo Jogo Ativo (" + std::to_string(m_activeGames.size()) + " jogo(s) em execucao)";
    }
    if (m_gameModeActive) return "Modo Jogo (Ativado Manualmente)";
    if (m_deepEcoActive) return "Modo Economia Maxima (Consumo Otimizado)";
    return "Modo Padrao do Sistema";
}

void ModeCoordinator::RestorePreviousState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_deepEcoActive && m_preDeepEcoState.isValid) {
        ApplySystemState(m_preDeepEcoState);
        m_deepEcoActive = false;
    }
    if ((!m_activeGames.empty() || m_gameModeActive) && m_preGameState.isValid) {
        ApplySystemState(m_preGameState);
        m_gameModeActive = false;
        m_activeGames.clear();
    }
    ApplyKeepAwake(false);
}

} // namespace opitcore
