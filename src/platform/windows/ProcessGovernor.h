#pragma once
#include "platform/windows/ProcessManager.h"
#include "utils/Types.h"
#include <windows.h> // classes de prioridade (IDLE_/BELOW_NORMAL_/..._PRIORITY_CLASS)
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace opitcore {

// Espelho do que a janela "Controlo de Processos" guarda (AdvProcessesConfig). O motor recebe copia para
// continuar testavel sem depender do singleton de configuracao nem do banco aberto.
struct ProcessGovernorSettings {
    uint32_t prioMaximized = ABOVE_NORMAL_PRIORITY_CLASS;
    uint32_t prioNormal = NORMAL_PRIORITY_CLASS;
    uint32_t prioMinimized = BELOW_NORMAL_PRIORITY_CLASS;
    uint32_t prioBackground = BELOW_NORMAL_PRIORITY_CLASS;
    uint32_t prioSystemTray = BELOW_NORMAL_PRIORITY_CLASS;
    uint32_t prioNoVisibleWindow = IDLE_PRIORITY_CLASS;
    uint32_t prioGame = HIGH_PRIORITY_CLASS;

    bool protectCriticalProcesses = true;
    bool preserveAudioVideoTasks = true;
    bool trimWorkingSetIdle = false;
    // A cacada: processos sem janela tambem entram. So se aplica a binarios de terceiro (fora de
    // \Windows), que sao os que comem memoria sem que a pessoa veja; as pecas do proprio Windows ficam
    // de fora porque rebaixar um svchost ocupado e exatamente o tipo de coisa que trava o sistema.
    bool includeWindowless = true;

    int maxWritesPerCycle = 24;
    uint64_t cooldownMs = 30000;
    uint64_t idleBeforeWindowlessDropMs = 60000;
    double busyCpuPercent = 0.5;      // abaixo disso o processo conta como ocioso
    double starveCpuPercent = 3.0;    // rebaixado e usando isto => esta com fome, sobe um degrau
    int starveLimit = 2;              // fusas antes de o nome entrar em quarentena da sessao
    uint64_t trimIntervalMs = 300000;
};

struct ProcessGovernorInput {
    uint64_t nowMs = 0;
    uint64_t elapsedMs = 1000;
    bool isGameRunning = false;
    std::string gameProcessName; // so o processo do jogo recebe o nivel "Jogos"; o resto segue normal
    uint32_t selfPid = 0;
    std::string excludedList; // "explorer.exe;dwm.exe;..."
};

struct ProcessAction {
    uint32_t pid = 0;
    std::string name;
    ProcessWindowState state = ProcessWindowState::NoVisibleWindow;
    uint32_t previousClass = NORMAL_PRIORITY_CLASS;
    uint32_t targetClass = NORMAL_PRIORITY_CLASS;
    bool ecoQoS = false;
    bool writeEcoQoS = false;
    bool trim = false;
    bool raise = false; // esta acao sobe prioridade (fome ou retorno ao primeiro plano)
    double cpuPercent = 0.0;
    std::string reason;
};

struct ProcessGovernorStats {
    size_t seen = 0;
    size_t managed = 0;
    size_t lowered = 0;
    size_t raised = 0;
    size_t writes = 0;
    size_t failedWrites = 0;
    size_t skippedBudget = 0;
    size_t skippedCooldown = 0;
    size_t quarantinedNames = 0;
    size_t windowlessManaged = 0;
    // Processos que o kernel protege (PPL/PP): ficam fora da automacao por impossibilidade, nao por
    // falha. Contados a parte para o numero "falhas" voltar a significar falha de verdade.
    size_t protectedRespected = 0;
    uint64_t lastCycleMs = 0;
};

// Estado anterior que o diario guarda de um PID, na forma minima que a decisao precisa.
struct ProcessPriorState {
    bool managedByUs = false;
    uint32_t appliedClass = 0;
    int starveHits = 0;
};

// Resultado puro da politica para um processo. Separado do resto para que os testes cobrem a decisao
// sem precisar de processos reais nem escrever na tabela de prioridades do Windows.
struct ProcessCoreDecision {
    uint32_t target = NORMAL_PRIORITY_CLASS;
    std::string reason;
    bool raise = false;        // esta decisao devolve prioridade
    bool quarantineNow = false;// a partir de agora este nome sai da automacao da sessao
    int starveHits = 0;
};

class ProcessGovernor {
public:
    static ProcessGovernor& Instance();

    // Politica pura: estado de janela + tempo ocioso medido + o que ja aplicamos -> nivel alvo.
    static ProcessCoreDecision Decide(const std::string& procName,
                                      ProcessWindowState state,
                                      const std::string& imagePath,
                                      const ProcessPriorState& prior,
                                      double cpuPercent,
                                      uint64_t idleForMs,
                                      const ProcessGovernorInput& in,
                                      const ProcessGovernorSettings& st);

    // Decide sem tocar no sistema. Alimenta o diario interno (ociosidade e tempo de CPU por PID) e e o
    // ponto unico dos testes: a politica fica fora do thread de trabalho.
    std::vector<ProcessAction> Plan(const std::vector<ProcessItem>& procs,
                                    const std::unordered_map<uint32_t, ProcessWindowState>& windowStates,
                                    const ProcessGovernorInput& in,
                                    const ProcessGovernorSettings& st,
                                    std::vector<std::string>* notes = nullptr);

    // Executa o plano devolvido por Plan e conta as escritas de facto.
    size_t RunCycle(const std::vector<ProcessItem>& procs,
                    const std::unordered_map<uint32_t, ProcessWindowState>& windowStates,
                    const ProcessGovernorInput& in,
                    const ProcessGovernorSettings& st);

    void Reset();

    // Roto curto para a coluna de estado da aba "Processos do Sistema".
    std::string ManagementLabelFor(uint32_t pid) const;
    ProcessGovernorStats Stats() const { return m_stats; }
    bool IsQuarantined(const std::string& procName) const;

    static bool MatchesExcluded(const std::string& procName, const std::string& list);
    static double CpuPercentFromDelta(uint64_t delta100ns, uint64_t elapsedMs);

private:
    struct Entry {
        std::string name;
        uint64_t lastCpu100ns = 0;
        uint64_t lastSeenMs = 0;
        uint64_t idleSinceMs = 0; // 0 = esta ocupado agora (ou ainda sem amostra anterior)
        uint64_t lastWriteMs = 0;
        uint64_t lastTrimMs = 0;
        uint32_t appliedClass = 0; // 0 = ainda nao escrevemos neste PID
        uint64_t lastDeniedMs = 0; // ultima escrita recusada por acesso negado: 15 min de descanso antes de tentar de novo
        uint32_t state = static_cast<uint32_t>(ProcessWindowState::NoVisibleWindow);
        bool ecoQoS = false;
        bool ecoQoSKnown = false;
        int starveHits = 0;
        std::string reason;
    };

    std::unordered_map<uint32_t, Entry> m_ledger;
    std::unordered_set<std::string> m_quarantine;
    // Nome ja anunciado como protegido: o aviso e por sessao, nao por ciclo nem por PID.
    std::unordered_set<std::string> m_protectedAnnounced;
    ProcessGovernorStats m_stats;
};

} // namespace opitcore
