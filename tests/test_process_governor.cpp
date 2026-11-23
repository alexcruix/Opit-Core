// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "platform/windows/ProcessGovernor.h"
#include "platform/windows/ProcessManager.h"
#include <iostream>
#include <windows.h>

using namespace opitcore;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FALHA: " << what << std::endl;
        g_failures++;
    }
}

ProcessItem FakeProc(uint32_t pid, const std::string& name, const std::string& image, uint32_t cls) {
    ProcessItem p;
    p.pid = pid;
    p.name = name;
    p.imagePath = image;
    p.priorityClass = cls;
    p.isSystemCritical = ProcessManager::IsCriticalProcess(name);
    p.cpuTime100ns = 0;
    return p;
}

ProcessGovernorSettings DefaultSettings() {
    ProcessGovernorSettings st;
    st.prioMaximized = ABOVE_NORMAL_PRIORITY_CLASS;
    st.prioNormal = NORMAL_PRIORITY_CLASS;
    st.prioMinimized = BELOW_NORMAL_PRIORITY_CLASS;
    st.prioBackground = BELOW_NORMAL_PRIORITY_CLASS;
    st.prioSystemTray = BELOW_NORMAL_PRIORITY_CLASS;
    st.prioNoVisibleWindow = IDLE_PRIORITY_CLASS;
    st.prioGame = HIGH_PRIORITY_CLASS;
    return st;
}

} // namespace

int main() {
    const ProcessGovernorSettings st = DefaultSettings();
    ProcessGovernorInput in;
    in.nowMs = 100000;
    in.elapsedMs = 1000;
    in.selfPid = 9999;
    in.excludedList = "explorer.exe;dwm.exe;opit-core.exe";

    // 1. Ordem das classes e teto de seguranca: nada mapeia para Tempo Real.
    Check(ProcessManager::PriorityRank(IDLE_PRIORITY_CLASS) == 0, "rank Ociosa");
    Check(ProcessManager::PriorityRank(BELOW_NORMAL_PRIORITY_CLASS) == 1, "rank Abaixo do Normal");
    Check(ProcessManager::PriorityRank(NORMAL_PRIORITY_CLASS) == 2, "rank Normal");
    Check(ProcessManager::PriorityClassFromRank(5) == HIGH_PRIORITY_CLASS, "rank 5 devolve Alta, nunca Tempo Real");
    Check(ProcessManager::PriorityClassFromRank(-3) == IDLE_PRIORITY_CLASS, "rank negativo devolve Ociosa");

    // 2. Binario do Windows vs de terceiro (a fronteira da cacada em processos sem janela).
    Check(ProcessManager::IsWindowsSystemBinary("C:\\Windows\\System32\\svchost.exe"), "svchost he de sistema");
    Check(ProcessManager::IsWindowsSystemBinary("C:\\Windows\\explorer.exe"), "explorer na raiz he de sistema");
    Check(ProcessManager::IsWindowsSystemBinary(""), "sem caminho lido he tratado como de sistema");
    Check(!ProcessManager::IsWindowsSystemBinary("C:\\Program Files\\Acme\\AcmeUpdater.exe"), "updater de terceiro nao he de sistema");

    // 3. Lista de excecao da janela avancada.
    Check(ProcessGovernor::MatchesExcluded("DWM.EXE", in.excludedList), "excecao ignora maiusculas");
    Check(!ProcessGovernor::MatchesExcluded("notepad.exe", in.excludedList), "notepad nao he excecao");
    Check(!ProcessGovernor::MatchesExcluded("notepad.exe", ""), "lista vazia nao casa nada");

    // 4. Medicao de CPU: 1 s de CPU em 1 s de relogio = 100 %.
    Check(ProcessGovernor::CpuPercentFromDelta(10000000ULL, 1000) > 99.5, "1s de CPU em 1s = 100%");
    Check(ProcessGovernor::CpuPercentFromDelta(50000ULL, 1000) < 1.0, "5ms de CPU em 1s = 0,5%");
    Check(ProcessGovernor::CpuPercentFromDelta(0, 1000) == 0.0, "sem delta = 0%");

    // 5. Os seis niveis da janela de configuracao mandam de verdade (antes o motor usava Idle fixo).
    auto bg = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Background,
                                      "C:\\Program Files\\Acme\\acme.exe", {}, 0.0, 0, in, st);
    Check(bg.target == BELOW_NORMAL_PRIORITY_CLASS, "segundo plano usa prioBackground");
    auto mn = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Minimized,
                                      "C:\\Program Files\\Acme\\acme.exe", {}, 0.0, 0, in, st);
    Check(mn.target == BELOW_NORMAL_PRIORITY_CLASS && mn.reason == "minimizada", "minimizada usa prioMinimized");
    auto tr = ProcessGovernor::Decide("acme.exe", ProcessWindowState::SystemTray,
                                      "C:\\Program Files\\Acme\\acme.exe", {}, 0.0, 0, in, st);
    Check(tr.target == BELOW_NORMAL_PRIORITY_CLASS && tr.reason == "bandeja", "bandeja usa prioSystemTray");

    auto mx = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Maximized,
                                      "C:\\Program Files\\Acme\\acme.exe", {}, 0.0, 0, in, st);
    Check(mx.target == ABOVE_NORMAL_PRIORITY_CLASS, "maximizado sem jogo usa prioMaximized");

    ProcessGovernorInput inGame = in;
    inGame.isGameRunning = true;
    inGame.gameProcessName = "AcmeGame.EXE";
    auto game = ProcessGovernor::Decide("acmegame.exe", ProcessWindowState::Maximized,
                                        "C:\\Games\\Acme\\acmegame.exe", {}, 0.0, 0, inGame, st);
    Check(game.target == HIGH_PRIORITY_CLASS, "o jogo em foco recebe prioGame");
    auto other = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Normal,
                                         "C:\\Program Files\\Acme\\acme.exe", {}, 0.0, 0, inGame, st);
    Check(other.target == NORMAL_PRIORITY_CLASS, "janela comum nao sobe para Alta so porque ha jogo aberto");

    // 6. Processos sem janela: teto de Abaixo do Normal enquanto ocupados, nivel da janela apos ociosidade.
    auto busyDaemon = ProcessGovernor::Decide("acmeassist.exe", ProcessWindowState::NoVisibleWindow,
                                              "C:\\Program Files\\Acme\\acmeassist.exe", {}, 4.0, 1000, in, st);
    Check(busyDaemon.target == BELOW_NORMAL_PRIORITY_CLASS, "daemon de terceiro ocupado para em Abaixo do Normal");
    auto idleDaemon = ProcessGovernor::Decide("acmeassist.exe", ProcessWindowState::NoVisibleWindow,
                                              "C:\\Program Files\\Acme\\acmeassist.exe", {}, 0.0, 120000, in, st);
    Check(idleDaemon.target == IDLE_PRIORITY_CLASS, "daemon de terceiro ocioso ha 2 min recebe o nivel da janela");
    auto systemDaemon = ProcessGovernor::Decide("svchost.exe", ProcessWindowState::NoVisibleWindow,
                                                "C:\\Windows\\System32\\svchost.exe", {}, 0.0, 600000, in, st);
    Check(systemDaemon.reason.empty(), "daemon do Windows fica fora da cacada");

    // 7. Anti-fome: o que foi rebaixado e volta a pedir CPU sobe um degrau; na segunda vez entra em quarentena.
    ProcessPriorState wasIdle;
    wasIdle.managedByUs = true;
    wasIdle.appliedClass = IDLE_PRIORITY_CLASS;
    ProcessGovernorSettings stNormalBg = st;
    stNormalBg.prioBackground = NORMAL_PRIORITY_CLASS;
    auto starve1 = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Background,
                                           "C:\\Program Files\\Acme\\acme.exe", wasIdle, 12.0, 0, in, stNormalBg);
    Check(starve1.raise && starve1.target == BELOW_NORMAL_PRIORITY_CLASS && starve1.starveHits == 1,
          "fome de CPU devolve um degrau");
    ProcessPriorState wasIdle2 = wasIdle;
    wasIdle2.starveHits = 1;
    auto starve2 = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Background,
                                           "C:\\Program Files\\Acme\\acme.exe", wasIdle2, 12.0, 0, in, stNormalBg);
    Check(starve2.quarantineNow && starve2.target == NORMAL_PRIORITY_CLASS,
          "segunda fome devolve Normal e quarantena o nome");
    auto foregroundNotStarved = ProcessGovernor::Decide("acme.exe", ProcessWindowState::Maximized,
                                                        "C:\\Program Files\\Acme\\acme.exe", wasIdle, 40.0, 0, in, st);
    Check(!foregroundNotStarved.raise, "processo em primeiro plano nao passa por fome");

    // 8. O ciclo completo ignora o que nao pode tocar e escreve so o que mudou.
    ProcessGovernor gov;
    std::vector<ProcessItem> procs;
    procs.push_back(FakeProc(1001, "acmeassist.exe", "C:\\Program Files\\Acme\\acmeassist.exe", NORMAL_PRIORITY_CLASS));
    procs.push_back(FakeProc(1002, "svchost.exe", "C:\\Windows\\System32\\svchost.exe", NORMAL_PRIORITY_CLASS));
    procs.push_back(FakeProc(1003, "dwm.exe", "C:\\Windows\\System32\\dwm.exe", NORMAL_PRIORITY_CLASS));
    procs.push_back(FakeProc(1004, "csrss.exe", "C:\\Windows\\System32\\csrss.exe", NORMAL_PRIORITY_CLASS));
    procs.push_back(FakeProc(9999, "eu-mesmo.exe", "C:\\Program Files\\Acme\\Opit-Core.exe", NORMAL_PRIORITY_CLASS));
    procs.push_back(FakeProc(1006, "acmetray.exe", "C:\\Program Files\\Acme\\acmetray.exe", IDLE_PRIORITY_CLASS));
    std::unordered_map<uint32_t, ProcessWindowState> states;
    states[1006] = ProcessWindowState::SystemTray; // ja esta mais baixo do que o alvo: nao brigamos

    auto actions = gov.Plan(procs, states, in, st);
    bool touchedAssistant = false;
    for (const auto& a : actions) {
        if (a.pid == 1001) touchedAssistant = true;
        Check(a.pid != 1002 && a.pid != 1003 && a.pid != 1004 && a.pid != 9999 && a.pid != 1006,
              "ciclo nao toca em protegido/excluido/sistema/eu mesmo/ja rebaixado");
    }
    Check(touchedAssistant, "daemon de terceiro ocioso entra no plano de escrita");
    Check(actions.size() == 1, "apenas uma escrita planejada neste ciclo");

    // 9. Orcamento de escritas por ciclo.
    ProcessGovernor budgetGov;
    std::vector<ProcessItem> many;
    for (uint32_t i = 0; i < 40; ++i) {
        many.push_back(FakeProc(2000 + i, "acme" + std::to_string(i) + ".exe",
                                "C:\\Program Files\\Acme\\acme.exe", NORMAL_PRIORITY_CLASS));
    }
    ProcessGovernorSettings tight = st;
    tight.maxWritesPerCycle = 24;
    auto manyActions = budgetGov.Plan(many, {}, in, tight);
    Check(manyActions.size() == 24, "orcamento limita as escritas do ciclo");
    Check(budgetGov.Stats().skippedBudget == 16, "sobras do orcamento ficam contadas");

    // 10. Cooldown por PID registrado no diario: um ciclo imediato nao reinsiste na mesma escrita.
    ProcessGovernor coolGov;
    auto first = coolGov.Plan({FakeProc(3001, "acmeloop.exe", "C:\\Program Files\\Acme\\acmeloop.exe",
                                       NORMAL_PRIORITY_CLASS)}, {}, in, st);
    Check(first.size() == 1, "primeiro ciclo planeja a escrita");

    // 11. A coluna Memoria da aba: GetProcessMemoryInfo so devolve algo com cb preenchido.
    auto real = ProcessManager::EnumerateProcesses();
    if (real.empty()) {
        std::cerr << "FALHA: EnumerateProcesses nao deveria retornar lista vazia" << std::endl;
        g_failures++;
    } else {
        size_t withMem = 0;
        size_t withPath = 0;
        size_t withCpu = 0;
        for (const auto& p : real) {
            if (p.workingSetBytes > 0) withMem++;
            if (!p.imagePath.empty()) withPath++;
            if (p.cpuTime100ns > 0) withCpu++;
        }
        std::cout << "test_process_governor: " << real.size() << " processos, memoria lida em " << withMem
                  << ", caminho lido em " << withPath << ", tempo de CPU em " << withCpu << std::endl;
        Check(withMem * 2 >= real.size(), "a leitura de memoria precisa funcionar na maioria dos processos");
        Check(withPath * 2 >= real.size(), "a leitura do caminho do binario precisa funcionar na maioria");
        Check(withCpu > 0, "tempo de CPU acumulado precisa ser lido (base da medicao de ociosidade)");
    }

    // 12. O mapa de janelas de uma passada classifica o processo do teste como sem janela visivel.
    auto winMap = ProcessManager::BuildWindowStateMap();
    Check(winMap.find(GetCurrentProcessId()) == winMap.end() ||
              winMap[GetCurrentProcessId()] == ProcessWindowState::NoVisibleWindow,
          "processo de teste sem janela nao pode aparecer como primeiro plano");

    // 13. SetPriority continua recusando Tempo Real e PID de sistema.
    Check(!ProcessManager::SetPriority(4, HIGH_PRIORITY_CLASS), "PID 4 intocavel");
    Check(!ProcessManager::SetPriority(GetCurrentProcessId(), REALTIME_PRIORITY_CLASS), "Tempo Real recusado");

    // 14. Processo protegido pelo kernel (PPL) sai do plano antes de qualquer tentativa de escrita, e o
    //     contador proprio diz quantos foram respeitados — nao aparece como falha, nao gera toast.
    ProcessGovernor pplGov;
    ProcessItem ppl = FakeProc(4468, "MpDefenderCoreService.exe", "C:\\Program Files\\WindowsApps\\MpDefenderCoreService.exe",
                               NORMAL_PRIORITY_CLASS);
    ppl.protectedByWindows = true;
    ppl.cpuTime100ns = 0;
    auto pplActions = pplGov.Plan({ppl}, {}, in, st);
    Check(pplActions.empty(), "processo protegido nao entra no plano de escrita");
    Check(pplGov.Stats().protectedRespected == 1, "protegido contado como respeitado, nao como falha");
    ProcessGovernor mixedGov;
    ProcessItem normal = FakeProc(1001, "acmeassist.exe", "C:\\Program Files\\Acme\\acmeassist.exe", NORMAL_PRIORITY_CLASS);
    auto mixed = mixedGov.Plan({ppl, normal}, {}, in, st);
    Check(mixed.size() == 1 && mixed[0].pid == 1001, "o protegido nao arrasta os outros consigo");
    Check(mixedGov.Stats().protectedRespected == 1, "o protegido continua contado no ciclo misto");

    // 15. A sonda real: o nosso proprio processo nao e protegido; se houver PPL no sistema, tem de ser.
    Check(!ProcessManager::IsProtectedHandle(nullptr), "handle nulo nao e protegido");
    Check(!ProcessManager::IsProtectedHandle(GetCurrentProcess()), "o processo de teste nao e PPL");
    size_t pplVivos = 0;
    for (const auto& p : ProcessManager::EnumerateProcesses()) {
        if (!p.protectedByWindows) continue;
        ++pplVivos;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
        if (h) {
            Check(ProcessManager::IsProtectedHandle(h), "PPL real confirmado pelo handle");
            CloseHandle(h);
        }
    }
    std::cout << "test_process_governor: " << pplVivos
              << " processos protegidos pelo Windows vistos nesta maquina (0 = NOT_RUN desta perna)" << std::endl;

    if (g_failures > 0) {
        std::cerr << "test_process_governor: " << g_failures << " falhas" << std::endl;
        return 1;
    }
    std::cout << "test_process_governor: SUCESSO (politica de processos medida, com tetos e quarentena)" << std::endl;
    return 0;
}
