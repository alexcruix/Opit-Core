#pragma once
#include <string>
#include <vector>

namespace opitcore {

struct ModuleLevelSetting {
    std::string level = "med"; // "min", "med", "max", "ultra"
    bool autoSmartMode = false;
};

class UnifiedSettingsModel {
public:
    static UnifiedSettingsModel& Instance();

    void Load();
    bool Save();

    // 1. Seção de Ativação Global
    bool enableOrchestrator = true;
    bool smartOrchestrator = true;

    // 2. Seção de Módulos (Grid)
    // Coluna Esquerda:
    bool modPowerPlans = true;
    bool modCoreParking = true;
    bool modGpuControl = true;
    bool modThermalGuardian = true;
    bool modAdaptiveHardware = true;
    bool modWindowsServices = true;
    bool subSmartServices = true; // Sub-checkbox aninhado
    bool modSystemStability = true;
    bool modPauseIndexingIdle = false;
    bool disableMicrosoftTelemetry = false;
    bool smartGamingServiceOptimization = true;

    // Coluna Direita:
    bool modCpuMinMax = true;
    bool modProcessControl = true;
    bool modFanCurve = true;
    bool modDeepEco = true;
    bool pauseIdleDevicesEco = false; // Pausar dispositivos e controladores ociosos em modo Economico (USB/PCIe)
    bool modAppPriority = true;

    // 3. Nível por módulo (min / med / max / ultra)
    ModuleLevelSetting levelPowerPlans{"med", false};
    ModuleLevelSetting levelCpuMinMax{"med", false};
    ModuleLevelSetting levelCoreParking{"med", false};
    ModuleLevelSetting levelProcesses{"med", false};
    ModuleLevelSetting levelGpu{"med", false};
    ModuleLevelSetting levelFans{"med", false};
    ModuleLevelSetting levelThermalGuardian{"med", false};
    ModuleLevelSetting levelStability{"med", false};
    ModuleLevelSetting levelServices{"med", true}; // Padrão: marcado

    // 5. Agrupamento svchost
    int svchostGrouping = 0; // 0: Minimo (Mais isolamento/seguro), 1: Padrao Windows, 2: Maximo Agrupamento

private:
    UnifiedSettingsModel() = default;
};

} // namespace opitcore
