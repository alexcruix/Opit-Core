// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "ThermalProtection.h"

namespace opitcore {

ThermalProtection::ThermalProtection() = default;

void ThermalProtection::Reset() {
    m_isThrottling = false;
}

ThermalProtection::Action ThermalProtection::Evaluate(double tempC, bool hasSensor, double limitC) {
    if (!m_enabled) {
        // Protecao desligada pela usuaria: devolver o controle e legitimo.
        if (m_isThrottling) {
            m_isThrottling = false;
            return Action::Restore;
        }
        return Action::None;
    }

    // Fail-safe: sem sensor nao ha prova de temperatura. Nao restaurar "as cegas" um teto
    // termico ativo; o sensor voltando com leitura abaixo da histerese e quem autoriza o Restore.
    if (!hasSensor) {
        return Action::None;
    }

    // Se ultrapassou o limite e ainda não está em throttling, disparar Throttle
    if (tempC >= limitC) {
        if (!m_isThrottling) {
            m_isThrottling = true;
            return Action::Throttle;
        }
    } 
    // Histerese de 5°C abaixo do limite para restauração segura
    else if (tempC < (limitC - 5.0)) {
        if (m_isThrottling) {
            m_isThrottling = false;
            return Action::Restore;
        }
    }

    return Action::None;
}

} // namespace opitcore
