// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanProvider.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace opitcore {

// Gémeo Linux dos providers ACPI / NVAPI / ADL / Intel-GPU. O kernel já expõe os mesmos sensores
// e o mesmo comando de ventoinha por sysfs: fanN_input (RPM), tempN_input (miligraus), pwmN (duty
// 0..pwmN_max) e pwmN_enable (0 = firmware/BIOS, 1 = manual). Nada aqui é simulado: se o chip não
// publica pwmN, o provider devolve Unsupported em vez de fingir que controla.
//
// A raiz é parametrizada exactamente para o teste poder apontar para um fixture: escrever no
// /sys real durante uma medição é proibido, e um caminho fixo dentro do código não dava essa
// garantia.
class HwmonFanProvider : public IFanProvider {
public:
    explicit HwmonFanProvider(std::string sysfsRoot = "/sys/class/hwmon");
    ~HwmonFanProvider() override;

    std::string GetProviderName() const override { return "Linux hwmon (sysfs)"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

    // Exposto para teste: quantos dos aparelhos publicados aceitam escrita manual. Serve para
    // distinguir "não há ventoinha" de "há ventoinha mas o driver não deixa encostar no duty".
    size_t DevicesWithPwmControl() const;

private:
    struct FanTarget {
        std::string id;            // "hwmon:hwmon0:fan1"
        std::string dir;           // <raiz>/hwmon0
        std::string chip;          // conteúdo de <raiz>/hwmon0/name, em minúsculas
        std::string fanInput;      // vazio = só temperatura
        std::string tempInput;     // vazio = só ventoinha
        std::string tempLabel;
        std::string pwmFile;       // vazio = sem controlo manual
        std::string pwmEnableFile;
        long long pwmMax = 255;
        long long pwmMin = 0;
    };

    bool ReadInt(const std::string& path, long long& out) const;
    bool WriteInt(const std::string& path, long long value, FanStatusCode& codeOut, std::string& msgOut) const;
    std::vector<FanTarget> Scan() const;
    // Reconstrói o mapa a partir do sysfs. Chamar sem trava (os métodos públicos já estão no
    // único thread de amostragem do FanControlManager, como os restantes providers).
    const FanTarget* ResolveTarget(const std::string& deviceId);
    FanDeviceType ClassifyChip(const std::string& chip) const;

    std::string m_root;
    std::unordered_map<std::string, FanTarget> m_targets;
    bool m_initialized = false;
};

} // namespace opitcore
