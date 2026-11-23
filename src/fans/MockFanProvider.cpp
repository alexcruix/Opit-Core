// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "MockFanProvider.h"
#include <chrono>

namespace opitcore {

MockFanProvider::MockFanProvider() = default;
MockFanProvider::~MockFanProvider() = default;

bool MockFanProvider::Initialize() {
    m_initialized = true;
    return true;
}

void MockFanProvider::Shutdown() {
    m_initialized = false;
}

void MockFanProvider::SetMockTemperature(double tempC) {
    m_mockTempC = tempC;
}

void MockFanProvider::SetSimulateReadError(bool enable) {
    m_simulateReadError = enable;
}

void MockFanProvider::SetSimulateCommandReject(bool enable) {
    m_simulateCommandReject = enable;
}

std::vector<FanDeviceInfo> MockFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized) return devices;

    FanDeviceInfo dev;
    dev.id = "mock:fan:0";
    dev.name = "Ventoinha Virtual Controlável (Mock)";
    dev.vendor = "OpitCore Mock Labs";
    dev.deviceType = FanDeviceType::VirtualMock;

    dev.capabilities.canReadTemperature = true;
    dev.capabilities.canReadRpm = true;
    dev.capabilities.canControlSpeed = true;
    dev.capabilities.canSetCurve = true;
    dev.capabilities.minSpeedPercent = 15;
    dev.capabilities.maxSpeedPercent = 100;
    dev.activeMode = FanControlMode::BiosFirmwareAuto;
    dev.manualTargetSpeedPercent = m_currentSpeedPercent;

    auto tel = ReadTelemetry(dev.id);
    if (tel.IsSuccess()) {
        dev.latestReading = tel.value;
    }

    devices.push_back(dev);
    return devices;
}

FanResult<FanReading> MockFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (!m_initialized || deviceId != "mock:fan:0") {
        res.code = FanStatusCode::Unavailable;
        res.message = "Dispositivo mock nao encontrado.";
        return res;
    }

    if (m_simulateReadError) {
        res.code = FanStatusCode::ReadError;
        res.message = "Falha simulada de leitura de sensor.";
        return res;
    }

    FanReading r;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    r.sensorSource = "Virtual Mock Thermal Engine";
    r.hasTemperature = true;
    r.temperatureC = m_mockTempC;
    r.hasRpm = true;
    // Estimativa de RPM simulada baseada no percentual (ex: 2000 RPM max * % / 100)
    r.rpm = static_cast<uint32_t>((2200 * m_currentSpeedPercent) / 100);
    r.hasReportedSpeedPercent = true;
    r.reportedSpeedPercent = m_currentSpeedPercent;

    res.code = FanStatusCode::Ok;
    res.value = r;
    return res;
}

FanResult<bool> MockFanProvider::SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    if (!m_initialized || deviceId != "mock:fan:0") {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (m_simulateCommandReject) {
        res.code = FanStatusCode::CommandRejected;
        res.value = false;
        res.message = "Comando de velocidade mock rejeitado para teste.";
        return res;
    }

    if (speedPercent < 15) speedPercent = 15;
    if (speedPercent > 100) speedPercent = 100;

    m_currentSpeedPercent = speedPercent;
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Velocidade mock ajustada com sucesso.";
    return res;
}

FanResult<bool> MockFanProvider::RestoreDefaultControl(const std::string& /*deviceId*/) {
    FanResult<bool> res;
    m_currentSpeedPercent = 50; // Padrão
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Controle restaurado para padrao mock.";
    return res;
}

} // namespace opitcore
