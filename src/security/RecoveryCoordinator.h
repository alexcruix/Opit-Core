// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <vector>
#include "SecurityTypes.h"

namespace opitcore {
namespace security {

class RecoveryCoordinator {
public:
    static RecoveryCoordinator& Instance();

    /**
     * Gera mensagem estruturada para exibição na UI ou no log em caso de violação de integridade.
     */
    std::string BuildRecoveryNotice(const std::vector<SecurityEvent>& events) const;

    /**
     * Abre com segurança a página oficial de download/reparo da ACSTC Lab no navegador padrão.
     */
    bool LaunchOfficialRepairPortal() const;

    /**
     * Retorna a URL oficial de reparo e recuperação.
     */
    const char* GetOfficialRepairUrl() const { return "https://www.acstclab.com/downloads/repair"; }

private:
    RecoveryCoordinator() = default;
    ~RecoveryCoordinator() = default;
};

} // namespace security
} // namespace opitcore
