#include "ProcessGovernor.h"
#include "core/Logger.h"
#include "utils/StringUtils.h"
#include <algorithm>
#include <chrono>
#include <windows.h>

namespace opitcore {

namespace {

constexpr int kRankIdle = 0;
constexpr int kRankBelowNormal = 1;
constexpr int kRankNormal = 2;
constexpr uint64_t kLedgerPruneMs = 60000;
constexpr uint64_t kDeniedRetryMs = 15 * 60 * 1000; // protegido: re-tenta em 15 min

int RankOf(uint32_t priorityClass) {
    return ProcessManager::PriorityRank(priorityClass);
}

// Nivel de um daemon sem janela: nunca acima de Normal, e o nivel da janela avancada so vale depois do
// tempo ocioso medido. Ocupado, o degrau mais fundo permitido e Abaixo do Normal (max, nao min: aqui
// numero maior = prioridade maior = menos reducao).
int WindowlessRank(uint64_t idleForMs, const ProcessGovernorSettings& st) {
    const int cfgRank = std::min(RankOf(st.prioNoVisibleWindow), kRankNormal);
    if (idleForMs >= st.idleBeforeWindowlessDropMs) return cfgRank;
    return std::max(cfgRank, kRankBelowNormal);
}

} // namespace

ProcessGovernor& ProcessGovernor::Instance() {
    static ProcessGovernor instance;
    return instance;
}

bool ProcessGovernor::MatchesExcluded(const std::string& procName, const std::string& list) {
    if (list.empty()) return false;
    std::string lowerProc = StringUtils::ToLower(StringUtils::Trim(procName));
    std::string lowerList = StringUtils::ToLower(list);
    size_t start = 0;
    while (start <= lowerList.size()) {
        size_t end = lowerList.find(';', start);
        if (end == std::string::npos) end = lowerList.size();
        std::string item = StringUtils::Trim(lowerList.substr(start, end - start));
        if (!item.empty() && item == lowerProc) return true;
        if (end == lowerList.size()) break;
        start = end + 1;
    }
    return false;
}

double ProcessGovernor::CpuPercentFromDelta(uint64_t delta100ns, uint64_t elapsedMs) {
    if (elapsedMs == 0 || delta100ns == 0) return 0.0;
    const double cpuMs = static_cast<double>(delta100ns) / 10000.0;
    return cpuMs / (static_cast<double>(elapsedMs) / 100.0);
}

void ProcessGovernor::Reset() {
    m_ledger.clear();
    m_quarantine.clear();
    m_protectedAnnounced.clear();
    m_stats = ProcessGovernorStats{};
}

bool ProcessGovernor::IsQuarantined(const std::string& procName) const {
    return m_quarantine.find(StringUtils::ToLower(StringUtils::Trim(procName))) != m_quarantine.end();
}

std::string ProcessGovernor::ManagementLabelFor(uint32_t pid) const {
    auto it = m_ledger.find(pid);
    if (it == m_ledger.end() || it->second.appliedClass == 0) return std::string();
    const Entry& e = it->second;
    std::string label = std::string("Auto: ") + ProcessManager::PriorityClassToString(e.appliedClass);
    if (!e.reason.empty()) label += " (" + e.reason + ")";
    return label;
}

ProcessCoreDecision ProcessGovernor::Decide(const std::string& procName,
                                            ProcessWindowState state,
                                            const std::string& imagePath,
                                            const ProcessPriorState& prior,
                                            double cpuPercent,
                                            uint64_t idleForMs,
                                            const ProcessGovernorInput& in,
                                            const ProcessGovernorSettings& st) {
    ProcessCoreDecision d;
    const std::string lowerName = StringUtils::ToLower(StringUtils::Trim(procName));
    const std::string lowerGame = StringUtils::ToLower(StringUtils::Trim(in.gameProcessName));
    const bool isGame = !lowerGame.empty() && lowerGame == lowerName;

    switch (state) {
        case ProcessWindowState::Maximized:
            d.target = isGame ? st.prioGame : st.prioMaximized;
            d.reason = "primeiro plano";
            break;
        case ProcessWindowState::Normal:
            d.target = isGame ? st.prioGame : st.prioNormal;
            d.reason = "janela ativa";
            break;
        case ProcessWindowState::Minimized:
            d.target = st.prioMinimized;
            d.reason = "minimizada";
            break;
        case ProcessWindowState::Background:
            d.target = st.prioBackground;
            d.reason = "segundo plano";
            break;
        case ProcessWindowState::SystemTray:
            d.target = st.prioSystemTray;
            d.reason = "bandeja";
            break;
        case ProcessWindowState::NoVisibleWindow:
        default:
            if (ProcessManager::IsWindowsSystemBinary(imagePath)) {
                // Pecas do proprio Windows ficam fora da cacada: rebaixar um daemon de sistema ocupado e
                // o caminho curto para "travou sem motivo".
                d.reason.clear();
                return d;
            }
            // Teto: nunca subir um daemon acima de Normal por conta propria. Degrau: so ate o nivel
            // escolhido na janela depois de um tempo ocioso medido; ocupado, para em Abaixo do Normal.
            d.target = ProcessManager::PriorityClassFromRank(WindowlessRank(idleForMs, st));
            d.reason = (idleForMs >= st.idleBeforeWindowlessDropMs) ? "sem janela, ocioso" : "sem janela, ocupado";
            break;
    }

    const bool foreground = (state == ProcessWindowState::Maximized || state == ProcessWindowState::Normal);

    // Anti-fome: o rebaixamento que a pessoa percebe como travamento aparece aqui - um processo que
    // viemos reduzindo e que de repente passa a pedir CPU estava com fome. Sobe um degrau, registra,
    // e na segunda vez o nome sai da automacao da sessao.
    if (!foreground && prior.managedByUs && prior.appliedClass != 0 &&
        RankOf(prior.appliedClass) < RankOf(d.target) && cpuPercent >= st.starveCpuPercent) {
        d.target = ProcessManager::PriorityClassFromRank(std::min(RankOf(prior.appliedClass) + 1, kRankNormal));
        d.raise = true;
        d.starveHits = prior.starveHits + 1;
        d.reason = "fome de CPU (" + std::to_string(static_cast<int>(cpuPercent)) + "%)";
        if (d.starveHits >= st.starveLimit) {
            d.quarantineNow = true;
            d.reason += ", quarantena";
            d.target = NORMAL_PRIORITY_CLASS;
        }
    }
    return d;
}

std::vector<ProcessAction> ProcessGovernor::Plan(const std::vector<ProcessItem>& procs,
                                                 const std::unordered_map<uint32_t, ProcessWindowState>& windowStates,
                                                 const ProcessGovernorInput& in,
                                                 const ProcessGovernorSettings& st,
                                                 std::vector<std::string>* notes) {
    // Poda o diario de processos que morreram: PID e reciclavel, entao nada pode ficar guardado para
    // sempre sem conferir o nome.
    for (auto it = m_ledger.begin(); it != m_ledger.end();) {
        if (in.nowMs > it->second.lastSeenMs + kLedgerPruneMs) {
            it = m_ledger.erase(it);
        } else {
            ++it;
        }
    }

    // Uma unica coleta de audio por ciclo (a lista de usuarios do nome sozinha nao bastava).
    std::unordered_set<uint32_t> audioPids;
    if (st.preserveAudioVideoTasks) {
        audioPids = ProcessManager::ProcessPidsWithActiveAudio();
    }

    std::vector<ProcessAction> actions;
    m_stats = ProcessGovernorStats{};
    m_stats.lastCycleMs = in.nowMs;
    m_stats.quarantinedNames = m_quarantine.size();

    for (const auto& p : procs) {
        m_stats.seen++;
        if (p.pid <= 4 || p.pid == in.selfPid) continue;

        const std::string lowerName = StringUtils::ToLower(StringUtils::Trim(p.name));

        auto lit = m_ledger.find(p.pid);
        const bool fresh = (lit == m_ledger.end()) || (lit->second.name != p.name); // PID reciclado
        Entry& e = m_ledger[p.pid];
        if (fresh) {
            e = Entry{};
            e.name = p.name;
        }
        const uint64_t prevCpu = fresh ? 0 : e.lastCpu100ns;
        e.lastSeenMs = in.nowMs;
        e.lastCpu100ns = p.cpuTime100ns;

        double cpuPct = CpuPercentFromDelta(p.cpuTime100ns >= prevCpu ? p.cpuTime100ns - prevCpu : 0, in.elapsedMs);
        const bool busy = cpuPct >= st.busyCpuPercent;
        if (busy) {
            e.idleSinceMs = 0;
        } else if (e.idleSinceMs == 0) {
            e.idleSinceMs = in.nowMs;
        }
        const uint64_t idleForMs = (e.idleSinceMs == 0) ? 0 : (in.nowMs - e.idleSinceMs);

        if (m_quarantine.find(lowerName) != m_quarantine.end()) {
            if (notes) notes->push_back("quarentena " + p.name);
            e.appliedClass = 0;
            continue;
        }
        if (st.protectCriticalProcesses && p.isSystemCritical) {
            e.reason.clear();
            continue;
        }
        if (MatchesExcluded(p.name, in.excludedList)) {
            e.reason = "excluido";
            continue;
        }
        // O kernel recusa escrita de prioridade em processos protegidos (PPL/PP): nem admin, nem SYSTEM,
        // nem certificado abre isso. Perguntar ao Windows antes de tentar acaba com a falha repetida, com
        // o toast de cada 15 min e com o "falhas 3" que encobria falhas verdadeiras.
        if (p.protectedByWindows) {
            m_stats.protectedRespected++;
            e.reason = "protegido pelo Windows";
            std::string lower = StringUtils::ToLower(StringUtils::Trim(p.name));
            if (m_protectedAnnounced.find(lower) == m_protectedAnnounced.end()) {
                m_protectedAnnounced.insert(lower);
                OPIT_LOG_INFO("ProcessGovernor: '" + p.name + "' (PID " + std::to_string(p.pid) +
                              ") e protegido pelo Windows; fica fora da automacao de prioridade.");
            }
            continue;
        }

        auto wit = windowStates.find(p.pid);
        const ProcessWindowState state =
            (wit != windowStates.end()) ? wit->second : ProcessWindowState::NoVisibleWindow;
        e.state = static_cast<uint32_t>(state);

        const bool foreground = (state == ProcessWindowState::Maximized || state == ProcessWindowState::Normal);

        if (st.preserveAudioVideoTasks && !foreground &&
            (audioPids.find(p.pid) != audioPids.end() || ProcessManager::HasActiveAudioOrMedia(p.pid, p.name))) {
            e.reason = "som ativo";
            continue;
        }
        if (state == ProcessWindowState::NoVisibleWindow && !st.includeWindowless) {
            e.reason.clear();
            continue;
        }

        ProcessPriorState prior;
        prior.managedByUs = (e.appliedClass != 0);
        prior.appliedClass = e.appliedClass;
        prior.starveHits = e.starveHits;

        const ProcessCoreDecision d = Decide(p.name, state, p.imagePath, prior, cpuPct, idleForMs, in, st);
        if (d.reason.empty()) {
            e.reason.clear();
            continue; // Decisao sem motivo = elegivel mas intocavel (daemon do proprio Windows).
        }
        if (d.quarantineNow) {
            m_quarantine.insert(lowerName);
            m_stats.quarantinedNames = m_quarantine.size();
        }
        e.starveHits = d.starveHits;

        const uint32_t target = d.target;
        const std::string& why = d.reason;
        const bool raise = d.raise;

        m_stats.managed++;
        if (state == ProcessWindowState::NoVisibleWindow) m_stats.windowlessManaged++;

        ProcessAction act;
        act.pid = p.pid;
        act.name = p.name;
        act.state = state;
        act.previousClass = p.priorityClass;
        act.targetClass = target;
        act.raise = raise;
        act.cpuPercent = cpuPct;
        act.reason = why;
        act.ecoQoS = (RankOf(target) <= kRankBelowNormal) && !foreground;
        act.writeEcoQoS = !e.ecoQoSKnown || (e.ecoQoS != act.ecoQoS);
        act.trim = st.trimWorkingSetIdle && state == ProcessWindowState::Minimized &&
                   idleForMs >= st.idleBeforeWindowlessDropMs &&
                   (e.lastTrimMs == 0 || in.nowMs - e.lastTrimMs >= st.trimIntervalMs);

        const int curRank = RankOf(p.priorityClass);
        const int tgtRank = RankOf(target);
        if (curRank == tgtRank) {
            e.appliedClass = target;
            e.reason = why;
            if (act.trim) actions.push_back(act);
            continue;
        }
        // Nao brigamos com rebaixamento que nao fizemos: se outra mao (ou a pessoa) ja deixou o processo
        // mais baixo do que o nosso alvo e ele nao voltou ao primeiro plano, deixamos estar.
        if (curRank < tgtRank && !foreground && e.appliedClass == 0) {
            e.reason = "preservado (mais baixo)";
            continue;
        }
        if (in.nowMs != 0 && e.lastWriteMs != 0 && (in.nowMs - e.lastWriteMs) < st.cooldownMs && !raise && !foreground) {
            m_stats.skippedCooldown++;
            continue;
        }
        // Escrita recusada pelo Windows ha pouco (protegido/PPL): nao insistir a cada ciclo — o alvo
        // nunca coincide e a falha se repete. Re-tenta so em 15 min, ou quando vier a subir (primeiro plano).
        if (!raise && e.lastDeniedMs != 0 && in.nowMs != 0 && (in.nowMs - e.lastDeniedMs) < kDeniedRetryMs) {
            e.reason = "protegido (acesso negado)";
            continue;
        }
        actions.push_back(act);
    }

    // Orcamento: primeiro plano primeiro, depois os maiores consumidores. Sem isso um ciclo podia
    // reescrever 170 processos de uma vez e travar a leitura da aba.
    std::sort(actions.begin(), actions.end(), [](const ProcessAction& a, const ProcessAction& b) {
        const bool af = (a.state == ProcessWindowState::Maximized || a.state == ProcessWindowState::Normal);
        const bool bf = (b.state == ProcessWindowState::Maximized || b.state == ProcessWindowState::Normal);
        if (af != bf) return af;
        return a.cpuPercent > b.cpuPercent;
    });
    if (st.maxWritesPerCycle > 0 && static_cast<int>(actions.size()) > st.maxWritesPerCycle) {
        m_stats.skippedBudget = actions.size() - static_cast<size_t>(st.maxWritesPerCycle);
        actions.resize(static_cast<size_t>(st.maxWritesPerCycle));
    }
    return actions;
}

size_t ProcessGovernor::RunCycle(const std::vector<ProcessItem>& procs,
                                 const std::unordered_map<uint32_t, ProcessWindowState>& windowStates,
                                 const ProcessGovernorInput& in,
                                 const ProcessGovernorSettings& st) {
    std::vector<std::string> notes;
    auto actions = Plan(procs, windowStates, in, st, &notes);

    size_t written = 0;
    size_t lowered = 0;
    size_t raised = 0;
    size_t failed = 0;

    for (const auto& act : actions) {
        auto lit = m_ledger.find(act.pid);
        if (lit == m_ledger.end()) continue;
        Entry& e = lit->second;

        bool classOk = true;
        if (RankOf(act.targetClass) != RankOf(act.previousClass)) {
            DWORD lastErr = ERROR_SUCCESS;
            classOk = ProcessManager::SetPriority(act.pid, act.targetClass, &lastErr);
            if (classOk) {
                e.appliedClass = act.targetClass;
                e.lastWriteMs = in.nowMs;
                e.reason = act.reason;
                written++;
                if (act.raise) raised++;
                else lowered++;
            } else if (lastErr == ERROR_ACCESS_DENIED && !act.raise) {
                // Fallback: o processo virou protegido depois da enumeracao (raro) ou outra mao travou a
                // escrita. Descansa 15 min e nomeia UMA vez por episodio, para o diario dizer quem e. Sem
                // toast: a condicao e permanente e a pessoa nao pode fazer nada a respeito.
                if (e.lastDeniedMs == 0 || in.nowMs - e.lastDeniedMs >= kDeniedRetryMs) {
                    OPIT_LOG_WARN("ProcessGovernor: '" + act.name + "' (PID " + std::to_string(act.pid) +
                                  ") recusou a escrita de prioridade (acesso negado).");
                }
                e.lastDeniedMs = in.nowMs;
                e.reason = "protegido (acesso negado)";
                failed++;
            } else {
                failed++;
            }
        }

        if (act.writeEcoQoS && RankOf(e.appliedClass != 0 ? e.appliedClass : act.targetClass) <= kRankBelowNormal) {
            if (ProcessManager::SetEcoQoS(act.pid, act.ecoQoS)) {
                e.ecoQoS = act.ecoQoS;
                e.ecoQoSKnown = true;
            }
        }

        if (act.trim) {
            if (ProcessManager::OptimizeMemory(act.pid)) {
                e.lastTrimMs = in.nowMs;
            }
        }
    }

    m_stats.lowered = lowered;
    m_stats.raised = raised;
    m_stats.writes = written;
    m_stats.failedWrites = failed;

    if (written > 0 || raised > 0) {
        OPIT_LOG_INFO("ProcessGovernor: ciclo " + std::to_string(in.nowMs) + " - rebaixados " +
                      std::to_string(lowered) + ", devolvidos por fome " + std::to_string(raised) +
                      ", escritas " + std::to_string(written) + ", falhas " + std::to_string(failed) +
                      ", sem janela gerenciados " + std::to_string(m_stats.windowlessManaged) +
                      ", em quarentena " + std::to_string(m_stats.quarantinedNames) +
                      ", protegidos respeitados " + std::to_string(m_stats.protectedRespected) +
                      ", orcamento pulado " + std::to_string(m_stats.skippedBudget) + ".");
    }
    return written;
}

} // namespace opitcore
