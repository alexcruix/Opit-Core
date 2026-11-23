// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "core/ModuleAdvancedSettings.h"
#include "core/DatabaseManager.h"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace opitcore;

void TestDefaultsAndValidation() {
    std::cout << "[Test] Advanced Settings Defaults and Validation..." << std::endl;
    auto& adv = ModuleAdvancedSettings::Instance();
    adv.ResetToDefaults(-1);

    // 1. CPU Min/Max validation
    adv.cpuMinMax.minPercentAC = 95;
    adv.cpuMinMax.maxPercentAC = 60; // Inválido: min > max
    adv.cpuMinMax.Validate();
    assert(adv.cpuMinMax.minPercentAC <= adv.cpuMinMax.maxPercentAC);
    assert(adv.cpuMinMax.minPercentAC == 60);

    // 2. Thermal limits validation
    adv.thermal.alertLimitC = 110.0; // Inválido: acima de 95
    adv.thermal.criticalLimitC = 80.0; // Inválido: menor que alert
    adv.thermal.Validate();
    assert(adv.thermal.criticalLimitC <= 100.0);
    assert(adv.thermal.alertLimitC < adv.thermal.criticalLimitC);

    // 3. Process exclusion validation
    assert(adv.processes.IsProcessExcluded("explorer.exe") == true);
    assert(adv.processes.IsProcessExcluded("EXPLORER.EXE") == true);
    assert(adv.processes.IsProcessExcluded("dwm.exe") == true);
    assert(adv.processes.IsProcessExcluded("cyberpunk2077.exe") == false);

    // 4. Services allowed validation
    adv.services.manageWSearch = false;
    assert(adv.services.IsServiceAllowed("WSearch") == false);
    adv.services.manageWSearch = true;
    assert(adv.services.IsServiceAllowed("WSearch") == true);
    assert(adv.services.IsServiceAllowed("RpcSs") == false); // Inviolável bloqueado

    std::cout << "  -> Defaults and Validation: OK" << std::endl;
}

void TestSqlitePersistence() {
    std::cout << "[Test] Advanced Settings SQLite Persistence..." << std::endl;
    std::string testDb = "test_adv_settings.db";
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    auto& db = DatabaseManager::Instance();
    assert(db.Open(testDb));

    auto& adv = ModuleAdvancedSettings::Instance();
    adv.ResetToDefaults(-1);

    // Customizar valores em múltiplos módulos
    adv.powerPlans.preferredSchemeAC = "{SCHEME-AC-TEST}";
    adv.powerPlans.preferredSchemeDC = "{SCHEME-DC-TEST}";
    adv.powerPlans.autoSwitchOnGame = false;
    adv.powerPlans.confirmationDelayMs = 4500;
    adv.powerPlans.forceEcoPlan = true;
    adv.powerPlans.forcedEcoPreviousSchemeGuid = "{TEST-GUID-1234}";
    adv.powerPlans.pauseIdleDevicesEco = true;

    adv.cpuMinMax.minPercentAC = 15;
    adv.cpuMinMax.maxPercentAC = 90;
    adv.cpuMinMax.ecoPlan = {5, 55, 5, 45};
    adv.cpuMinMax.balancedPlan = {5, 80, 5, 70};
    adv.cpuMinMax.highPerfPlan = {5, 95, 5, 90};
    adv.cpuMinMax.allowTurboBoost = false;
    adv.cpuMinMax.targetSchemeGuid = "{SCHEME-TEST-999}";
    adv.hwClassOverride = "notebook";

    adv.coreParking.minCoresActiveAC = 25;
    adv.coreParking.forceUnparkOnGame = true;
    adv.coreParking.forceMaxParking = true;

    adv.processes.autoProcessManagement = true;
    adv.processes.prioMaximized = 0x00000080; // High
    adv.processes.prioMinimized = 0x00000040; // Idle
    adv.processes.preserveAudioVideoTasks = true;

    adv.gpu.graphicsPreference = 1; // Economia
    adv.gpu.preferredAdapterName = "NVIDIA GeForce RTX 4070";

    adv.fans.controlMode = 2; // Curva
    adv.fans.thermalHysteresisC = 6;

    adv.thermal.alertLimitC = 78.5;
    adv.thermal.throttleActionCap = 60;

    adv.stability.observationWindowSamples = 8;
    adv.stability.minImprovementPercent = 4.5;

    adv.services.manageSpooler = false;

    // Salvar no banco
    assert(adv.Save());

    // Resetar em memória
    adv.ResetToDefaults(-1);
    assert(adv.cpuMinMax.maxPercentAC == 100);
    assert(adv.thermal.alertLimitC == 80.0);
    assert(adv.gpu.graphicsPreference == 2);
    assert(adv.fans.controlMode == 0);

    // Recarregar do banco
    assert(adv.Load());

    // Verificar se todas as personalizações foram restauradas
    assert(adv.powerPlans.preferredSchemeAC == "{SCHEME-AC-TEST}");
    assert(adv.powerPlans.preferredSchemeDC == "{SCHEME-DC-TEST}");
    assert(adv.powerPlans.autoSwitchOnGame == false);
    assert(adv.powerPlans.confirmationDelayMs == 4500);
    assert(adv.powerPlans.forceEcoPlan == true);
    assert(adv.powerPlans.forcedEcoPreviousSchemeGuid == "{TEST-GUID-1234}");
    assert(adv.powerPlans.pauseIdleDevicesEco == true);

    assert(adv.gpu.graphicsPreference == 1);
    assert(adv.gpu.preferredAdapterName == "NVIDIA GeForce RTX 4070");

    assert(adv.fans.controlMode == 2);
    assert(adv.fans.thermalHysteresisC == 6);

    assert(adv.cpuMinMax.minPercentAC == 15);
    assert(adv.cpuMinMax.maxPercentAC == 90);
    assert(adv.cpuMinMax.ecoPlan.maxPercentAC == 55);
    assert(adv.cpuMinMax.balancedPlan.maxPercentAC == 80);
    assert(adv.cpuMinMax.highPerfPlan.maxPercentAC == 95);
    assert(adv.cpuMinMax.allowTurboBoost == false);
    assert(adv.cpuMinMax.targetSchemeGuid == "{SCHEME-TEST-999}");
    // Override de classe sobrevive ao banco: Load ve a classe gravada ("notebook", do Save com o
    // override) igual a efetiva e NAO re-semeia — os limites customizados acima voltam intactos.
    assert(adv.hwClassOverride == "notebook");
    assert(adv.EffectiveHasBattery() == true);
    assert(adv.EffectiveHwClassName() == "notebook");

    assert(adv.coreParking.minCoresActiveAC == 25);
    assert(adv.coreParking.forceUnparkOnGame == true);
    assert(adv.coreParking.forceMaxParking == true);

    assert(adv.processes.autoProcessManagement == true);
    assert(adv.processes.prioMaximized == 0x00000080);
    assert(adv.processes.prioMinimized == 0x00000040);
    assert(adv.processes.preserveAudioVideoTasks == true);

    assert(adv.thermal.alertLimitC == 78.5);
    assert(adv.thermal.throttleActionCap == 60);

    assert(adv.stability.observationWindowSamples == 8);
    assert(adv.stability.minImprovementPercent == 4.5);

    assert(adv.services.manageSpooler == false);

    db.Close();
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    std::cout << "  -> SQLite Persistence: OK" << std::endl;
}

void TestHwClassOverride() {
    std::cout << "[Test] Hardware class override normalization..." << std::endl;
    // Valores validos passam; qualquer outra coisa (lixo, vazio, maiusculas com espaco) volta a auto.
    assert(ModuleAdvancedSettings::NormalizeHwClass("auto") == "auto");
    assert(ModuleAdvancedSettings::NormalizeHwClass("desktop") == "desktop");
    assert(ModuleAdvancedSettings::NormalizeHwClass("notebook") == "notebook");
    assert(ModuleAdvancedSettings::NormalizeHwClass("  NOTEBOOK  ") == "notebook");
    assert(ModuleAdvancedSettings::NormalizeHwClass("Desktop") == "desktop");
    assert(ModuleAdvancedSettings::NormalizeHwClass("") == "auto");
    assert(ModuleAdvancedSettings::NormalizeHwClass("tablet") == "auto");
    assert(ModuleAdvancedSettings::NormalizeHwClass("lixo qualquer") == "auto");
    std::cout << "  -> Hardware class override: OK" << std::endl;
}

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << " INICIANDO TESTES DOS PAINÉIS AVANÇADOS POR MÓDULO" << std::endl;
    std::cout << "=================================================" << std::endl;

    TestDefaultsAndValidation();
    TestSqlitePersistence();
    TestHwClassOverride();

    std::cout << "=================================================" << std::endl;
    std::cout << " TODOS OS TESTES DOS PAINÉIS AVANÇADOS PASSARAM  " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
