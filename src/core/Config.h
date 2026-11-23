// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace opitcore {

class Config {
public:
    static Config& Instance();
    bool Load(const std::string& path = "opitcore.json");
    bool Save(const std::string& path = "opitcore.json");

    enum class Profile { Eco, Balanced, HighPerformance };
    static std::string ProfileToString(Profile p);
    static Profile StringToProfile(const std::string& str);

    // Getters e Setters
    Profile GetProfile() const { return m_profile; }
    void SetProfile(Profile p) { m_profile = p; }

    bool IsAutoPowerSwitchEnabled() const { return m_autoPowerSwitch; }
    void SetAutoPowerSwitch(bool enabled) { m_autoPowerSwitch = enabled; }

    int GetSampleIntervalMs() const { return m_sampleIntervalMs; }
    void SetSampleIntervalMs(int ms) { m_sampleIntervalMs = ms; }

    double GetThermalLimitC() const { return m_thermalLimitC; }
    void SetThermalLimitC(double limit) { m_thermalLimitC = limit; }

    bool IsThermalProtectionEnabled() const { return m_thermalProtectionEnabled; }
    void SetThermalProtectionEnabled(bool enabled) { m_thermalProtectionEnabled = enabled; }

    bool IsAutoGameModeEnabled() const { return m_autoGameModeEnabled; }
    void SetAutoGameModeEnabled(bool enabled) { m_autoGameModeEnabled = enabled; }

    bool GetStartWithWindows() const { return m_startWithWindows; }
    void SetStartWithWindows(bool enabled) { m_startWithWindows = enabled; }

    bool GetStartMinimized() const { return m_startMinimized; }
    void SetStartMinimized(bool enabled) { m_startMinimized = enabled; }

    bool GetCloseToTray() const { return m_closeToTray; }
    void SetCloseToTray(bool enabled) { m_closeToTray = enabled; }

    int GetProcessorThrottleCap() const { return m_processorThrottleCap; }
    void SetProcessorThrottleCap(int cap) { m_processorThrottleCap = cap; }

    std::string GetLastPowerScheme() const { return m_lastPowerScheme; }
    void SetLastPowerScheme(const std::string& guid) { m_lastPowerScheme = guid; }

    std::vector<std::string> GetCustomGames() const { return m_customGames; }
    void SetCustomGames(const std::vector<std::string>& games) { m_customGames = games; }
    void AddCustomGame(const std::string& game);
    void RemoveCustomGame(const std::string& game);

    std::string GetConfigPath() const { return m_configPath; }

private:
    Config() = default;

    Profile m_profile = Profile::Balanced;
    bool m_autoPowerSwitch = false;
    int m_sampleIntervalMs = 1000;
    double m_thermalLimitC = 80.0;
    bool m_thermalProtectionEnabled = true;
    bool m_autoGameModeEnabled = true;
    bool m_startWithWindows = false;
    bool m_startMinimized = false;
    bool m_closeToTray = true;
    int m_processorThrottleCap = 100;
    std::string m_lastPowerScheme;
    std::vector<std::string> m_customGames;
    std::string m_configPath;
};

} // namespace opitcore
