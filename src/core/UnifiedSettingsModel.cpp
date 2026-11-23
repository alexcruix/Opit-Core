// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "UnifiedSettingsModel.h"
#include "Config.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include <sstream>

namespace opitcore {

UnifiedSettingsModel& UnifiedSettingsModel::Instance() {
    static UnifiedSettingsModel instance;
    return instance;
}

void UnifiedSettingsModel::Load() {
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return;

    auto getBool = [&](const std::string& key, bool defaultVal) -> bool {
        std::string v = db.GetSetting(key, defaultVal ? "1" : "0");
        return (v == "1" || v == "true");
    };

    auto getStr = [&](const std::string& key, const std::string& defaultVal) -> std::string {
        return db.GetSetting(key, defaultVal);
    };

    auto getInt = [&](const std::string& key, int defaultVal) -> int {
        std::string v = db.GetSetting(key, std::to_string(defaultVal));
        try { return std::stoi(v); } catch (...) { return defaultVal; }
    };

    // 1. Ativação Global
    enableOrchestrator = getBool("unified_enable_orchestrator", true);
    smartOrchestrator = getBool("unified_smart_orchestrator", true);

    // 2. Módulos
    modPowerPlans = getBool("unified_mod_power_plans", true);
    modCoreParking = getBool("unified_mod_core_parking", true);
    modGpuControl = getBool("unified_mod_gpu_control", true);
    modThermalGuardian = getBool("unified_mod_thermal_guardian", true);
    modAdaptiveHardware = getBool("unified_mod_adaptive_hw", true);
    modWindowsServices = getBool("unified_mod_win_services", true);
    subSmartServices = getBool("unified_sub_smart_services", true);
    modSystemStability = getBool("unified_mod_stability", true);
    modPauseIndexingIdle = getBool("unified_mod_pause_indexing_idle", false);
    disableMicrosoftTelemetry = getBool("unified_disable_ms_telemetry", false);
    smartGamingServiceOptimization = getBool("unified_smart_gaming_svc", true);

    modCpuMinMax = getBool("unified_mod_cpu_min_max", true);
    modProcessControl = getBool("unified_mod_proc_control", true);
    modFanCurve = getBool("unified_mod_fan_curve", true);
    modDeepEco = getBool("unified_mod_deep_eco", true);
    pauseIdleDevicesEco = getBool("unified_pause_idle_devices_eco", false);
    modAppPriority = getBool("unified_mod_app_priority", true);

    // 3. Níveis por Módulo
    levelPowerPlans.level = getStr("unified_lvl_power_plans", "med");
    levelPowerPlans.autoSmartMode = getBool("unified_auto_power_plans", false);

    levelCpuMinMax.level = getStr("unified_lvl_cpu_min_max", "med");
    levelCpuMinMax.autoSmartMode = getBool("unified_auto_cpu_min_max", false);

    levelCoreParking.level = getStr("unified_lvl_core_parking", "med");
    levelCoreParking.autoSmartMode = getBool("unified_auto_core_parking", false);

    levelProcesses.level = getStr("unified_lvl_processes", "med");
    levelProcesses.autoSmartMode = getBool("unified_auto_processes", false);

    levelGpu.level = getStr("unified_lvl_gpu", "med");
    levelGpu.autoSmartMode = getBool("unified_auto_gpu", false);

    levelFans.level = getStr("unified_lvl_fans", "med");
    levelFans.autoSmartMode = getBool("unified_auto_fans", false);

    levelThermalGuardian.level = getStr("unified_lvl_thermal_guardian", "med");
    levelThermalGuardian.autoSmartMode = getBool("unified_auto_thermal_guardian", false);

    levelStability.level = getStr("unified_lvl_stability", "med");
    levelStability.autoSmartMode = getBool("unified_auto_stability", false);

    levelServices.level = getStr("unified_lvl_services", "med");
    levelServices.autoSmartMode = getBool("unified_auto_services", true);

    // 5. Agrupamento svchost
    svchostGrouping = getInt("unified_svchost_grouping", 0);

    OPIT_LOG_INFO("UnifiedSettingsModel: Configuracoes carregadas com sucesso");
}

bool UnifiedSettingsModel::Save() {
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return false;

    auto setBool = [&](const std::string& key, bool val) {
        db.SetSetting(key, val ? "1" : "0");
    };

    auto setStr = [&](const std::string& key, const std::string& val) {
        db.SetSetting(key, val);
    };

    auto setInt = [&](const std::string& key, int val) {
        db.SetSetting(key, std::to_string(val));
    };

    // 1. Ativação Global
    setBool("unified_enable_orchestrator", enableOrchestrator);
    setBool("unified_smart_orchestrator", smartOrchestrator);

    // 2. Módulos
    setBool("unified_mod_power_plans", modPowerPlans);
    setBool("unified_mod_core_parking", modCoreParking);
    setBool("unified_mod_gpu_control", modGpuControl);
    setBool("unified_mod_thermal_guardian", modThermalGuardian);
    setBool("unified_mod_adaptive_hw", modAdaptiveHardware);
    setBool("unified_mod_win_services", modWindowsServices);
    setBool("unified_sub_smart_services", subSmartServices);
    setBool("unified_mod_stability", modSystemStability);
    setBool("unified_mod_pause_indexing_idle", modPauseIndexingIdle);
    setBool("unified_disable_ms_telemetry", disableMicrosoftTelemetry);
    setBool("unified_smart_gaming_svc", smartGamingServiceOptimization);

    setBool("unified_mod_cpu_min_max", modCpuMinMax);
    setBool("unified_mod_proc_control", modProcessControl);
    setBool("unified_mod_fan_curve", modFanCurve);
    setBool("unified_mod_deep_eco", modDeepEco);
    setBool("unified_pause_idle_devices_eco", pauseIdleDevicesEco);
    setBool("unified_mod_app_priority", modAppPriority);

    // 3. Níveis por Módulo
    setStr("unified_lvl_power_plans", levelPowerPlans.level);
    setBool("unified_auto_power_plans", levelPowerPlans.autoSmartMode);

    setStr("unified_lvl_cpu_min_max", levelCpuMinMax.level);
    setBool("unified_auto_cpu_min_max", levelCpuMinMax.autoSmartMode);

    setStr("unified_lvl_core_parking", levelCoreParking.level);
    setBool("unified_auto_core_parking", levelCoreParking.autoSmartMode);

    setStr("unified_lvl_processes", levelProcesses.level);
    setBool("unified_auto_processes", levelProcesses.autoSmartMode);

    setStr("unified_lvl_gpu", levelGpu.level);
    setBool("unified_auto_gpu", levelGpu.autoSmartMode);

    setStr("unified_lvl_fans", levelFans.level);
    setBool("unified_auto_fans", levelFans.autoSmartMode);

    setStr("unified_lvl_thermal_guardian", levelThermalGuardian.level);
    setBool("unified_auto_thermal_guardian", levelThermalGuardian.autoSmartMode);

    setStr("unified_lvl_stability", levelStability.level);
    setBool("unified_auto_stability", levelStability.autoSmartMode);

    setStr("unified_lvl_services", levelServices.level);
    setBool("unified_auto_services", levelServices.autoSmartMode);

    // 5. Agrupamento svchost
    setInt("unified_svchost_grouping", svchostGrouping);

    // Sincronizar também com arquivo de configuração global
    Config::Instance().Save(Config::Instance().GetConfigPath());
    OPIT_LOG_INFO("UnifiedSettingsModel: Configuracoes persistidas no SQLite e JSON");
    return true;
}

} // namespace opitcore
