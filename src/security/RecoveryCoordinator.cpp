#include "RecoveryCoordinator.h"
#include <windows.h>
#include <shellapi.h>
#include <sstream>

#pragma comment(lib, "shell32.lib")

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
    HINSTANCE hRes = ShellExecuteA(nullptr, "open", GetOfficialRepairUrl(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hRes) > 32;
}

} // namespace security
} // namespace opitcore
