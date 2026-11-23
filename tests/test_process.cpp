// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "platform/windows/ProcessManager.h"
#include <iostream>
#include <windows.h>

int main() {
    // 1. Processos críticos devem ser protegidos
    if (!opitcore::ProcessManager::IsCriticalProcess("csrss.exe")) {
        std::cerr << "Falha: csrss.exe deve ser critico" << std::endl;
        return 1;
    }
    if (!opitcore::ProcessManager::IsCriticalProcess("services.exe")) {
        std::cerr << "Falha: services.exe deve ser critico" << std::endl;
        return 1;
    }
    if (!opitcore::ProcessManager::IsCriticalProcess("lsass.exe")) {
        std::cerr << "Falha: lsass.exe deve ser critico" << std::endl;
        return 1;
    }
    if (!opitcore::ProcessManager::IsCriticalProcess("explorer.exe")) {
        std::cerr << "Falha: explorer.exe deve ser critico" << std::endl;
        return 1;
    }
    if (opitcore::ProcessManager::IsCriticalProcess("notepad.exe")) {
        std::cerr << "Falha: notepad.exe nao deve ser critico" << std::endl;
        return 1;
    }

    // 2. Proteção contra Realtime Priority
    if (opitcore::ProcessManager::SetPriority(GetCurrentProcessId(), REALTIME_PRIORITY_CLASS)) {
        std::cerr << "Falha: SetPriority nao deve permitir REALTIME_PRIORITY_CLASS" << std::endl;
        return 1;
    }

    // 3. Enumeração de processos
    auto procs = opitcore::ProcessManager::EnumerateProcesses();
    if (procs.empty()) {
        std::cerr << "Falha: EnumerateProcesses retornou lista vazia" << std::endl;
        return 1;
    }

    bool foundCurrent = false;
    uint32_t currentPid = GetCurrentProcessId();
    for (const auto& p : procs) {
        if (p.pid == currentPid) {
            foundCurrent = true;
            break;
        }
    }
    if (!foundCurrent) {
        std::cerr << "Falha: Processo atual nao encontrado na lista" << std::endl;
        return 1;
    }

    std::cout << "test_process: SUCESSO (" << procs.size() << " processos enumerados com protecao de seguranca)" << std::endl;
    return 0;
}
