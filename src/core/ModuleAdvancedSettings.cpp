#include "ModuleAdvancedSettings.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include "power/PowerManager.h"
#include "utils/StringUtils.h"
#include <algorithm>
#include <sstream>

namespace opitcore {

void AdvPowerPlansConfig::Validate() {
    confirmationDelayMs = std::clamp(confirmationDelayMs, 500, 10000);
}

void PlanThrottleSettings::Validate() {
    minPercentAC = std::clamp(minPercentAC, 0u, 100u);
    maxPercentAC = std::clamp(maxPercentAC, 0u, 100u);
    minPercentDC = std::clamp(minPercentDC, 0u, 100u);
    maxPercentDC = std::clamp(maxPercentDC, 0u, 100u);
    if (minPercentAC > maxPercentAC) minPercentAC = maxPercentAC;
    if (minPercentDC > maxPercentDC) minPercentDC = maxPercentDC;
}

PlanThrottleSettings AdvCpuMinMaxConfig::GetPlanThrottle(const std::string& schemeGuidOrName) const {
    std::string s = StringUtils::ToLower(schemeGuidOrName);
    if (s.find("a1841308-3541-4fab-bc81-f71556f20b4a") != std::string::npos ||
        s.find("eco") != std::string::npos ||
        s.find("economia") != std::string::npos ||
        s.find("poupança") != std::string::npos ||
        s.find("saver") != std::string::npos) {
        return ecoPlan;
    }
    if (s.find("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") != std::string::npos ||
        s.find("e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos ||
        s.find("alto") != std::string::npos ||
        s.find("high") != std::string::npos ||
        s.find("desempenho") != std::string::npos ||
        s.find("ultimate") != std::string::npos) {
        return highPerfPlan;
    }
    return balancedPlan;
}

void AdvCpuMinMaxConfig::SetPlanThrottle(const std::string& schemeGuidOrName, const PlanThrottleSettings& throttle) {
    std::string s = StringUtils::ToLower(schemeGuidOrName);
    if (s.find("a1841308-3541-4fab-bc81-f71556f20b4a") != std::string::npos ||
        s.find("eco") != std::string::npos ||
        s.find("economia") != std::string::npos ||
        s.find("poupança") != std::string::npos ||
        s.find("saver") != std::string::npos) {
        ecoPlan = throttle;
        ecoPlan.Validate();
    } else if (s.find("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") != std::string::npos ||
               s.find("e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos ||
               s.find("alto") != std::string::npos ||
               s.find("high") != std::string::npos ||
               s.find("desempenho") != std::string::npos ||
               s.find("ultimate") != std::string::npos) {
        highPerfPlan = throttle;
        highPerfPlan.Validate();
    } else {
        balancedPlan = throttle;
        balancedPlan.Validate();
    }
}

void AdvCpuMinMaxConfig::ApplyStandardProfile(bool hasBattery) {
    // Duas tabelas e nao uma so com redutor: o notebook tem linha AC e linha DC proprias, e a
    // bateria da usuaria pede teto mais baixo que a tomada (0-20/21-35/36-60 vs 0-25/26-50/51-75).
    if (hasBattery) {
        ecoPlan = PlanThrottleSettings{0, 25, 0, 20};
        balancedPlan = PlanThrottleSettings{26, 50, 21, 35};
        highPerfPlan = PlanThrottleSettings{51, 75, 36, 60};
    } else {
        // Desktop e servidor nao tem linha de bateria que diverga: a mesma tabela vale para as duas.
        ecoPlan = PlanThrottleSettings{0, 35, 0, 35};
        balancedPlan = PlanThrottleSettings{36, 78, 36, 78};
        highPerfPlan = PlanThrottleSettings{79, 100, 79, 100};
    }
    ecoPlan.Validate();
    balancedPlan.Validate();
    highPerfPlan.Validate();
}

bool AdvCpuMinMaxConfig::Validate(std::string* outError) {
    ecoPlan.Validate();
    balancedPlan.Validate();
    highPerfPlan.Validate();

    if (minPercentAC > 100 || maxPercentAC > 100 || minPercentDC > 100 || maxPercentDC > 100) {
        if (outError) *outError = "Valores de CPU devem estar na escala normalizada de 0 a 100%.";
        return false;
    }
    if (minPercentAC > maxPercentAC) {
        if (outError) *outError = "Conflito: CPU Minimo AC (" + std::to_string(minPercentAC) +
                                  "%) nao pode ser maior que o Maximo AC (" + std::to_string(maxPercentAC) + "%).";
        minPercentAC = maxPercentAC;
        return false;
    }
    if (minPercentDC > maxPercentDC) {
        if (outError) *outError = "Conflito: CPU Minimo DC (" + std::to_string(minPercentDC) +
                                  "%) nao pode ser maior que o Maximo DC (" + std::to_string(maxPercentDC) + "%).";
        minPercentDC = maxPercentDC;
        return false;
    }
    reevaluationIntervalMs = std::clamp(reevaluationIntervalMs, 250, 5000);
    return true;
}

void AdvCoreParkingConfig::Validate() {
    minCoresActiveAC = std::clamp(minCoresActiveAC, 5u, 100u);
    minCoresActiveDC = std::clamp(minCoresActiveDC, 5u, 100u);
    hysteresisMs = std::clamp(hysteresisMs, 1000, 10000);
}

void AdvProcessesConfig::Validate() {
    if (excludedProcesses.empty()) {
        excludedProcesses = "explorer.exe;dwm.exe;lsass.exe;csrss.exe;services.exe;taskmgr.exe;Opit-Core.exe;svchost.exe;sihost.exe;ctfmon.exe;RuntimeBroker.exe;WmiPrvSE.exe;audiodg.exe;SearchIndexer.exe;NVDisplay.Container.exe";
    }
    // Nunca rebaixar processos sem janela visivel ou em segundo plano para IDLE_PRIORITY_CLASS (64),
    // pois servicos auxiliares de entrada (ctfmon/HID) e drivers sofrem starvation sob carga.
    if (prioNoVisibleWindow == 64) prioNoVisibleWindow = 32; // NORMAL_PRIORITY_CLASS
    if (prioBackground == 64) prioBackground = 16384;        // BELOW_NORMAL_PRIORITY_CLASS
    if (prioMinimized == 64) prioMinimized = 16384;
    if (prioSystemTray == 64) prioSystemTray = 16384;
}

bool AdvProcessesConfig::IsProcessExcluded(const std::string& procName) const {
    std::string lowerProc = StringUtils::ToLower(procName);
    std::string lowerList = StringUtils::ToLower(excludedProcesses);
    std::stringstream ss(lowerList);
    std::string item;
    while (std::getline(ss, item, ';')) {
        std::string trimmed = StringUtils::Trim(item);
        if (!trimmed.empty() && lowerProc == trimmed) {
            return true;
        }
    }
    return false;
}

void AdvGpuConfig::Validate() {
    graphicsPreference = std::clamp(graphicsPreference, 0, 2);
    powerProfile = std::clamp(powerProfile, 0, 3);
    coreClockOffsetMHz = std::clamp(coreClockOffsetMHz, -200, 250);
    memoryClockOffsetMHz = std::clamp(memoryClockOffsetMHz, -200, 400);
    powerLimitPercent = std::clamp(powerLimitPercent, 60, 120);
}

void AdvFansConfig::Validate() {
    controlMode = std::clamp(controlMode, 0, 2);
    thermalHysteresisC = std::clamp(thermalHysteresisC, 2, 10);
    enforceFirmwareSafeguards = true; // Inviolável por segurança
}

void AdvThermalConfig::Validate() {
    alertLimitC = std::clamp(alertLimitC, 60.0, 95.0);
    criticalLimitC = std::clamp(criticalLimitC, 70.0, 100.0);
    if (alertLimitC > criticalLimitC) alertLimitC = criticalLimitC - 5.0;

    hysteresisRestoreC = std::clamp(hysteresisRestoreC, 3.0, 15.0);
    throttleActionCap = std::clamp(throttleActionCap, 50u, 85u);
    confirmationTimeMs = std::clamp(confirmationTimeMs, 1000, 5000);
}

void AdvStabilityConfig::Validate() {
    observationWindowSamples = std::clamp(observationWindowSamples, 3, 20);
    minImprovementPercent = std::clamp(minImprovementPercent, 1.0, 10.0);
    noiseTolerancePercent = std::clamp(noiseTolerancePercent, 1.0, 8.0);
}

void AdvServicesConfig::Validate() {
    highCpuThreshold = std::clamp(highCpuThreshold, 50u, 95u);
    aiAutonomyLevel = std::clamp(aiAutonomyLevel, 0, 2);
    idleSecondsBeforePause = std::clamp(idleSecondsBeforePause, 60u, 1800u);
    minGainMB = std::clamp(minGainMB, 0u, 512u);
    maintenanceHour = std::clamp(maintenanceHour, 0, 23);
    maintenanceMinutes = std::clamp(maintenanceMinutes, 5, 180);
    if (customExcludedServices.empty()) {
        customExcludedServices = "RpcSs;DcomLaunch;EventLog;PlugPlay;LanmanWorkstation";
    }
}

bool AdvServicesConfig::IsServiceAllowed(const std::string& svcName) const {
    std::string lowerSvc = StringUtils::ToLower(StringUtils::Trim(svcName));
    if (lowerSvc == "wsearch" && !manageWSearch) return false;
    if (lowerSvc == "spooler" && !manageSpooler) return false;
    if (lowerSvc == "diagtrack" && !manageDiagTrack) return false;
    if (lowerSvc == "sysmain" && !manageSysMain) return false;

    static const std::vector<std::string> thirdPartyPrefixes = {
        "gupdate", "edgeupdate", "adobearm", "adobeservice", "mozillamaintenance",
        "braveelevation", "opera autoupdate", "dellclient", "hpapphelper", "hpnetwork",
        "hpsysinfo", "lenovovantage", "asuslink", "asussystem", "dropboxupdate"
    };
    if (!manageThirdPartyUpdaters) {
        for (const auto& p : thirdPartyPrefixes) {
            if (lowerSvc.find(p) != std::string::npos) return false;
        }
    }

    // Checar lista de exclusão inviolável do sistema
    std::string lowerList = StringUtils::ToLower(customExcludedServices);
    std::stringstream ss(lowerList);
    std::string item;
    while (std::getline(ss, item, ';')) {
        std::string trimmed = StringUtils::Trim(item);
        if (!trimmed.empty() && lowerSvc == trimmed) {
            return false;
        }
    }

    // Checar lista de exceções personalizadas pelo usuário
    if (!customUserExcludedServices.empty()) {
        std::string lowerUserList = StringUtils::ToLower(customUserExcludedServices);
        std::stringstream ssUser(lowerUserList);
        std::string userItem;
        while (std::getline(ssUser, userItem, ';')) {
            std::string trimmed = StringUtils::Trim(userItem);
            if (!trimmed.empty() && lowerSvc == trimmed) {
                return false;
            }
        }
    }

    return true;
}

ModuleAdvancedSettings& ModuleAdvancedSettings::Instance() {
    static ModuleAdvancedSettings instance;
    return instance;
}

std::string ModuleAdvancedSettings::NormalizeHwClass(const std::string& value) {
    std::string s = StringUtils::ToLower(StringUtils::Trim(value));
    if (s == "desktop" || s == "notebook") return s;
    return "auto";
}

bool ModuleAdvancedSettings::EffectiveHasBattery() const {
    if (hwClassOverride == "desktop") return false;
    if (hwClassOverride == "notebook") return true;
    return SystemHasBattery();
}

std::string ModuleAdvancedSettings::EffectiveHwClassName() const {
    return EffectiveHasBattery() ? "notebook" : "desktop";
}

void ModuleAdvancedSettings::ValidateAll() {
    powerPlans.Validate();
    cpuMinMax.Validate();
    coreParking.Validate();
    processes.Validate();
    gpu.Validate();
    fans.Validate();
    thermal.Validate();
    stability.Validate();
    services.Validate();
}

void ModuleAdvancedSettings::ResetToDefaults(int moduleIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (moduleIndex == -1 || moduleIndex == 0) powerPlans = AdvPowerPlansConfig{};
    if (moduleIndex == -1 || moduleIndex == 1) cpuMinMax = AdvCpuMinMaxConfig{};
    // Voltar ao padrao volta para o padrao DESTE equipamento (override de classe vale aqui tambem),
    // nao para o do desktop.
    if (moduleIndex == -1 || moduleIndex == 1) cpuMinMax.ApplyStandardProfile(EffectiveHasBattery());
    if (moduleIndex == -1 || moduleIndex == 2) coreParking = AdvCoreParkingConfig{};
    if (moduleIndex == -1 || moduleIndex == 3) processes = AdvProcessesConfig{};
    if (moduleIndex == -1 || moduleIndex == 4) gpu = AdvGpuConfig{};
    if (moduleIndex == -1 || moduleIndex == 5) fans = AdvFansConfig{};
    if (moduleIndex == -1 || moduleIndex == 6) thermal = AdvThermalConfig{};
    if (moduleIndex == -1 || moduleIndex == 7) stability = AdvStabilityConfig{};
    if (moduleIndex == -1 || moduleIndex == 8) services = AdvServicesConfig{};
}

bool ModuleAdvancedSettings::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ValidateAll();

    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return false;

    // 1. Power Plans
    db.SetSetting("adv_pwr_scheme_ac", powerPlans.preferredSchemeAC);
    db.SetSetting("adv_pwr_scheme_dc", powerPlans.preferredSchemeDC);
    db.SetSetting("adv_pwr_auto_game", powerPlans.autoSwitchOnGame ? "1" : "0");
    db.SetSetting("adv_pwr_restore_game", powerPlans.restoreOnGameExit ? "1" : "0");
    db.SetSetting("adv_pwr_delay_ms", std::to_string(powerPlans.confirmationDelayMs));
    db.SetSetting("adv_pwr_fallback_bal", powerPlans.fallbackToBalanced ? "1" : "0");
    db.SetSetting("adv_pwr_force_eco", powerPlans.forceEcoPlan ? "1" : "0");
    db.SetSetting("adv_pwr_prev_eco_guid", powerPlans.forcedEcoPreviousSchemeGuid);
    db.SetSetting("adv_pwr_pause_idle_devices", powerPlans.pauseIdleDevicesEco ? "1" : "0");

    // 2. CPU Min/Max (Salvo separadamente para cada plano de energia)
    // Economia
    db.SetSetting("adv_cpu_eco_min_ac", std::to_string(cpuMinMax.ecoPlan.minPercentAC));
    db.SetSetting("adv_cpu_eco_max_ac", std::to_string(cpuMinMax.ecoPlan.maxPercentAC));
    db.SetSetting("adv_cpu_eco_min_dc", std::to_string(cpuMinMax.ecoPlan.minPercentDC));
    db.SetSetting("adv_cpu_eco_max_dc", std::to_string(cpuMinMax.ecoPlan.maxPercentDC));
    // Balanceado
    db.SetSetting("adv_cpu_bal_min_ac", std::to_string(cpuMinMax.balancedPlan.minPercentAC));
    db.SetSetting("adv_cpu_bal_max_ac", std::to_string(cpuMinMax.balancedPlan.maxPercentAC));
    db.SetSetting("adv_cpu_bal_min_dc", std::to_string(cpuMinMax.balancedPlan.minPercentDC));
    db.SetSetting("adv_cpu_bal_max_dc", std::to_string(cpuMinMax.balancedPlan.maxPercentDC));
    // Alta Performance
    db.SetSetting("adv_cpu_high_min_ac", std::to_string(cpuMinMax.highPerfPlan.minPercentAC));
    db.SetSetting("adv_cpu_high_max_ac", std::to_string(cpuMinMax.highPerfPlan.maxPercentAC));
    db.SetSetting("adv_cpu_high_min_dc", std::to_string(cpuMinMax.highPerfPlan.minPercentDC));
    db.SetSetting("adv_cpu_high_max_dc", std::to_string(cpuMinMax.highPerfPlan.maxPercentDC));
    // Ativo / Global
    db.SetSetting("adv_cpu_min_ac", std::to_string(cpuMinMax.minPercentAC));
    db.SetSetting("adv_cpu_max_ac", std::to_string(cpuMinMax.maxPercentAC));
    db.SetSetting("adv_cpu_min_dc", std::to_string(cpuMinMax.minPercentDC));
    db.SetSetting("adv_cpu_max_dc", std::to_string(cpuMinMax.maxPercentDC));
    db.SetSetting("adv_cpu_allow_turbo", cpuMinMax.allowTurboBoost ? "1" : "0");
    db.SetSetting("adv_cpu_interval_ms", std::to_string(cpuMinMax.reevaluationIntervalMs));
    db.SetSetting("adv_cpu_target_scheme", cpuMinMax.targetSchemeGuid);
    // Quem salva valores explicitos passa a pertencer ao perfil-padrao atual: sem esta linha a
    // proxima execucao entenderia a instalacao como desatualizada e poria o padrao por cima do ajuste.
    db.SetSetting("adv_cpu_profile", AdvCpuMinMaxConfig::kStandardProfileId);
    db.SetSetting("adv_cpu_profile_class", EffectiveHwClassName());
    db.SetSetting("adv_hw_class_override", hwClassOverride);

    // 3. Core Parking
    db.SetSetting("adv_cp_active_ac", std::to_string(coreParking.minCoresActiveAC));
    db.SetSetting("adv_cp_active_dc", std::to_string(coreParking.minCoresActiveDC));
    db.SetSetting("adv_cp_unpark_game", coreParking.forceUnparkOnGame ? "1" : "0");
    db.SetSetting("adv_cp_hysteresis_ms", std::to_string(coreParking.hysteresisMs));
    db.SetSetting("adv_cp_force_max", coreParking.forceMaxParking ? "1" : "0");
    db.SetSetting("adv_cp_prev_ac", std::to_string(coreParking.previousParkingAc));

    // 4. Processes
    db.SetSetting("adv_proc_prio_fg", std::to_string(processes.foregroundPriorityClass));
    db.SetSetting("adv_proc_prio_game", std::to_string(processes.gamePriorityClass));
    db.SetSetting("adv_proc_excluded", processes.excludedProcesses);
    db.SetSetting("adv_proc_trim_idle", processes.trimWorkingSetIdle ? "1" : "0");
    db.SetSetting("adv_proc_protect_crit", processes.protectCriticalProcesses ? "1" : "0");
    db.SetSetting("adv_proc_auto", processes.autoProcessManagement ? "1" : "0");
    db.SetSetting("adv_proc_prio_max", std::to_string(processes.prioMaximized));
    db.SetSetting("adv_proc_prio_norm", std::to_string(processes.prioNormal));
    db.SetSetting("adv_proc_prio_min", std::to_string(processes.prioMinimized));
    db.SetSetting("adv_proc_prio_bg", std::to_string(processes.prioBackground));
    db.SetSetting("adv_proc_prio_tray", std::to_string(processes.prioSystemTray));
    db.SetSetting("adv_proc_prio_nowin", std::to_string(processes.prioNoVisibleWindow));
    db.SetSetting("adv_proc_pres_media", processes.preserveAudioVideoTasks ? "1" : "0");

    // 5. GPU
    db.SetSetting("adv_gpu_pref", std::to_string(gpu.graphicsPreference));
    db.SetSetting("adv_gpu_low_latency", gpu.dxgiLowLatency ? "1" : "0");
    db.SetSetting("adv_gpu_adapter", gpu.preferredAdapterName);
    db.SetSetting("adv_gpu_profile", std::to_string(gpu.powerProfile));
    db.SetSetting("adv_gpu_core_offset", std::to_string(gpu.coreClockOffsetMHz));
    db.SetSetting("adv_gpu_mem_offset", std::to_string(gpu.memoryClockOffsetMHz));
    db.SetSetting("adv_gpu_power_limit", std::to_string(gpu.powerLimitPercent));

    // 6. Fans
    db.SetSetting("adv_fan_mode", std::to_string(fans.controlMode));
    db.SetSetting("adv_fan_hysteresis_c", std::to_string(fans.thermalHysteresisC));

    // 7. Thermal Guardian
    db.SetSetting("adv_therm_alert_c", std::to_string(thermal.alertLimitC));
    db.SetSetting("adv_therm_crit_c", std::to_string(thermal.criticalLimitC));
    db.SetSetting("adv_therm_hysteresis_c", std::to_string(thermal.hysteresisRestoreC));
    db.SetSetting("adv_therm_action_cap", std::to_string(thermal.throttleActionCap));
    db.SetSetting("adv_therm_confirm_ms", std::to_string(thermal.confirmationTimeMs));
    db.SetSetting("adv_therm_log", thermal.logThermalIncidents ? "1" : "0");

    // 8. Stability
    db.SetSetting("adv_stab_obs_samples", std::to_string(stability.observationWindowSamples));
    db.SetSetting("adv_stab_min_gain", std::to_string(stability.minImprovementPercent));
    db.SetSetting("adv_stab_revert_reg", stability.autoRevertOnRegression ? "1" : "0");
    db.SetSetting("adv_stab_dpc_latency", stability.monitorDpcLatency ? "1" : "0");
    db.SetSetting("adv_stab_noise_tol", std::to_string(stability.noiseTolerancePercent));

    // 9. Services
    db.SetSetting("adv_svc_pause_game", services.pauseOnGame ? "1" : "0");
    db.SetSetting("adv_svc_pause_eco", services.pauseOnEco ? "1" : "0");
    db.SetSetting("adv_svc_pause_highcpu", services.pauseOnHighCpu ? "1" : "0");
    db.SetSetting("adv_svc_highcpu_thresh", std::to_string(services.highCpuThreshold));
    db.SetSetting("adv_svc_bloat_perm", services.pauseBloatwarePermanent ? "1" : "0");
    db.SetSetting("adv_svc_ai_autonomy", std::to_string(services.aiAutonomyLevel));
    db.SetSetting("adv_svc_wsearch", services.manageWSearch ? "1" : "0");
    db.SetSetting("adv_svc_spooler", services.manageSpooler ? "1" : "0");
    db.SetSetting("adv_svc_diagtrack", services.manageDiagTrack ? "1" : "0");
    db.SetSetting("adv_svc_sysmain", services.manageSysMain ? "1" : "0");
    db.SetSetting("adv_svc_thirdparty", services.manageThirdPartyUpdaters ? "1" : "0");
    db.SetSetting("adv_svc_restore_exit", services.restoreOnExit ? "1" : "0");
    db.SetSetting("adv_svc_check_deps", services.checkDependencies ? "1" : "0");
    db.SetSetting("adv_svc_excluded", services.customExcludedServices);
    db.SetSetting("adv_svc_user_excluded", services.customUserExcludedServices);
    db.SetSetting("adv_svc_pause_idle", services.pauseWhenIdle ? "1" : "0");
    db.SetSetting("adv_svc_idle_secs", std::to_string(services.idleSecondsBeforePause));
    db.SetSetting("adv_svc_true_pause", services.allowTruePause ? "1" : "0");
    db.SetSetting("adv_svc_min_gain_mb", std::to_string(services.minGainMB));
    db.SetSetting("adv_svc_maint_hour", std::to_string(services.maintenanceHour));
    db.SetSetting("adv_svc_maint_mins", std::to_string(services.maintenanceMinutes));

    OPIT_LOG_INFO("ModuleAdvancedSettings: Configuracoes avancadas salvas no SQLite com sucesso.");
    return true;
}

bool ModuleAdvancedSettings::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return false;

    auto getInt = [&](const std::string& key, int defaultVal) -> int {
        std::string s = db.GetSetting(key, "");
        if (s.empty()) return defaultVal;
        try { return std::stoi(s); } catch (...) { return defaultVal; }
    };

    auto getUint = [&](const std::string& key, uint32_t defaultVal) -> uint32_t {
        std::string s = db.GetSetting(key, "");
        if (s.empty()) return defaultVal;
        try { return (uint32_t)std::stoul(s); } catch (...) { return defaultVal; }
    };

    auto getDouble = [&](const std::string& key, double defaultVal) -> double {
        std::string s = db.GetSetting(key, "");
        if (s.empty()) return defaultVal;
        try { return std::stod(s); } catch (...) { return defaultVal; }
    };

    auto getBool = [&](const std::string& key, bool defaultVal) -> bool {
        std::string s = db.GetSetting(key, "");
        if (s.empty()) return defaultVal;
        return (s == "1" || s == "true");
    };

    // 1. Power Plans
    powerPlans.preferredSchemeAC = db.GetSetting("adv_pwr_scheme_ac", powerPlans.preferredSchemeAC);
    powerPlans.preferredSchemeDC = db.GetSetting("adv_pwr_scheme_dc", powerPlans.preferredSchemeDC);
    powerPlans.autoSwitchOnGame = getBool("adv_pwr_auto_game", powerPlans.autoSwitchOnGame);
    powerPlans.restoreOnGameExit = getBool("adv_pwr_restore_game", powerPlans.restoreOnGameExit);
    powerPlans.confirmationDelayMs = getInt("adv_pwr_delay_ms", powerPlans.confirmationDelayMs);
    powerPlans.fallbackToBalanced = getBool("adv_pwr_fallback_bal", powerPlans.fallbackToBalanced);
    powerPlans.forceEcoPlan = getBool("adv_pwr_force_eco", powerPlans.forceEcoPlan);
    powerPlans.forcedEcoPreviousSchemeGuid = db.GetSetting("adv_pwr_prev_eco_guid", powerPlans.forcedEcoPreviousSchemeGuid);
    powerPlans.pauseIdleDevicesEco = getBool("adv_pwr_pause_idle_devices", powerPlans.pauseIdleDevicesEco);

    // 2. CPU Min/Max (Carregado separadamente para cada plano de energia)
    // Primeira execuacao, ou primeira execucao depois de a usuaria declarar um novo perfil-padrao:
    // semeia as tabelas pela classe de hardware detectada e grava no banco. Fora disso o valor salvo
    // e sagrado — foi a pessoa que ajustou na janela de CPU Min/Max, e "podem alterar depois"
    // faz parte da regra.
    hwClassOverride = NormalizeHwClass(db.GetSetting("adv_hw_class_override", hwClassOverride));
    const std::string seededClass = db.GetSetting("adv_cpu_profile_class", "");
    const bool reseeding = db.GetSetting("adv_cpu_profile", "") != AdvCpuMinMaxConfig::kStandardProfileId ||
                           seededClass != EffectiveHwClassName();
    if (reseeding) {
        const bool hasBattery = EffectiveHasBattery();
        cpuMinMax.ApplyStandardProfile(hasBattery);
        // Gravar as doze linhas junto com o marcador: sem isto o perfil existia so em memoria, o
        // marcador convencia a proxima execucao de que ja estava semeado e os valores antigos
        // voltavam por cima do padrao declarado.
        const auto persistThrottle = [&db](const char* prefix, const PlanThrottleSettings& plan) {
            db.SetSetting((std::string(prefix) + "_min_ac").c_str(), std::to_string(plan.minPercentAC));
            db.SetSetting((std::string(prefix) + "_max_ac").c_str(), std::to_string(plan.maxPercentAC));
            db.SetSetting((std::string(prefix) + "_min_dc").c_str(), std::to_string(plan.minPercentDC));
            db.SetSetting((std::string(prefix) + "_max_dc").c_str(), std::to_string(plan.maxPercentDC));
        };
        persistThrottle("adv_cpu_eco", cpuMinMax.ecoPlan);
        persistThrottle("adv_cpu_bal", cpuMinMax.balancedPlan);
        persistThrottle("adv_cpu_high", cpuMinMax.highPerfPlan);
        db.SetSetting("adv_cpu_profile", AdvCpuMinMaxConfig::kStandardProfileId);
        db.SetSetting("adv_cpu_profile_class", EffectiveHwClassName());
        const std::string overrideNote = (hwClassOverride == "auto")
            ? "deteccao automatica"
            : "override manual '" + hwClassOverride + "'";
        OPIT_LOG_INFO(std::string("ModuleAdvancedSettings: perfil-padrao de bandas semeado para ") +
                      (hasBattery ? "NOTEBOOK (tomada 0-25 / 26-50 / 51-75, bateria 0-20 / 21-35 / 36-60)"
                                  : "DESKTOP OU SERVIDOR (0-35 / 36-78 / 79-100)") +
                      " [" + overrideNote + "]");
    } else {
        // Economia
        cpuMinMax.ecoPlan.minPercentAC = getUint("adv_cpu_eco_min_ac", cpuMinMax.ecoPlan.minPercentAC);
        cpuMinMax.ecoPlan.maxPercentAC = getUint("adv_cpu_eco_max_ac", cpuMinMax.ecoPlan.maxPercentAC);
        cpuMinMax.ecoPlan.minPercentDC = getUint("adv_cpu_eco_min_dc", cpuMinMax.ecoPlan.minPercentDC);
        cpuMinMax.ecoPlan.maxPercentDC = getUint("adv_cpu_eco_max_dc", cpuMinMax.ecoPlan.maxPercentDC);
        // Balanceado
        cpuMinMax.balancedPlan.minPercentAC = getUint("adv_cpu_bal_min_ac", cpuMinMax.balancedPlan.minPercentAC);
        cpuMinMax.balancedPlan.maxPercentAC = getUint("adv_cpu_bal_max_ac", cpuMinMax.balancedPlan.maxPercentAC);
        cpuMinMax.balancedPlan.minPercentDC = getUint("adv_cpu_bal_min_dc", cpuMinMax.balancedPlan.minPercentDC);
        cpuMinMax.balancedPlan.maxPercentDC = getUint("adv_cpu_bal_max_dc", cpuMinMax.balancedPlan.maxPercentDC);
        // Alta Performance
        cpuMinMax.highPerfPlan.minPercentAC = getUint("adv_cpu_high_min_ac", cpuMinMax.highPerfPlan.minPercentAC);
        cpuMinMax.highPerfPlan.maxPercentAC = getUint("adv_cpu_high_max_ac", cpuMinMax.highPerfPlan.maxPercentAC);
        cpuMinMax.highPerfPlan.minPercentDC = getUint("adv_cpu_high_min_dc", cpuMinMax.highPerfPlan.minPercentDC);
        cpuMinMax.highPerfPlan.maxPercentDC = getUint("adv_cpu_high_max_dc", cpuMinMax.highPerfPlan.maxPercentDC);
    }
    // Ativo / Global
    cpuMinMax.minPercentAC = getUint("adv_cpu_min_ac", cpuMinMax.minPercentAC);
    cpuMinMax.maxPercentAC = getUint("adv_cpu_max_ac", cpuMinMax.maxPercentAC);
    cpuMinMax.minPercentDC = getUint("adv_cpu_min_dc", cpuMinMax.minPercentDC);
    cpuMinMax.maxPercentDC = getUint("adv_cpu_max_dc", cpuMinMax.maxPercentDC);
    cpuMinMax.allowTurboBoost = getBool("adv_cpu_allow_turbo", cpuMinMax.allowTurboBoost);
    cpuMinMax.reevaluationIntervalMs = getInt("adv_cpu_interval_ms", cpuMinMax.reevaluationIntervalMs);
    cpuMinMax.targetSchemeGuid = db.GetSetting("adv_cpu_target_scheme", cpuMinMax.targetSchemeGuid);

    // 3. Core Parking
    coreParking.minCoresActiveAC = getUint("adv_cp_active_ac", coreParking.minCoresActiveAC);
    coreParking.minCoresActiveDC = getUint("adv_cp_active_dc", coreParking.minCoresActiveDC);
    coreParking.forceUnparkOnGame = getBool("adv_cp_unpark_game", coreParking.forceUnparkOnGame);
    coreParking.hysteresisMs = getInt("adv_cp_hysteresis_ms", coreParking.hysteresisMs);
    coreParking.forceMaxParking = getBool("adv_cp_force_max", coreParking.forceMaxParking);
    coreParking.previousParkingAc = getUint("adv_cp_prev_ac", coreParking.previousParkingAc);

    // 4. Processes
    processes.foregroundPriorityClass = getUint("adv_proc_prio_fg", processes.foregroundPriorityClass);
    processes.gamePriorityClass = getUint("adv_proc_prio_game", processes.gamePriorityClass);
    processes.excludedProcesses = db.GetSetting("adv_proc_excluded", processes.excludedProcesses);
    processes.trimWorkingSetIdle = getBool("adv_proc_trim_idle", processes.trimWorkingSetIdle);
    processes.protectCriticalProcesses = getBool("adv_proc_protect_crit", processes.protectCriticalProcesses);
    processes.autoProcessManagement = getBool("adv_proc_auto", processes.autoProcessManagement);
    processes.prioMaximized = getUint("adv_proc_prio_max", processes.prioMaximized);
    processes.prioNormal = getUint("adv_proc_prio_norm", processes.prioNormal);
    processes.prioMinimized = getUint("adv_proc_prio_min", processes.prioMinimized);
    processes.prioBackground = getUint("adv_proc_prio_bg", processes.prioBackground);
    processes.prioSystemTray = getUint("adv_proc_prio_tray", processes.prioSystemTray);
    processes.prioNoVisibleWindow = getUint("adv_proc_prio_nowin", processes.prioNoVisibleWindow);
    processes.preserveAudioVideoTasks = getBool("adv_proc_pres_media", processes.preserveAudioVideoTasks);

    // 5. GPU
    gpu.graphicsPreference = getInt("adv_gpu_pref", gpu.graphicsPreference);
    gpu.dxgiLowLatency = getBool("adv_gpu_low_latency", gpu.dxgiLowLatency);
    gpu.preferredAdapterName = db.GetSetting("adv_gpu_adapter", gpu.preferredAdapterName);
    gpu.powerProfile = getInt("adv_gpu_profile", gpu.powerProfile);
    gpu.coreClockOffsetMHz = getInt("adv_gpu_core_offset", gpu.coreClockOffsetMHz);
    gpu.memoryClockOffsetMHz = getInt("adv_gpu_mem_offset", gpu.memoryClockOffsetMHz);
    gpu.powerLimitPercent = getInt("adv_gpu_power_limit", gpu.powerLimitPercent);

    // 6. Fans
    fans.controlMode = getInt("adv_fan_mode", fans.controlMode);
    fans.thermalHysteresisC = getInt("adv_fan_hysteresis_c", fans.thermalHysteresisC);

    // 7. Thermal Guardian
    thermal.alertLimitC = getDouble("adv_therm_alert_c", thermal.alertLimitC);
    thermal.criticalLimitC = getDouble("adv_therm_crit_c", thermal.criticalLimitC);
    thermal.hysteresisRestoreC = getDouble("adv_therm_hysteresis_c", thermal.hysteresisRestoreC);
    thermal.throttleActionCap = getUint("adv_therm_action_cap", thermal.throttleActionCap);
    thermal.confirmationTimeMs = getInt("adv_therm_confirm_ms", thermal.confirmationTimeMs);
    thermal.logThermalIncidents = getBool("adv_therm_log", thermal.logThermalIncidents);

    // 8. Stability
    stability.observationWindowSamples = getInt("adv_stab_obs_samples", stability.observationWindowSamples);
    stability.minImprovementPercent = getDouble("adv_stab_min_gain", stability.minImprovementPercent);
    stability.autoRevertOnRegression = getBool("adv_stab_revert_reg", stability.autoRevertOnRegression);
    stability.monitorDpcLatency = getBool("adv_stab_dpc_latency", stability.monitorDpcLatency);
    stability.noiseTolerancePercent = getDouble("adv_stab_noise_tol", stability.noiseTolerancePercent);

    // 9. Services
    services.pauseOnGame = getBool("adv_svc_pause_game", services.pauseOnGame);
    services.pauseOnEco = getBool("adv_svc_pause_eco", services.pauseOnEco);
    services.pauseOnHighCpu = getBool("adv_svc_pause_highcpu", services.pauseOnHighCpu);
    services.highCpuThreshold = static_cast<uint32_t>(getInt("adv_svc_highcpu_thresh", static_cast<int>(services.highCpuThreshold)));
    services.pauseBloatwarePermanent = getBool("adv_svc_bloat_perm", services.pauseBloatwarePermanent);
    services.aiAutonomyLevel = getInt("adv_svc_ai_autonomy", services.aiAutonomyLevel);
    services.manageWSearch = getBool("adv_svc_wsearch", services.manageWSearch);
    services.manageSpooler = getBool("adv_svc_spooler", services.manageSpooler);
    services.manageDiagTrack = getBool("adv_svc_diagtrack", services.manageDiagTrack);
    services.manageSysMain = getBool("adv_svc_sysmain", services.manageSysMain);
    services.manageThirdPartyUpdaters = getBool("adv_svc_thirdparty", services.manageThirdPartyUpdaters);
    services.restoreOnExit = getBool("adv_svc_restore_exit", services.restoreOnExit);
    services.checkDependencies = getBool("adv_svc_check_deps", services.checkDependencies);
    services.customExcludedServices = db.GetSetting("adv_svc_excluded", services.customExcludedServices);
    services.customUserExcludedServices = db.GetSetting("adv_svc_user_excluded", services.customUserExcludedServices);
    services.pauseWhenIdle = getBool("adv_svc_pause_idle", services.pauseWhenIdle);
    services.idleSecondsBeforePause =
        static_cast<uint32_t>(getInt("adv_svc_idle_secs", static_cast<int>(services.idleSecondsBeforePause)));
    services.allowTruePause = getBool("adv_svc_true_pause", services.allowTruePause);
    services.minGainMB = static_cast<uint32_t>(getInt("adv_svc_min_gain_mb", static_cast<int>(services.minGainMB)));
    services.maintenanceHour = getInt("adv_svc_maint_hour", services.maintenanceHour);
    services.maintenanceMinutes = getInt("adv_svc_maint_mins", services.maintenanceMinutes);

    ValidateAll();

    // Sanitização defensiva de inicialização (Boot Safety Guard):
    // Impede que valores extremos de testes anteriores congelem a CPU (ex: cap 37% abaixo do P2 64% do Ryzen).
    // O 'forceEcoPlan' persistido era desligado aqui com o pretexto de evitar suspensao de barramentos no boot,
    // mas as duas coisas sao linhas separadas: quem corta USB/PCIe e pauseIdleDevicesEco (adv_pwr_pause_idle_devices),
    // que o CoreRuntime aplica por conta propria. Desligar a imposicao apagava, a cada inicio, a escolha explicita
    // da usuaria no botao "Forcar plano economico" da janela avancada. Agora a escolha sobrevive ao reinicio.
    if (powerPlans.forceEcoPlan) {
        OPIT_LOG_INFO("ModuleAdvancedSettings: 'Forcar plano economico' persistido sera honrado nesta sessao.");
    }
    // Preserva integralmente os valores salvos pelo usuario
    if (cpuMinMax.minPercentAC > cpuMinMax.maxPercentAC) {
        cpuMinMax.minPercentAC = 5;
    }
    if (cpuMinMax.minPercentDC > cpuMinMax.maxPercentDC) {
        cpuMinMax.minPercentDC = 5;
    }

    OPIT_LOG_INFO("ModuleAdvancedSettings: Configuracoes avancadas carregadas com sucesso.");
    return true;
}

} // namespace opitcore
