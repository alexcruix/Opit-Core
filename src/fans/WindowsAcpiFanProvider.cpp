// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "WindowsAcpiFanProvider.h"
#include "hardware/HardwareDetection.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <chrono>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace opitcore {

WindowsAcpiFanProvider::WindowsAcpiFanProvider() = default;

WindowsAcpiFanProvider::~WindowsAcpiFanProvider() {
    Shutdown();
}

bool WindowsAcpiFanProvider::Initialize() {
    m_initialized = true;
    return true;
}

void WindowsAcpiFanProvider::Shutdown() {
    m_initialized = false;
}

double WindowsAcpiFanProvider::QueryAcpiTemperature(bool& outHasSensor) {
    outHasSensor = false;
    if (m_acpiUnsupported) {
        return 0.0;
    }
    double tempC = 0.0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hr);

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = nullptr;
        BSTR bstrNamespace = SysAllocString(L"ROOT\\WMI");
        hr = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
        SysFreeString(bstrNamespace);

        if (SUCCEEDED(hr) && pSvc) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

            BSTR bstrWql = SysAllocString(L"WQL");
            BSTR bstrQuery = SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
            IEnumWbemClassObject* pEnumerator = nullptr;
            hr = pSvc->ExecQuery(bstrWql, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator);
            SysFreeString(bstrWql);
            SysFreeString(bstrQuery);

            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject* pclsObj = nullptr;
                ULONG uReturn = 0;
                if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    if (SUCCEEDED(pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0))) {
                        if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                            long raw = (vtProp.vt == VT_I4) ? vtProp.lVal : (long)vtProp.ulVal;
                            tempC = (raw - 2732) / 10.0;
                            if (tempC > 0.0 && tempC < 130.0) {
                                outHasSensor = true;
                            }
                        }
                        VariantClear(&vtProp);
                    }
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
            pSvc->Release();
        }
        pLoc->Release();
    }

    if (shouldUninit) {
        CoUninitialize();
    }

    if (!outHasSensor) {
        m_acpiUnsupported = true;
    }

    return tempC;
}

std::vector<FanDeviceInfo> WindowsAcpiFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;

    FanDeviceInfo cpuDevice;
    cpuDevice.id = "mb:acpi:cpu";
    auto cpu = HardwareDetection::DetectCpu();
    cpuDevice.name = cpu.brand.empty() ? "Processador do Sistema (CPU)" : cpu.brand;
    cpuDevice.vendor = cpu.vendor.empty() ? "Placa-Mae / ACPI" : cpu.vendor;
    cpuDevice.deviceType = FanDeviceType::CpuMotherboard;

    // Capacidades honestas no modo usuario do Windows:
    // Leitura termica ACPI: Sim
    // RPM: Apenas se houver sensor WMI documentado, senao indisponivel
    // Controle direto de PWM: Nao em modo usuario (delegado ao BIOS)
    cpuDevice.capabilities.canReadTemperature = true;
    cpuDevice.capabilities.canReadRpm = false;
    cpuDevice.capabilities.canControlSpeed = false;
    cpuDevice.capabilities.canSetCurve = false;
    cpuDevice.activeMode = FanControlMode::BiosFirmwareAuto;

    bool hasSensor = false;
    double t = QueryAcpiTemperature(hasSensor);
    cpuDevice.latestReading.hasTemperature = hasSensor;
    cpuDevice.latestReading.temperatureC = hasSensor ? t : -1.0;
    cpuDevice.latestReading.sensorSource = "ACPI ROOT\\WMI MSAcpi_ThermalZoneTemperature";
    cpuDevice.latestReading.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    devices.push_back(cpuDevice);
    return devices;
}

FanResult<FanReading> WindowsAcpiFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (deviceId != "mb:acpi:cpu") {
        res.code = FanStatusCode::Unavailable;
        res.message = "Dispositivo nao reconhecido por este provedor.";
        return res;
    }

    bool hasSensor = false;
    double t = QueryAcpiTemperature(hasSensor);

    FanReading r;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    r.sensorSource = "ACPI ROOT\\WMI MSAcpi_ThermalZoneTemperature";
    r.hasTemperature = hasSensor;
    r.temperatureC = hasSensor ? t : -1.0;
    r.hasRpm = false;
    r.rpm = 0;
    r.hasReportedSpeedPercent = false;

    if (!hasSensor) {
        r.lastErrorMessage = "Zona termica ACPI nao reportou leitura valida ou sensor indisponivel no firmware.";
    }

    res.code = hasSensor ? FanStatusCode::Ok : FanStatusCode::ReadError;
    res.value = r;
    return res;
}

FanResult<bool> WindowsAcpiFanProvider::SetSpeedPercent(const std::string& /*deviceId*/, uint32_t /*speedPercent*/) {
    FanResult<bool> res;
    res.code = FanStatusCode::Unsupported;
    res.value = false;
    res.message = "O controle direto de rotacao de conectores PWM da placa-mae em modo usuario nao e suportado pelo Windows nativo. Gerenciado com seguranca pelo BIOS.";
    return res;
}

FanResult<bool> WindowsAcpiFanProvider::RestoreDefaultControl(const std::string& /*deviceId*/) {
    FanResult<bool> res;
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "O controle termico da CPU permanece sob autoridade do firmware/BIOS.";
    return res;
}

} // namespace opitcore
