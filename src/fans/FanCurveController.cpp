#include "FanCurveController.h"
#include <cmath>
#include <algorithm>

namespace opitcore {

FanCurveController::FanCurveController() = default;
FanCurveController::~FanCurveController() = default;

bool FanCurveController::ValidateCurve(const FanCurveConfig& curve, std::string& outError) {
    if (curve.points.empty()) {
        outError = "A curva nao possui pontos definidos.";
        return false;
    }
    if (curve.points.size() < 2) {
        outError = "A curva precisa de pelo menos 2 pontos para interpolacao.";
        return false;
    }

    for (size_t i = 1; i < curve.points.size(); ++i) {
        if (curve.points[i].tempC <= curve.points[i - 1].tempC) {
            outError = "Os pontos de temperatura devem ser estritamente crescentes.";
            return false;
        }
    }

    for (const auto& p : curve.points) {
        if (p.speedPercent > 100) {
            outError = "Percentual de velocidade nao pode exceder 100%.";
            return false;
        }
    }

    return true;
}

void FanCurveController::ResetDeviceState(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_deviceStates.erase(deviceId);
}

uint32_t FanCurveController::EvaluateSpeed(const std::string& deviceId, const FanCurveConfig& curve, double currentTempC) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Salvaguarda Térmica Crítica da Curva (sem histerese: emergência é imediata)
    if (currentTempC >= curve.emergencyTempC) {
        m_deviceStates[deviceId] = {currentTempC, 100};
        return 100;
    }

    if (curve.points.empty()) {
        return 50;
    }

    // Alvo da curva: endpoints e interpolacao passam pela MESMA histerese; sem isso a
    // temperatura oscilando em cima do primeiro/ultimo ponto fazia a ventoinha flapping.
    uint32_t calculatedSpeed;
    if (currentTempC <= curve.points.front().tempC) {
        calculatedSpeed = curve.points.front().speedPercent;
    } else if (currentTempC >= curve.points.back().tempC) {
        calculatedSpeed = curve.points.back().speedPercent;
    } else {
        calculatedSpeed = 50;
        for (size_t i = 0; i < curve.points.size() - 1; ++i) {
            const auto& p1 = curve.points[i];
            const auto& p2 = curve.points[i + 1];

            if (currentTempC >= p1.tempC && currentTempC <= p2.tempC) {
                double rangeTemp = p2.tempC - p1.tempC;
                if (rangeTemp <= 0.001) {
                    calculatedSpeed = p2.speedPercent;
                } else {
                    double factor = (currentTempC - p1.tempC) / rangeTemp;
                    calculatedSpeed = static_cast<uint32_t>(p1.speedPercent + factor * (static_cast<double>(p2.speedPercent) - p1.speedPercent));
                }
                break;
            }
        }
        calculatedSpeed = std::clamp(calculatedSpeed, 0u, 100u);
    }

    // Histerese unica (bordas incluidas): caindo, so diminui a rotacao se a queda passar da histerese
    auto& state = m_deviceStates[deviceId];
    if (state.lastAppliedTempC > 0.0 &&
        currentTempC < state.lastAppliedTempC &&
        (state.lastAppliedTempC - currentTempC) < curve.hysteresisC) {
        return state.lastAppliedSpeed;
    }

    state.lastAppliedTempC = currentTempC;
    state.lastAppliedSpeed = calculatedSpeed;
    return calculatedSpeed;
}

} // namespace opitcore
