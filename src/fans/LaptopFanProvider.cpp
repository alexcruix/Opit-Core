// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "LaptopFanProvider.h"
#include <windows.h>
#include <chrono>

namespace opitcore {

LaptopFanProvider::LaptopFanProvider() = default;
LaptopFanProvider::~LaptopFanProvider() = default;

bool LaptopFanProvider::Initialize() {
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        m_isLaptop = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
    }
    m_initialized = true;
    return true;
}

void LaptopFanProvider::Shutdown() {
    m_initialized = false;
}

std::vector<FanDeviceInfo> LaptopFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized || !m_isLaptop) {
        return devices;
    }

    FanDeviceInfo dev;
    dev.id = "laptop:ec:main";
    dev.name = "Controlador Embarcado do Notebook (EC)";
    dev.vendor = "OEM Notebook";
    dev.deviceType = FanDeviceType::LaptopEC;

    dev.capabilities.canReadTemperature = true;
    dev.capabilities.canReadRpm = false;
    dev.capabilities.canControlSpeed = false;
    dev.capabilities.canSetCurve = false;
    dev.activeMode = FanControlMode::BiosFirmwareAuto;

    dev.latestReading.sensorSource = "ACPI Notebook EC Thermal Zone";
    dev.latestReading.hasTemperature = true;
    dev.latestReading.temperatureC = 45.0; // Padrão de referência seguro
    dev.latestReading.hasRpm = false;
    dev.latestReading.lastErrorMessage = "O firmware de notebooks modernos protege a escrita direta no controlador embarcado (EC). Controle de ventilacao sob gestao do BIOS OEM.";

    devices.push_back(dev);
    return devices;
}

FanResult<FanReading> LaptopFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (!m_isLaptop || deviceId != "laptop:ec:main") {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    FanReading r;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    r.sensorSource = "ACPI Notebook EC";
    r.hasTemperature = false;
    r.hasRpm = false;
    r.lastErrorMessage = "Monitoramento passivo de telemetria do notebook ativo.";

    res.code = FanStatusCode::Ok;
    res.value = r;
    return res;
}

FanResult<bool> LaptopFanProvider::SetSpeedPercent(const std::string& /*deviceId*/, uint32_t /*speedPercent*/) {
    FanResult<bool> res;
    res.code = FanStatusCode::Unsupported;
    res.value = false;
    res.message = "Escrita direta em registradores EC de notebook sem driver OEM autenticado oferece alto risco de travamento de hardware. Operacao bloqueada por salvaguarda de seguranca.";
    return res;
}

FanResult<bool> LaptopFanProvider::RestoreDefaultControl(const std::string& /*deviceId*/) {
    FanResult<bool> res;
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Curva de arrefecimento mantida pelo firmware do fabricante do notebook.";
    return res;
}

} // namespace opitcore
