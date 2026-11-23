// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace opitcore {

struct PowerScheme {
    std::string guid;
    std::string name;
    bool isActive = false;
};

struct BatteryStatusInfo {
    bool hasBattery = false;
    bool isAcOnline = true;
    int batteryPercent = 100;
    bool isCharging = false;
};

// Classe de hardware perguntada ao proprio Windows: true quando ha bateria (notebook).
// Existe separada porque o perfil-padrao dos limites de CPU precisa dela antes de qualquer
// PowerManager existir (o banco de configuracoes e lido na inicializacao).
bool SystemHasBattery();

class PowerManager {
public:
    PowerManager();
    ~PowerManager();

    bool Initialize();
    std::vector<PowerScheme> ListSchemes();
    PowerScheme GetActiveScheme();
    bool SetActiveScheme(const std::string& guidStr);
    
    // Controle do estado do processador (Throttling / Performance cap)
    uint32_t GetProcessorThrottleLimit();
    bool SetProcessorThrottleLimit(uint32_t percent);
    bool SetProcessorThrottleRange(uint32_t minPercent, uint32_t maxPercent);
    bool SetCoreParking(uint32_t minCoresPercent);

    // Métodos granulares para o painel avançado por plano específico
    std::string FindEcoScheme();
    std::string FindBalancedScheme();
    std::string FindHighPerformanceScheme();
    bool ForceEcoPlan(bool enable, std::string& outAppliedName, std::string& outStatusMsg);
    bool IsEcoPlanForced() const { return m_isEcoPlanForced; }

    bool ReadSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t& minAc, uint32_t& maxAc, uint32_t& minDc, uint32_t& maxDc);
    bool WriteSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t minAc, uint32_t maxAc, uint32_t minDc, uint32_t maxDc);

    bool ReadCoreParking(const std::string& schemeGuid, uint32_t& acVal, uint32_t& dcVal);
    bool SetCoreParkingForScheme(const std::string& schemeGuid, uint32_t minCoresPercent);
    // Estacionamento por linha (tomada e bateria com valores diferentes): as outras escritas gravam
    // o mesmo numero nas duas e achatavam a linha que a usuaria escolheu.
    bool SetCoreParkingLines(const std::string& schemeGuid, uint32_t acPercent, uint32_t dcPercent);
    bool ForceMaxCoreParking(bool enable, uint32_t& outAppliedVal);
    bool IsMaxCoreParkingForced() const { return m_isMaxCoreParkingForced; }

    // Gerenciamento e Proteção contra desligamento de Discos/HDs
    uint32_t GetDiskIdleTimeout(bool dc = false);
    bool SetDiskIdleTimeout(uint32_t seconds, bool dc = false);
    bool PreventDiskPowerDown(bool prevent);
    bool IsDiskPowerDownPrevented() const { return m_isDiskPowerDownPrevented; }

    // Suspensão e pausa de dispositivos ociosos em modo Economico (USB Suspend e PCIe ASPM)
    bool ConfigureIdleDeviceSuspension(const std::string& schemeGuid, bool pauseIdleDevices);
    bool IsIdleDeviceSuspensionEnabled() const { return m_pauseIdleDevicesEco; }
    void SetIdleDeviceSuspensionEnabled(bool enable) { m_pauseIdleDevicesEco = enable; }

    // Bateria / Alimentação
    BatteryStatusInfo GetBatteryStatus();

    // Restauração das configurações originais
    void RestoreOriginalSettings();

private:
    std::string m_originalSchemeGuid;
    uint32_t m_originalThrottleLimit = 100;
    bool m_initialized = false;

    bool m_isEcoPlanForced = false;
    std::string m_preForcedEcoSchemeGuid;

    bool m_isMaxCoreParkingForced = false;
    uint32_t m_preForcedCoreParkingAc = 50;

    bool m_isDiskPowerDownPrevented = false;
    uint32_t m_prePreventDiskTimeoutAc = 0;
    uint32_t m_prePreventDiskTimeoutDc = 0;

    bool m_pauseIdleDevicesEco = false;

#ifndef _WIN32
    // Estado exato capturado antes da primeira escrita, para o restaurar nao inventar
    // valores: em Linux, depois de escrever nao ha como consultar o valor anterior.
    // Uma politica cpufreq por classe de nucleo (hibridos Intel tem policy0 e policy8 com
    // governor, EPP e tetos proprios), por isso o estado e por politica e nao global.
    struct LinuxPolicyState {
        std::string path;
        std::string governor;
        std::string epp;
        bool hasEpp = false;
        unsigned long long minKHz = 0;
        unsigned long long maxKHz = 0;
    };
    std::vector<int> m_linuxParkedCpus;   // cpuN colocados offline por esta instancia
    std::vector<LinuxPolicyState> m_linuxOriginalPolicies;
    bool m_linuxCapturedState = false;
    std::vector<std::pair<std::string, std::string>> m_linuxBlockControlOriginals;  // runtime PM dos discos
    std::vector<std::pair<std::string, std::string>> m_linuxUsbControlOriginals;    // suspend selectivo USB
    std::string m_linuxAspmOriginalPolicy;
    bool m_linuxAspmCaptured = false;
#endif
};

} // namespace opitcore
