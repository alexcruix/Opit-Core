// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "FanSafetyController.h"
#include <algorithm>

namespace opitcore {

FanSafetyController::FanSafetyController() = default;
FanSafetyController::~FanSafetyController() = default;

void FanSafetyController::RecordReadingSuccess(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consecutiveErrors[deviceId] = 0;
}

void FanSafetyController::RecordReadingFailure(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consecutiveErrors[deviceId]++;
}

void FanSafetyController::SetThermalThresholds(double criticalLimitC, double alertLimitC) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_criticalLimitC = criticalLimitC;
    m_alertLimitC = alertLimitC;
}

FanSafetyAction FanSafetyController::EvaluateSafety(const FanDeviceInfo& device,
                                                    const FanReading& reading,
                                                    uint32_t requestedSpeed,
                                                    uint32_t& outSafeSpeed,
                                                    std::string& outReason) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Falha de Sensor ou Amostra Desatualizada
    int errorCount = m_consecutiveErrors[device.id];
    if (errorCount >= 3 || reading.isStale || !reading.hasTemperature) {
        outSafeSpeed = 100;
        outReason = "Sensor termico com falha ou desatualizado (" + std::to_string(errorCount) +
                    " falhas consecutivas). Acionando retorno de seguranca ao firmware.";
        return FanSafetyAction::FallbackToFirmware;
    }

    double temp = reading.temperatureC;

    // 2. Proteção Térmica Crítica de Emergência (criticalLimitC da configuracao, fabrica 90°C)
    if (temp >= m_criticalLimitC) {
        outSafeSpeed = 100;
        outReason = "Temperatura critica detectada (" + std::to_string(static_cast<int>(temp)) +
                    "°C >= " + std::to_string(static_cast<int>(m_criticalLimitC)) +
                    "°C). Salvaguarda termica forcou 100% de ventilacao.";
        return FanSafetyAction::ForceEmergencyFullSpeed;
    }

    // 3. Piso Térmico Elevado (alertLimitC da configuracao, fabrica 80°C) -> Mínimo 70% de rotação
    if (temp >= m_alertLimitC && requestedSpeed < 70) {
        outSafeSpeed = 70;
        outReason = "Temperatura elevada (" + std::to_string(static_cast<int>(temp)) +
                    "°C >= " + std::to_string(static_cast<int>(m_alertLimitC)) +
                    "°C). Velocidade minima elevada para 70% por seguranca.";
        return FanSafetyAction::ClampToSafeMinimum;
    }

    // 4. Piso de Hardware Mínimo Permitido
    uint32_t minAllowed = (device.capabilities.minSpeedPercent > 0) ? device.capabilities.minSpeedPercent : 20;
    if (requestedSpeed < minAllowed) {
        outSafeSpeed = minAllowed;
        outReason = "Velocidade solicitada (" + std::to_string(requestedSpeed) +
                    "%) abaixo do piso de seguranca (" + std::to_string(minAllowed) + "%). Ajustado para o piso.";
        return FanSafetyAction::ClampToSafeMinimum;
    }

    // 5. Comando Totalmente Seguro
    outSafeSpeed = std::clamp(requestedSpeed, minAllowed, 100u);
    outReason = "Comando validado dentro dos limites operacionais seguros.";
    return FanSafetyAction::Allow;
}

} // namespace opitcore
