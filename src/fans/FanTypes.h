// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace opitcore {

enum class FanDeviceType {
    CpuMotherboard,
    GpuNvidia,
    GpuAmd,
    GpuIntel,
    LaptopEC,
    VirtualMock
};

inline const char* FanDeviceTypeToString(FanDeviceType type) {
    switch (type) {
        case FanDeviceType::CpuMotherboard: return "CPU / Placa-Mãe";
        case FanDeviceType::GpuNvidia: return "GPU NVIDIA";
        case FanDeviceType::GpuAmd: return "GPU AMD";
        case FanDeviceType::GpuIntel: return "GPU Intel";
        case FanDeviceType::LaptopEC: return "Notebook / EC";
        case FanDeviceType::VirtualMock: return "Dispositivo de Teste (Mock)";
        default: return "Desconhecido";
    }
}

enum class FanControlMode {
    BiosFirmwareAuto = 0,
    ManualFixed = 1,
    CustomCurve = 2
};

inline const char* FanControlModeToString(FanControlMode mode) {
    switch (mode) {
        case FanControlMode::BiosFirmwareAuto: return "Automático (Firmware / BIOS)";
        case FanControlMode::ManualFixed: return "Velocidade Fixa (Manual)";
        case FanControlMode::CustomCurve: return "Curva Personalizada";
        default: return "Indefinido";
    }
}

enum class FanStatusCode {
    Ok = 0,
    Unsupported = 1,
    Unavailable = 2,
    AccessDenied = 3,
    ReadError = 4,
    CommandRejected = 5,
    Unverified = 6
};

inline const char* FanStatusCodeToString(FanStatusCode code) {
    switch (code) {
        case FanStatusCode::Ok: return "Sucesso";
        case FanStatusCode::Unsupported: return "Recurso Não Suportado";
        case FanStatusCode::Unavailable: return "Dispositivo Indisponível";
        case FanStatusCode::AccessDenied: return "Permissão Insuficiente (Acesso Negado)";
        case FanStatusCode::ReadError: return "Falha de Leitura";
        case FanStatusCode::CommandRejected: return "Comando Rejeitado pelo Hardware";
        case FanStatusCode::Unverified: return "Resultado Não Verificável";
        default: return "Erro Desconhecido";
    }
}

template<typename T>
struct FanResult {
    FanStatusCode code = FanStatusCode::Ok;
    T value{};
    std::string message;

    bool IsSuccess() const { return code == FanStatusCode::Ok; }
};

struct FanCapabilities {
    bool canReadTemperature = false;
    bool canReadRpm = false;
    bool canControlSpeed = false;
    bool canSetCurve = false;
    uint32_t minSpeedPercent = 0;
    uint32_t maxSpeedPercent = 100;
};

struct FanReading {
    uint64_t timestampMs = 0;
    double temperatureC = -1.0;
    bool hasTemperature = false;
    uint32_t rpm = 0;
    bool hasRpm = false;
    uint32_t reportedSpeedPercent = 0;
    bool hasReportedSpeedPercent = false;
    bool isStale = false;
    std::string sensorSource;
    std::string lastErrorMessage;
};

struct FanCurvePoint {
    double tempC = 0.0;
    uint32_t speedPercent = 0;

    bool operator<(const FanCurvePoint& other) const {
        return tempC < other.tempC;
    }
};

struct FanCurveConfig {
    std::string referenceSensor = "CPU";
    std::vector<FanCurvePoint> points = {
        {35.0, 30},
        {50.0, 45},
        {65.0, 65},
        {75.0, 85},
        {85.0, 100}
    };
    int hysteresisC = 3;
    double emergencyTempC = 90.0;

    void Validate() {
        hysteresisC = std::clamp(hysteresisC, 1, 10);
        emergencyTempC = std::clamp(emergencyTempC, 75.0, 105.0);
        if (points.empty()) {
            points = {{40.0, 30}, {70.0, 70}, {85.0, 100}};
        }
        std::sort(points.begin(), points.end());
        for (auto& p : points) {
            p.speedPercent = std::clamp(p.speedPercent, 0u, 100u);
            p.tempC = std::clamp(p.tempC, 20.0, 110.0);
        }
        // O clamp pode colapsar pontos distintos no mesmo °C: fica um por temperatura,
        // com a velocidade mais alta (conservador) para a interpolacao nao degenerar.
        std::vector<FanCurvePoint> dedup;
        for (const auto& p : points) {
            if (!dedup.empty() && dedup.back().tempC == p.tempC) {
                dedup.back().speedPercent = std::max(dedup.back().speedPercent, p.speedPercent);
            } else {
                dedup.push_back(p);
            }
        }
        points = std::move(dedup);
    }
};

struct FanDeviceInfo {
    std::string id;
    std::string name;
    std::string vendor;
    FanDeviceType deviceType = FanDeviceType::CpuMotherboard;
    FanCapabilities capabilities;
    FanReading latestReading;
    FanControlMode activeMode = FanControlMode::BiosFirmwareAuto;
    uint32_t manualTargetSpeedPercent = 50;
    FanCurveConfig curveConfig;
};

} // namespace opitcore
