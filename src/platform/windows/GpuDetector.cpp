// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "GpuDetector.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")

namespace opitcore {

std::vector<GpuDetails> GpuDetector::DetectGpus() {
    std::vector<GpuDetails> gpus;
    IDXGIFactory* pFactory = nullptr;

    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (FAILED(hr) || !pFactory) {
        return gpus;
    }

    IDXGIAdapter* pAdapter = nullptr;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
            GpuDetails details;
            details.description = StringUtils::ToUtf8(desc.Description);
            details.dedicatedVideoMemoryMB = desc.DedicatedVideoMemory / (1024 * 1024);
            details.sharedSystemMemoryMB = desc.SharedSystemMemory / (1024 * 1024);
            details.vendorId = desc.VendorId;
            details.deviceId = desc.DeviceId;
            // Ignorar renderizador de software básico do Windows da lista principal ou marcar
            // Microsoft Basic Render Driver tem vendorId 0x1414
            details.isDedicated = (desc.DedicatedVideoMemory > 128 * 1024 * 1024) && (desc.VendorId != 0x1414);
            details.origin = DataOrigin::Detected;
            details.confidence = DataConfidence::Verified;
            gpus.push_back(details);
        }
        pAdapter->Release();
    }

    pFactory->Release();
    return gpus;
}

} // namespace opitcore
