// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <cstdint>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
// Os numeros que o SCM da aos estados e aos controlos, repetidos aqui com os mesmos valores
// (WinBase.h do SDK 10.0.26100.0, medidos no cabecalho). O HostInfo desta classe guarda o estado
// do unit nestes mesmos bits, pelo que o reconciliador — que e um so e decide em cima deles —
// funciona identico nos dois sistemas: em Linux "active" vale SERVICE_RUNNING porque e a mesma
// pergunta feita a outra fonte. Os unicos valores novos desta lista sao os que o systemd nao tem
// (PAUSE/CONTINUE) e mesmo assim mantemos o numero do Windows: o gemelo nunca os escreve.
#define SERVICE_STOPPED 0x00000001u
#define SERVICE_START_PENDING 0x00000002u
#define SERVICE_STOP_PENDING 0x00000003u
#define SERVICE_RUNNING 0x00000004u
#define SERVICE_CONTINUE_PENDING 0x00000005u
#define SERVICE_PAUSE_PENDING 0x00000006u
#define SERVICE_PAUSED 0x00000007u
#define SERVICE_ACCEPT_STOP 0x00000001u
#define SERVICE_ACCEPT_PAUSE_CONTINUE 0x00000002u
#define SERVICE_ACCEPT_SHUTDOWN 0x00000004u
#define SERVICE_CONTROL_STOP 0x00000001u
#define SERVICE_CONTROL_PAUSE 0x00000002u
#define SERVICE_CONTROL_CONTINUE 0x00000003u
#endif

namespace opitcore {

// Como um servico que pausamos volta a funcionar. Cada nivel tem uma prova diferente de retorno; sem
// prova o servico nao entra na automacao (e declarado nao automatizavel na interface, em vez de ser
// parado e quebrado em silencio).
enum class ServiceWake : int {
    None = 0,           // sem retorno garantido -> nunca pausar automaticamente
    OnUserActivity,     // volta no instante em que a pessoa mexe no mouse ou no teclado
    OnDemandProcess,    // volta quando o aplicativo que precisa dele abre
    OnMaintenanceWindow,// volta na janela diaria de atualizacao/manutencao
    OnTriggerEnd        // volta quando o gatilho que pausou (jogo / eco / alta CPU) acaba
};

const char* ServiceWakeToString(ServiceWake w);

// Uma entrada do catalogo de automacao: o que pode ser pausado, com que prova de retorno e a partir de
// que nivel de autonomia da IA.
struct ServiceCatalogEntry {
    const char* name;
    const char* label;
    ServiceWake wake;
    int minAutonomy; // 0 = so com regras fixas, 1 = tambem em modo consultivo, 2 = so em automatico
    const char* note;
};

// Estado do mundo que a automacao de servicos observa num ciclo. Tudo que decide entra por aqui, para que
// a decisao seja reproduzivel num teste sem mexer no SCM.
struct ServiceSweepInput {
    uint64_t nowMs = 0;
    uint64_t userIdleMs = 0;          // GetLastInputInfo, medido no ciclo
    uint64_t idlePauseAfterMs = 240000; // ociosidade minima antes de encostar no primeiro servico
    bool isGameRunning = false;
    bool isBatteryOrEco = false;
    double cpuUsagePercent = 0.0;
    int autonomyLevel = 1;
    uint32_t highCpuThreshold = 85;
    uint64_t minGainMB = 8;           // so vale pausar se o hospedeiro medido passar daqui
    int maintenanceHour = 3;          // inicio da janela diaria de atualizacao
    int maintenanceMinutes = 30;      // duracao da janela em minutos
    std::string serviceLevel = "min"; // matriz min/med/max/ultra dos Servicos Inteligentes
    bool subSmartServices = false;
    bool pauseIndexingIdle = false;   // interruptor antigo "Pausar indexacao com o PC parado" (Unified)
    const std::vector<std::string>* runningProcesses = nullptr; // nomes em execucao (vazio = descobrir)
};

// O que guardamos de cada servico que pausamos, para conseguir devolve-lo exatamente como era.
struct ServicePauseRecord {
    std::string name;
    bool wasRunning = false;
    bool pausedByPauseControl = false; // PAUSE real do SCM, em vez de STOP
    bool declared = false;             // escolha declarada da pessoa (manual, opt-out, regra fixa): o ciclo nao reavalia nem desfaz
    uint64_t pausedAtMs = 0;
    ServiceWake wake = ServiceWake::None;
    uint64_t hostWorkingSetMB = 0;     // ganho medido no momento da pausa
    std::string reason;
};

struct ServiceSweepStats {
    size_t candidates = 0;
    size_t paused = 0;
    size_t resumed = 0;
    size_t skippedSharedHost = 0;
    size_t skippedSmallGain = 0;
    size_t skippedLockout = 0;
    size_t skippedNoReturn = 0;
    size_t skippedBudget = 0;  // orcamento do ciclo esgotado
    size_t skippedOther = 0;   // recusa do SCM, falta de privilegio, erro de abertura
    size_t unautomatable = 0;
    uint64_t gainMBLastCycle = 0;
    uint64_t lastCycleMs = 0;
};

struct ServiceStatusInfo {
    std::string name;
    std::string displayName;
    bool isRunning = false;
    bool isPausedByOpit = false;
    bool isCritical = false;
    bool isAllowed = true;
    std::string category;
    std::string rule = "default"; // "default", "auto", "pause_on_game", "pause_on_eco", "pause_on_high_cpu", "always_paused", "never_pause"
    std::string statusText;
    std::string wake;             // prova de retorno deste servico
    uint64_t gainMB = 0;          // memoria medida do processo que hospedava o servico
    std::string blockReason;      // por que a automacao nao toca nele
};

class ServiceManager {
public:
    static ServiceManager& Instance();

    bool IsServiceRunning(const std::string& serviceName);
    bool PauseOrStopService(const std::string& serviceName);
    // Pausa com prova de retorno registrada. `counted` = o ciclo de automacao (aplica trava anti-vaivem);
    // `declared` = escolha da pessoa sem prova exigida (manual, opt-out de telemetria, regra fixa).
    bool PauseOrStopService(const std::string& serviceName, ServiceWake wake, const std::string& reason,
                            bool counted = false, bool declared = false);
    bool ResumeOrStartService(const std::string& serviceName);
    void RestoreModifiedServices();

    // Ciclo unico e autoritario da automacao de servicos. Os tres caminhos anteriores
    // (EvaluateAdvancedTriggers + ApplyServiceLevel + ApplySmartGamingAndEcoServices) podiam agir sobre o
    // mesmo servico dentro da mesma amostra: um parava e o outro restaurava, e a restauracao apagava o
    // cooldown de 5 min. E isso que produzia o vaivem do Spooler medido em 15 eventos / 3 min.
    void RunAutomaticCycle(const ServiceSweepInput& in);

    // Leitura do mundo (ocio, settings, processos) + um ciclo. E a porta que o CoreRuntime usa: uma chamada
    // por tick, em vez das tres que disputavam o mesmo servico.
    ServiceSweepInput SweepInputFromSystem(bool isGameRunning, bool isBatteryOrEco, double cpuUsagePercent,
                                           const std::string& serviceLevel, bool subSmartServices);
    void RunServiceCycleFromSystem(bool isGameRunning, bool isBatteryOrEco, double cpuUsagePercent,
                                   const std::string& serviceLevel, bool subSmartServices);

    // --- Peca do reconciliador, expostas para teste e para a interface ---
    static const std::vector<ServiceCatalogEntry>& Catalog();
    static ServiceWake RequiredWakeFor(const std::string& serviceName);
    // A prova de retorno deste servico vale agora? Servico sem prova valendo nao pode ficar pausado.
    static bool ShouldWakeNow(ServiceWake w, const ServiceSweepInput& in, bool demandProcessRunning);
    static bool InMaintenanceWindow(int hour, int minuteSpan);
    // Matriz min/med/max/ultra: quais nomes cada nivel encosta (exposta para teste).
    static bool LevelMatrixIncludes(const std::string& level, const std::string& serviceName,
                                    bool subSmartServices, bool isGameRunning);
    // O nome esta hospedado sozinho num processo? Devolve o PID e a memoria medida do hospedeiro.
    static bool HostProcessExclusive(const std::string& serviceName, uint32_t* pidOut, uint64_t* workingSetMBOut);
    // Ha trabalho de impressao na fila (usado para NAO pausar o Spooler em cima de job pendurado).
    static bool PrintJobsPending();
    ServiceSweepStats Stats() const;
    std::vector<ServicePauseRecord> PauseRecords() const;
    bool IsUnautomatable(const std::string& serviceName) const;
    std::string BlockReasonFor(const std::string& serviceName) const;

    // Aplicação da matriz de serviços inteligentes do Opit-Core
    void ApplyServiceLevel(const std::string& level, bool isGameRunning, bool subSmartServices);

    // Controle dedicado da Telemetria da Microsoft (Opt-in do usuário)
    bool SetMicrosoftTelemetryDisabled(bool disable);
    bool IsMicrosoftTelemetryDisabled() const;

    // Detecção e otimização dinâmica baseada nos serviços de terceiros instalados no sistema
    std::vector<std::string> DetectInstalledNonEssentialServices();
    void ApplySmartGamingAndEcoServices(bool isGaming, bool isDeepEco);

    // Verificação de dependência entre aplicativos instalados/em execução e serviços
    bool IsServiceNeededByActiveApplication(const std::string& serviceName);
    // Variante com a lista de processos ja lida neste ciclo: a verificacao de demanda nao pode
    // reenumerar os processos a cada servico de uma varredura de 25 nomes.
    bool IsServiceNeededByActiveApplication(const std::string& serviceName,
                                            const std::vector<std::string>& runningProcesses);

    // Avaliação contínua dos gatilhos avançados (Jogos, Eco/Bateria, Carga alta de CPU)
    void EvaluateAdvancedTriggers(bool isGameRunning, bool isBatteryOrEco, double cpuUsagePercent);

    // Gerenciamento granular de regras por serviço individual
    void SetServiceRule(const std::string& serviceName, const std::string& rule);
    std::string GetServiceRule(const std::string& serviceName);
    void LoadAllServiceRules();
    std::unordered_map<std::string, std::string> GetAllCustomRules() const;

    // Enumeração completa de serviços para a nova aba dedicada
    std::vector<ServiceStatusInfo> EnumerateAllServices(const std::string& filter = "");

    // Governança de IA com Guardrails
    bool CanServiceBeOptimized(const std::string& serviceName) const;
    bool IsCriticalService(const std::string& serviceName) const;
    bool RequestAiServiceOptimization(const std::string& serviceName, bool pause, const std::string& rationale, std::string* outFeedback = nullptr);

    // Ações manuais e diagnósticos
    bool PauseService(const std::string& serviceName) { return PauseOrStopService(serviceName); }
    bool ResumeService(const std::string& serviceName) { return ResumeOrStartService(serviceName); }
    void RestoreAllServicesNow();
    size_t GetPausedServicesCount() const;
    std::vector<std::string> GetPausedServicesList() const;
    std::vector<ServiceStatusInfo> GetTrackedServicesInfo();

private:
    ServiceManager();
    ~ServiceManager();

    // Um nome por ciclo: o que queremos que ele faca e com que prova de retorno ele volta.
    struct DesiredState {
        ServiceWake wake = ServiceWake::None;
        std::string reason;
        bool pause = false;
        bool protect = false; // regra "Nunca Pausar (Protegido)": nao para e nao e reescrito
        bool fixedRule = false; // "Manter sempre parado": a pessoa assumiu o retorno, nao exigimos prova
    };

    void BuildDesiredStates(const ServiceSweepInput& in, std::unordered_map<std::string, DesiredState>* out);
    // Codigo de bloqueio (para a estatistica e para a coluna da UI): 0 fez, 1 host dividido, 2 ganho
    // pequeno, 3 trava anti-vaivem, 4 sem prova de retorno, 5 critico/excluido, 6 demanda ativa, 7 falhou.
    int PauseTracked(const std::string& name, const DesiredState& want, const ServiceSweepInput& in,
                     std::string* blockOut);
    bool WakeTracked(const std::string& name, bool countedAsDemand, uint64_t nowMs);
    // Conta uma volta por demanda e aplica a trava. Chamar com m_mutex ja tomado.
    void CountDemandWakeLocked(const std::string& lowerKey, uint64_t nowMs);
    void SaveJournal();
    void LoadJournal();

    // Mapa servico -> processo hospedeiro, uma unica passada de EnumServicesStatusEx por ciclo.
    struct HostInfo {
        uint32_t pid = 0;
        int servicesInHost = 0; // > 1 = divididos com outros servicos: pausar atinge quem nao pedimos
        uint64_t workingSetMB = 0;
#ifdef _WIN32
        DWORD controlsAccepted = 0;
        DWORD state = 0;
#else
        // Mesma largura do DWORD e os mesmos bits SERVICE_*_Accepted / SERVICE_STATE_*; em
        // Linux o gemelo do systemd guarda aqui o estado do unit (ActiveState/SubState).
        uint32_t controlsAccepted = 0;
        uint32_t state = 0;
#endif
        bool known = false;
    };
    void RefreshHostCache(uint64_t nowMs);
    bool HostFromCache(const std::string& serviceName, HostInfo* out) const;
    void SetBlockReason(const std::string& serviceName, const std::string& reason, uint64_t nowMs);

    std::mutex m_mutex;
    std::set<std::string> m_pausedServices;
    std::unordered_map<std::string, uint64_t> m_lastStopAttemptMs;
    std::unordered_map<std::string, std::string> m_serviceRules;
    std::unordered_map<std::string, ServicePauseRecord> m_records;
    std::unordered_map<std::string, uint64_t> m_lastWakeMs;    // svc -> ultima volta por demanda
    std::unordered_map<std::string, int> m_demandWakes;        // svc -> voltas por demanda na janela
    std::unordered_map<std::string, std::string> m_blockReason;// por que a automacao nao toca nele
    std::set<std::string> m_unautomatable;                     // devolveu sem nos pedir, demais vezes
    ServiceSweepStats m_stats;
    bool m_highCpuTriggerActive = false; // histerese real: entra no limiar, sai 15 pontos abaixo
    bool m_rulesLoaded = false;
    bool m_journalLoaded = false;
    uint64_t m_lastHostCacheMs = 0;
    uint64_t m_lastCycleMs = 0;
    std::unordered_map<std::string, HostInfo> m_hostCache; // servico -> hospedeiro medido
    std::vector<std::string> m_thirdPartyNames;            // descobertos na mesma passada do SCM
};

} // namespace opitcore
