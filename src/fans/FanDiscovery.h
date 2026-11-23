// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanTypes.h"
#include "FanProvider.h"
#include <vector>
#include <memory>
#include <mutex>

#include <unordered_map>

namespace opitcore {

class FanDiscovery {
public:
    FanDiscovery();
    ~FanDiscovery();

    void RegisterProvider(std::shared_ptr<IFanProvider> provider);
    void InitializeAll();
    void ShutdownAll();

    std::vector<FanDeviceInfo> DiscoverAllDevices();
    std::shared_ptr<IFanProvider> GetProviderForDevice(const std::string& deviceId);

private:
    std::vector<std::shared_ptr<IFanProvider>> m_providers;
    std::unordered_map<std::string, std::shared_ptr<IFanProvider>> m_deviceProviderMap;
    std::mutex m_mutex;
};

} // namespace opitcore
