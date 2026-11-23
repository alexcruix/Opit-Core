// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include "SecurityTypes.h"
#include <string>
#include <vector>

namespace opitcore {
namespace security {

class SecurityPolicy {
public:
    static SecurityCategory EvaluateStatus(IntegrityStatus status, bool isCriticalComponent);

    /**
     * Avalia o conjunto de resultados de integridade e determina a resposta global do sistema.
     * @param events Lista de eventos detectados
     * @param outShouldBlockExecution Retorna true se a execução de recursos sensíveis deve ser impedida
     * @param outHighestCategory Retorna a categoria mais severa observada
     */
    static void EvaluateOverall(const std::vector<SecurityEvent>& events,
                                bool& outShouldBlockExecution,
                                SecurityCategory& outHighestCategory);
};

} // namespace security
} // namespace opitcore
