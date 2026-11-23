// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "RecoveryCoordinator.h"
#include <sstream>

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

#else

#include <sys/wait.h>
#include <unistd.h>

#endif

namespace opitcore {
namespace security {

RecoveryCoordinator& RecoveryCoordinator::Instance() {
    static RecoveryCoordinator instance;
    return instance;
}

std::string RecoveryCoordinator::BuildRecoveryNotice(const std::vector<SecurityEvent>& events) const {
    std::ostringstream oss;
    oss << "[OPIT-CORE DEFENSE] Atencao: A integridade da instalacao nao pode ser confirmada com seguranca.\n\n";

    for (const auto& ev : events) {
        if (ev.category == SecurityCategory::CategoryC || ev.category == SecurityCategory::CategoryB) {
            oss << " - Componente: " << (ev.component.empty() ? "Sistema Principal" : ev.component) << "\n";
            oss << "   Status: " << IntegrityStatusToString(ev.status) << "\n";
            oss << "   Motivo: " << ev.description << "\n\n";
        }
    }

    oss << "Por medidas de seguranca contra violacoes e adulteracoes, os recursos avancados foram suspensos.\n";
    oss << "Para restaurar o Opit-Core com integridade certificada, utilize o instalador oficial assinado em:\n";
    oss << GetOfficialRepairUrl() << "\n";

    return oss.str();
}

bool RecoveryCoordinator::LaunchOfficialRepairPortal() const {
#ifdef _WIN32
    HINSTANCE hRes = ShellExecuteA(nullptr, "open", GetOfficialRepairUrl(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hRes) > 32;
#else
    // xdg-open e o equivalente do "open" do ShellExecuteA: entrega o URL ao desktop do utilizador.
    // Se o binario nao existir, o filho sai com 127 e a funcao devolve false — nao ha sucesso
    // inventado para o portal de reparo.
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        execlp("xdg-open", "xdg-open", GetOfficialRepairUrl(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

} // namespace security
} // namespace opitcore
