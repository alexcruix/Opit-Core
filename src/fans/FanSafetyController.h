#pragma once
#include "FanTypes.h"
#include <unordered_map>
#include <mutex>
#include <string>

namespace opitcore {

enum class FanSafetyAction {
    Allow,
    ClampToSafeMinimum,
    ForceEmergencyFullSpeed,
    FallbackToFirmware
};

class FanSafetyController {
public:
    FanSafetyController();
    ~FanSafetyController();

    // Limiares termicos vindos da configuracao da usuaria (adv.thermal); padroes de fabrica 90/80.
    void SetThermalThresholds(double criticalLimitC, double alertLimitC);

    // Avalia a segurança do comando pretendido
    FanSafetyAction EvaluateSafety(const FanDeviceInfo& device,
                                  const FanReading& reading,
                                  uint32_t requestedSpeed,
                                  uint32_t& outSafeSpeed,
                                  std::string& outReason);

    void RecordReadingSuccess(const std::string& deviceId);
    void RecordReadingFailure(const std::string& deviceId);

private:
    std::unordered_map<std::string, int> m_consecutiveErrors;
    std::mutex m_mutex;
    double m_criticalLimitC = 90.0;
    double m_alertLimitC = 80.0;
};

} // namespace opitcore
