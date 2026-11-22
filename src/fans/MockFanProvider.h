#pragma once
#include "FanProvider.h"

namespace opitcore {

class MockFanProvider : public IFanProvider {
public:
    MockFanProvider();
    ~MockFanProvider() override;

    std::string GetProviderName() const override { return "Virtual Test Mock Provider"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

    // Métodos de controle de simulação para testes
    void SetMockTemperature(double tempC);
    void SetSimulateReadError(bool enable);
    void SetSimulateCommandReject(bool enable);
    uint32_t GetCurrentAppliedSpeedPercent() const { return m_currentSpeedPercent; }

private:
    bool m_initialized = false;
    double m_mockTempC = 45.0;
    uint32_t m_currentSpeedPercent = 50;
    bool m_simulateReadError = false;
    bool m_simulateCommandReject = false;
};

} // namespace opitcore
