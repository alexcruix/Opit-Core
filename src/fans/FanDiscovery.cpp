#include "FanDiscovery.h"
#include <unordered_set>

namespace opitcore {

FanDiscovery::FanDiscovery() = default;

FanDiscovery::~FanDiscovery() {
    ShutdownAll();
}

void FanDiscovery::RegisterProvider(std::shared_ptr<IFanProvider> provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (provider) {
        m_providers.push_back(provider);
    }
}

void FanDiscovery::InitializeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_providers) {
        p->Initialize();
    }
}

void FanDiscovery::ShutdownAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_providers) {
        p->Shutdown();
    }
    m_deviceProviderMap.clear();
}

std::vector<FanDeviceInfo> FanDiscovery::DiscoverAllDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FanDeviceInfo> allDevices;
    std::unordered_set<std::string> seenIds;
    m_deviceProviderMap.clear();

    for (auto& p : m_providers) {
        auto devs = p->EnumerateDevices();
        for (auto& d : devs) {
            if (seenIds.find(d.id) == seenIds.end()) {
                seenIds.insert(d.id);
                m_deviceProviderMap[d.id] = p;
                allDevices.push_back(d);
            }
        }
    }
    return allDevices;
}

std::shared_ptr<IFanProvider> FanDiscovery::GetProviderForDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_deviceProviderMap.find(deviceId);
    if (it != m_deviceProviderMap.end()) {
        return it->second;
    }
    for (auto& p : m_providers) {
        auto devs = p->EnumerateDevices();
        for (const auto& d : devs) {
            if (d.id == deviceId) {
                m_deviceProviderMap[deviceId] = p;
                return p;
            }
        }
    }
    return nullptr;
}

} // namespace opitcore
