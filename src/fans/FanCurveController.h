#pragma once
#include "FanTypes.h"
#include <unordered_map>
#include <mutex>

namespace opitcore {

class FanCurveController {
public:
    FanCurveController();
    ~FanCurveController();

    // Calcula velocidade recomendada (0 a 100%) baseado na curva e temperatura atual
    uint32_t EvaluateSpeed(const std::string& deviceId, const FanCurveConfig& curve, double currentTempC);

    // Validação de sanidade da curva
    static bool ValidateCurve(const FanCurveConfig& curve, std::string& outError);

    // Reset de estado de histerese
    void ResetDeviceState(const std::string& deviceId);

private:
    struct DeviceCurveState {
        double lastAppliedTempC = -1.0;
        uint32_t lastAppliedSpeed = 0;
    };

    std::unordered_map<std::string, DeviceCurveState> m_deviceStates;
    std::mutex m_mutex;
};

} // namespace opitcore
