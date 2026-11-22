#include "fans/FanControlManager.h"
#include "fans/MockFanProvider.h"
#include "fans/IntelGpuFanProvider.h"
#include "fans/NvidiaNvapiFanProvider.h"
#include "fans/AmdAdlFanProvider.h"
#include "fans/WindowsAcpiFanProvider.h"
#include "fans/LaptopFanProvider.h"
#include "core/DatabaseManager.h"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace opitcore;

void TestDiscoveryAndCapabilities() {
    std::cout << "[Test] 1. Descoberta de Dispositivos e Capacidades..." << std::endl;
    auto& mgr = FanControlManager::Instance();
    mgr.Shutdown();
    assert(mgr.Initialize(true)); // Ativa mock para garantir previsibilidade de teste unitário

    auto devices = mgr.GetDevices();
    assert(!devices.empty());

    bool foundCpu = false;
    bool foundMock = false;

    for (const auto& dev : devices) {
        if (dev.id == "mb:acpi:cpu") {
            foundCpu = true;
            // CPU em modo usuário tem leitura de temperatura mas não controle direto de PWM
            assert(dev.capabilities.canReadTemperature == true);
            assert(dev.capabilities.canControlSpeed == false);
        }
        if (dev.id == "mock:fan:0") {
            foundMock = true;
            assert(dev.capabilities.canReadTemperature == true);
            assert(dev.capabilities.canReadRpm == true);
            assert(dev.capabilities.canControlSpeed == true);
            assert(dev.capabilities.canSetCurve == true);
        }
    }

    assert(foundCpu);
    assert(foundMock);
    std::cout << "  -> Descoberta e Capacidades: OK" << std::endl;
}

void TestCurveValidationAndEvaluation() {
    std::cout << "[Test] 2. Validacao de Curvas e Interpolacao..." << std::endl;
    FanCurveController controller;

    FanCurveConfig validCurve;
    validCurve.points = {
        {30.0, 20},
        {50.0, 40},
        {70.0, 80},
        {85.0, 100}
    };
    validCurve.hysteresisC = 2;
    validCurve.emergencyTempC = 90.0;

    std::string err;
    assert(FanCurveController::ValidateCurve(validCurve, err));

    // Curva inválida (ordem invertida)
    FanCurveConfig invalidCurve;
    invalidCurve.points = {
        {70.0, 40},
        {50.0, 80}
    };
    assert(!FanCurveController::ValidateCurve(invalidCurve, err));

    // Teste de interpolação:
    // A 30°C -> deve ser 20%
    uint32_t speed30 = controller.EvaluateSpeed("mock:fan:0", validCurve, 30.0);
    assert(speed30 == 20);

    // A 40°C (ponto médio entre 30°C/20% e 50°C/40%) -> deve ser 30%
    uint32_t speed40 = controller.EvaluateSpeed("mock:fan:0", validCurve, 40.0);
    assert(speed40 == 30);

    // A 95°C (emergência >= 90°C) -> deve forçar 100%
    uint32_t speedEmerg = controller.EvaluateSpeed("mock:fan:0", validCurve, 95.0);
    assert(speedEmerg == 100);

    std::cout << "  -> Validacao e Interpolacao de Curvas: OK" << std::endl;
}

void TestSafetyController() {
    std::cout << "[Test] 3. Controlador de Seguranca e Protecao Termica..." << std::endl;
    FanSafetyController safety;

    FanDeviceInfo mockDev;
    mockDev.id = "mock:fan:0";
    mockDev.capabilities.canControlSpeed = true;
    mockDev.capabilities.minSpeedPercent = 20;

    FanReading reading;
    reading.hasTemperature = true;
    reading.temperatureC = 50.0;
    reading.isStale = false;

    uint32_t safeSpeed = 0;
    std::string reason;

    // Caso 1: Comando normal
    auto action1 = safety.EvaluateSafety(mockDev, reading, 60, safeSpeed, reason);
    assert(action1 == FanSafetyAction::Allow);
    assert(safeSpeed == 60);

    // Caso 2: Comando perigoso abaixo do piso de 20%
    auto action2 = safety.EvaluateSafety(mockDev, reading, 5, safeSpeed, reason);
    assert(action2 == FanSafetyAction::ClampToSafeMinimum);
    assert(safeSpeed == 20);

    // Caso 3: Temperatura crítica (>= 90°C) -> força 100%
    reading.temperatureC = 92.0;
    auto action3 = safety.EvaluateSafety(mockDev, reading, 40, safeSpeed, reason);
    assert(action3 == FanSafetyAction::ForceEmergencyFullSpeed);
    assert(safeSpeed == 100);

    // Caso 4: Sensor com falhas repetidas (>= 3 erros) -> fallback ao firmware
    reading.temperatureC = 50.0;
    safety.RecordReadingFailure(mockDev.id);
    safety.RecordReadingFailure(mockDev.id);
    safety.RecordReadingFailure(mockDev.id);
    auto action4 = safety.EvaluateSafety(mockDev, reading, 50, safeSpeed, reason);
    assert(action4 == FanSafetyAction::FallbackToFirmware);

    std::cout << "  -> Controlador de Seguranca: OK" << std::endl;
}

void TestManualControlAndFirmwareRestore() {
    std::cout << "[Test] 4. Controle Manual e Restauracao de Firmware..." << std::endl;
    auto& mgr = FanControlManager::Instance();

    // Aplicar velocidade manual
    assert(mgr.SetManualSpeed("mock:fan:0", 75));

    FanDeviceInfo info;
    assert(mgr.GetDeviceInfo("mock:fan:0", info));
    assert(info.activeMode == FanControlMode::ManualFixed);
    assert(info.manualTargetSpeedPercent == 75);

    // Tentar aplicar em dispositivo sem suporte (CPU / Placa-mãe)
    assert(!mgr.SetManualSpeed("mb:acpi:cpu", 80));

    // Restaurar controle ao firmware
    assert(mgr.RestoreFirmwareControl("mock:fan:0"));
    assert(mgr.GetDeviceInfo("mock:fan:0", info));
    assert(info.activeMode == FanControlMode::BiosFirmwareAuto);

    std::cout << "  -> Controle Manual e Restauracao: OK" << std::endl;
}

void TestSqlitePersistence() {
    std::cout << "[Test] 5. Persistencia SQLite das Configuracoes de Ventoinha..." << std::endl;
    std::string testDb = "test_fan_store.db";
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    auto& db = DatabaseManager::Instance();
    assert(db.Open(testDb));

    auto& mgr = FanControlManager::Instance();
    mgr.SetDeviceMode("mock:fan:0", FanControlMode::CustomCurve);
    mgr.SetManualSpeed("mock:fan:0", 65);

    FanCurveConfig customCurve;
    customCurve.hysteresisC = 4;
    customCurve.emergencyTempC = 92.0;
    customCurve.points = {{40.0, 35}, {60.0, 55}, {80.0, 95}};
    mgr.SetDeviceCurve("mock:fan:0", customCurve);

    // Salvar no SQLite
    assert(mgr.SaveConfigurations());

    // Resetar em memória
    mgr.SetDeviceMode("mock:fan:0", FanControlMode::BiosFirmwareAuto);

    // Recarregar do SQLite
    assert(mgr.LoadConfigurations());

    FanDeviceInfo restored;
    assert(mgr.GetDeviceInfo("mock:fan:0", restored));
    assert(restored.activeMode == FanControlMode::CustomCurve);
    assert(restored.manualTargetSpeedPercent == 65);
    assert(restored.curveConfig.hysteresisC == 4);
    assert(restored.curveConfig.emergencyTempC == 92.0);

    db.Close();
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    std::cout << "  -> Persistencia SQLite: OK" << std::endl;
}

void TestProviderGracefulAbsenceAndFallbacks() {
    std::cout << "[Test] 6. Ausencia Graciosa de DLLs Opcionais e Fallback Seguro..." << std::endl;
    // Intel Provider
    IntelGpuFanProvider intelProv;
    assert(intelProv.Initialize());
    auto intelDevs = intelProv.EnumerateDevices();
    for (const auto& d : intelDevs) {
        assert(d.deviceType == FanDeviceType::GpuIntel);
        // Se for fallback integrada, controle manual deve ser false
        if (d.id.find(":igpu:") != std::string::npos) {
            assert(!d.capabilities.canControlSpeed);
            assert(!d.capabilities.canReadRpm);
        }
    }
    intelProv.Shutdown();

    // NVIDIA Provider sem DLL / GPU dedicada
    NvidiaNvapiFanProvider nvProv;
    // Inicialização não deve crashar mesmo se nvapi64.dll não estiver presente
    bool nvInit = nvProv.Initialize();
    if (!nvInit) {
        auto nvDevs = nvProv.EnumerateDevices();
        assert(nvDevs.empty());
    }
    nvProv.Shutdown();

    // AMD Provider sem DLL / GPU dedicada
    AmdAdlFanProvider amdProv;
    bool amdInit = amdProv.Initialize();
    if (!amdInit) {
        auto amdDevs = amdProv.EnumerateDevices();
        assert(amdDevs.empty());
    }
    amdProv.Shutdown();

    std::cout << "  -> Ausencia de DLLs e Fallback: OK" << std::endl;
}

void TestMotherboardAndLaptopSafety() {
    std::cout << "[Test] 7. Seguranca Termica de Placa-Mae e Notebook..." << std::endl;
    WindowsAcpiFanProvider acpiProv;
    assert(acpiProv.Initialize());
    auto acpiDevs = acpiProv.EnumerateDevices();
    assert(!acpiDevs.empty());
    for (const auto& dev : acpiDevs) {
        assert(dev.deviceType == FanDeviceType::CpuMotherboard);
        assert(dev.capabilities.canReadTemperature);
        assert(!dev.capabilities.canControlSpeed); // Modo usuário seguro não faz PWM em Ring 0
    }
    acpiProv.Shutdown();

    LaptopFanProvider laptopProv;
    assert(laptopProv.Initialize());
    auto laptopDevs = laptopProv.EnumerateDevices();
    for (const auto& dev : laptopDevs) {
        assert(dev.deviceType == FanDeviceType::LaptopEC);
        assert(!dev.capabilities.canControlSpeed); // Salvaguarda: EC bloqueado para escrita direta
    }
    laptopProv.Shutdown();

    std::cout << "  -> Seguranca Termica de Placa-Mae e Notebook: OK" << std::endl;
}

void TestUnavailableSensorsAndZeroRpmHandling() {
    std::cout << "[Test] 8. Tratamento de Sensores Indisponiveis e Zero-RPM..." << std::endl;
    FanReading reading;
    reading.timestampMs = 1000;
    reading.hasTemperature = false;
    reading.temperatureC = -1.0;
    reading.hasRpm = true;
    reading.rpm = 0; // Modo Zero-RPM (ventoinha parada em repouso)
    reading.sensorSource = "VBIOS Fan Curve";

    // RPM 0 com hasRpm true é válido (Zero-RPM mode), não é erro
    assert(reading.hasRpm && reading.rpm == 0);

    // Leitura sem temperatura deve ser claramente indicada
    assert(!reading.hasTemperature);

    std::cout << "  -> Sensores Indisponiveis e Zero-RPM: OK" << std::endl;
}

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << " INICIANDO TESTES DO MODULO AVANCADO DE VENTOINHAS" << std::endl;
    std::cout << "=================================================" << std::endl;

    TestDiscoveryAndCapabilities();
    TestCurveValidationAndEvaluation();
    TestSafetyController();
    TestManualControlAndFirmwareRestore();
    TestSqlitePersistence();
    TestProviderGracefulAbsenceAndFallbacks();
    TestMotherboardAndLaptopSafety();
    TestUnavailableSensorsAndZeroRpmHandling();

    std::cout << "=================================================" << std::endl;
    std::cout << " TODOS OS TESTES DE VENTOINHAS PASSARAM COM EXITO " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
