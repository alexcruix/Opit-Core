// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace opitcore {

// 1. Planos de Energia
struct AdvPowerPlansConfig {
    std::string preferredSchemeAC;     // GUID ou nome do plano em AC
    std::string preferredSchemeDC;     // GUID ou nome do plano em DC
    bool autoSwitchOnGame = true;      // Alternar para Alto Desempenho ao iniciar jogo
    bool restoreOnGameExit = true;     // Restaurar plano anterior ao fechar jogo
    int confirmationDelayMs = 2000;    // Tempo de confirmação (ms) [500 - 10000]
    bool fallbackToBalanced = true;    // Se o plano falhar, usar Equilibrado
    bool forceEcoPlan = false;         // Forçar plano econômico explícito
    std::string forcedEcoPreviousSchemeGuid; // Plano anterior para restauração
    bool pauseIdleDevicesEco = false;  // Pausar dispositivos e controladores inativos em modo Economico (USB Suspend / PCIe ASPM)

    void Validate();
};

// Configuração granular de limites de CPU por plano individual
struct PlanThrottleSettings {
    uint32_t minPercentAC = 5;
    uint32_t maxPercentAC = 100;
    uint32_t minPercentDC = 5;
    uint32_t maxPercentDC = 75;

    // A fonte de energia decide qual par vale naquele instante: tomada = linha AC, bateria = linha DC.
    uint32_t Min(bool dc) const { return dc ? minPercentDC : minPercentAC; }
    uint32_t Max(bool dc) const { return dc ? maxPercentDC : maxPercentAC; }

    void Validate();
};

// 2. CPU Min/Max (Escala Normalizada 0 a 100)
struct AdvCpuMinMaxConfig {
    // Limites ativos globais/atuais
    uint32_t minPercentAC = 5;         // Minimo em AC (0 a 100)
    uint32_t maxPercentAC = 100;       // Maximo em AC (0 a 100)
    uint32_t minPercentDC = 5;         // Minimo em Bateria (0 a 100)
    uint32_t maxPercentDC = 75;        // Maximo em Bateria (0 a 100)
    bool allowTurboBoost = true;       // Permitir Turbo Boost
    int reevaluationIntervalMs = 1000; // Intervalo de reavaliacao (ms) [250 - 5000]
    std::string targetSchemeGuid;      // GUID do plano em configuracao

    // Configurações salvas individualmente para cada plano de energia
    // Defaults compilados = perfil desktop (maquina sem bateria). Em execucao o valor real vem de
    // ApplyStandardProfile(), que escolhe o perfil pela classe de hardware detectada.
    PlanThrottleSettings ecoPlan{0, 35, 0, 35};           // Economia de Energia
    PlanThrottleSettings balancedPlan{36, 78, 36, 78};    // Balanceado / Equilibrado
    PlanThrottleSettings highPerfPlan{79, 100, 79, 100};  // Alta Performance / Jogos

    // Perfil-padrao declarado pela usuaria em 17/11/2026 e gravado como limites Min/Max de cada plano:
    //   desktop / servidor (sem bateria), na tomada: 0-35 % / 36-78 % / 79-100 %
    //   notebook, na tomada:                         0-25 % / 26-50 % / 51-75 %
    //   notebook, na bateria:                        0-20 % / 21-35 % / 36-60 %
    // As faixas que o orquestrador usa para trocar de plano saem DAQUI (mesmos numeros, uma fonte so),
    // entao o que esta escrito na janela de CPU Min/Max e o que o motor obedece. A semeadura acontece
    // uma vez por kStandardProfileId: o que a pessoa ajustar depois permanece intacto.
    static constexpr const char* kStandardProfileId = "bandas-classe-hardware-v1";
    void ApplyStandardProfile(bool hasBattery);

    PlanThrottleSettings GetPlanThrottle(const std::string& schemeGuidOrName) const;
    void SetPlanThrottle(const std::string& schemeGuidOrName, const PlanThrottleSettings& throttle);

    bool Validate(std::string* outError = nullptr);
};

// 3. Core Parking
struct AdvCoreParkingConfig {
    uint32_t minCoresActiveAC = 50;    // 25% (Desempenho), 50% (Equilibrado), 100% (Economia)
    uint32_t minCoresActiveDC = 75;    // Bateria
    bool forceUnparkOnGame = true;     // Desestacionar núcleos para jogos
    int hysteresisMs = 3000;           // Histerese para evitar oscilações [1000 - 10000]
    bool forceMaxParking = false;      // Forçar máximo de núcleos pausados
    uint32_t previousParkingAc = 50;   // Valor anterior para restauração

    void Validate();
};

// 4. Processos
struct AdvProcessesConfig {
    uint32_t foregroundPriorityClass = 0x00008000; // ABOVE_NORMAL_PRIORITY_CLASS
    uint32_t gamePriorityClass = 0x00000080;       // HIGH_PRIORITY_CLASS
    std::string excludedProcesses = "explorer.exe;dwm.exe;lsass.exe;csrss.exe;services.exe;taskmgr.exe;Opit-Core.exe";
    bool trimWorkingSetIdle = false;               // Otimizar working set
    bool protectCriticalProcesses = true;          // Proteger processos do Windows

    // Configurações por estado da janela/aplicação
    bool autoProcessManagement = false;            // Botão/Modo "Auto"
    uint32_t prioMaximized = 0x00008000;           // ABOVE_NORMAL_PRIORITY_CLASS
    uint32_t prioNormal = 0x00000020;              // NORMAL_PRIORITY_CLASS
    uint32_t prioMinimized = 0x00004000;           // BELOW_NORMAL_PRIORITY_CLASS
    uint32_t prioBackground = 0x00004000;          // BELOW_NORMAL_PRIORITY_CLASS
    uint32_t prioSystemTray = 0x00004000;          // BELOW_NORMAL_PRIORITY_CLASS
    uint32_t prioNoVisibleWindow = 0x00000040;      // IDLE_PRIORITY_CLASS
    bool preserveAudioVideoTasks = true;           // Preservar apps de gravação, áudio e tarefas ativas

    void Validate();
    bool IsProcessExcluded(const std::string& procName) const;
};

// 5. GPU
struct AdvGpuConfig {
    int graphicsPreference = 2;        // 0 = Padrão, 1 = Economia/Integrada, 2 = Alto Desempenho/Dedicada
    bool dxgiLowLatency = true;        // Flag de baixa latência DXGI
    std::string preferredAdapterName;  // Nome do adaptador selecionado
    int powerProfile = 1;              // 0 = Eco, 1 = Balanced, 2 = HighPerformance, 3 = Custom
    int coreClockOffsetMHz = 0;        // Offset em MHz [-200 a +250]
    int memoryClockOffsetMHz = 0;      // Offset em MHz [-200 a +400]
    int powerLimitPercent = 100;       // Limite percentual de TDP [60 a 120%]

    void Validate();
};

// 6. Ventoinhas (Fan Curve)
struct AdvFansConfig {
    int controlMode = 0;               // 0 = BIOS/Firmware Automático, 1 = Silencioso, 2 = Agressivo/Performance
    int thermalHysteresisC = 5;        // Histerese térmica (°C) [2 - 10]
    bool enforceFirmwareSafeguards = true; // Inviolável: proteções do firmware

    void Validate();
};

// 7. Guardião Térmico
struct AdvThermalConfig {
    double alertLimitC = 80.0;         // Limite de alerta/ação (°C) [60.0 - 95.0]
    double criticalLimitC = 90.0;      // Limite crítico de emergência (°C) [70.0 - 100.0]
    double hysteresisRestoreC = 5.0;   // Histerese de restauração (°C) [3.0 - 15.0]
    uint32_t throttleActionCap = 70;   // % de CPU ao disparar throttle [50 - 85]
    int confirmationTimeMs = 2000;     // Tempo mínimo de confirmação (ms) [1000 - 5000]
    bool logThermalIncidents = true;   // Gravar no log do banco

    void Validate();
};

// 8. Estabilidade
struct AdvStabilityConfig {
    int observationWindowSamples = 5;  // Amostras de observação [3 - 20]
    double minImprovementPercent = 3.0;// % de ganho mínimo para manter ação [1.0 - 10.0]
    bool autoRevertOnRegression = true;// Reverter regressão automaticamente
    bool monitorDpcLatency = true;     // Monitorar DPC / variações
    double noiseTolerancePercent = 3.0;// Tolerância a ruído natural [1.0 - 8.0]

    void Validate();
};

// 9. Serviços Windows
struct AdvServicesConfig {
    // Gatilhos de Pausa Automática ("Quando Pausar")
    bool pauseOnGame = true;                  // Pausar ao detectar jogos / modo gamer
    bool pauseOnEco = true;                   // Pausar em modo bateria ou economia
    bool pauseOnHighCpu = false;              // Pausar sob alta carga de CPU
    uint32_t highCpuThreshold = 85;           // Limiar percentual de CPU [50 a 95%]
    bool pauseBloatwarePermanent = false;     // Manter telemetria e atualizadores permanentemente pausados

    // Nível de Autonomia da IA
    // 0 = Manual (Apenas regras fixas do usuário)
    // 1 = Consultivo (IA recomenda no chat e pede confirmação antes de pausar)
    // 2 = Automático (IA gerencia e otimiza serviços seguros dinamicamente)
    int aiAutonomyLevel = 1;

    // Categorias e Serviços Gerenciados
    bool manageWSearch = true;                // Windows Search (Indexação de arquivos)
    bool manageSysMain = true;                // SysMain (Superfetch)
    bool manageSpooler = true;                // Spooler de Impressão (quando sem filas ativas)
    bool manageDiagTrack = true;              // Telemetria do Windows (DiagTrack / dmwappushservice)
    bool manageThirdPartyUpdaters = true;     // Atualizadores em 2º plano (GoogleUpdate, EdgeUpdate, etc.)

    // Proteções e Exceções
    bool restoreOnExit = true;                // Restaurar ao fechar o Opit-Core
    bool checkDependencies = true;            // Verificar dependências no SCM antes de pausar
    std::string customExcludedServices = "RpcSs;DcomLaunch;EventLog;PlugPlay;LanmanWorkstation"; // Bloqueados pelo sistema (invioláveis)
    std::string customUserExcludedServices = ""; // Exceções personalizadas pelo usuário (NUNCA pausar)

    // Varredura de ociosidade: pausar o maximo que a maquina permitir enquanto ninguem usa, com
    // gatilho de retorno. O retorno e imediato (a pessoa mexe no mouse e o servico volta).
    bool pauseWhenIdle = true;
    uint32_t idleSecondsBeforePause = 240;    // [60 a 1800] ociosidade medida antes de encostar no primeiro
    bool allowTruePause = true;               // usar PAUSE do SCM quando o servico aceita, em vez de STOP
    uint32_t minGainMB = 8;                   // so vale a pena pausar se o hospedeiro medido passar daqui
    int maintenanceHour = 3;                  // janela diaria de atualizacao/telemetria [0 a 23]
    int maintenanceMinutes = 30;              // duracao da janela em minutos [5 a 180]

    void Validate();
    bool IsServiceAllowed(const std::string& svcName) const;
};

class ModuleAdvancedSettings {
public:
    static ModuleAdvancedSettings& Instance();

    bool Load();
    bool Save();

    // Override manual da classe de hardware ("compativel com qualquer hardware"): "auto" segue a
    // deteccao (bateria = notebook); "desktop" ou "notebook" forcam a tabela de bandas daquela classe.
    // Existe para maquinas que a heuristica erra: VM, tablet, mini-PC com bateria sempre ligada.
    std::string hwClassOverride = "auto";
    static std::string NormalizeHwClass(const std::string& value);
    bool EffectiveHasBattery() const;
    std::string EffectiveHwClassName() const;

    // Reset explícito para valores padrão seguros
    void ResetToDefaults(int moduleIndex = -1);

    // Validação completa de todos os módulos
    void ValidateAll();

    AdvPowerPlansConfig powerPlans;
    AdvCpuMinMaxConfig cpuMinMax;
    AdvCoreParkingConfig coreParking;
    AdvProcessesConfig processes;
    AdvGpuConfig gpu;
    AdvFansConfig fans;
    AdvThermalConfig thermal;
    AdvStabilityConfig stability;
    AdvServicesConfig services;

private:
    ModuleAdvancedSettings() = default;
    mutable std::mutex m_mutex;
};

} // namespace opitcore
