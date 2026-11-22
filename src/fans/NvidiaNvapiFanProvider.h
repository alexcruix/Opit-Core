#pragma once
#include "FanProvider.h"
#include <windows.h>
#include <vector>

namespace opitcore {

class NvidiaNvapiFanProvider : public IFanProvider {
public:
    NvidiaNvapiFanProvider();
    ~NvidiaNvapiFanProvider() override;

    std::string GetProviderName() const override { return "NVIDIA NVAPI Fan Provider"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    HMODULE m_hNvApiDll = nullptr;
    bool m_initialized = false;
    std::vector<void*> m_nvPhysicalGpus;

    void* QueryNvInterface(unsigned int id);
};

} // namespace opitcore
