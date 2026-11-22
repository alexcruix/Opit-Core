#include "hardware/GpuControlManager.h"
#include "control/ModeCoordinator.h"
#include "control/GameDetector.h"
#include "power/PowerManager.h"
#include "platform/windows/ProcessManager.h"
#include "platform/windows/ServiceManager.h"
#include "core/DatabaseManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/Config.h"
#include "utils/FileUtils.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>

using namespace opitcore;

static void TestGpuDetectionAndMocks() {
    std::cout << "[TEST] 1. Interrogacao de GPU e Mocks por Fabricante..." << std::endl;

    auto& gpuMgr = GpuControlManager::Instance();
    gpuMgr.Initialize();

    size_t gpuCount = gpuMgr.GetGpuCount();
    std::cout << "  GPUs detectadas: " << gpuCount << std::endl;
    for (size_t i = 0; i < gpuCount; ++i) {
        auto caps = gpuMgr.GetCapabilities(i);
        auto stat = gpuMgr.GetStatus(i);
        std::cout << "  - Adaptador #" << i << ": " << caps.adapterName << " (" << GpuVendorToString(caps.vendor) << ")" << std::endl;
        std::cout << "    Caps: CoreClock=" << caps.canControlCoreClock
                  << ", MemClock=" << caps.canControlMemoryClock
                  << ", PowerLimit=" << caps.canControlPowerLimit
                  << " (" << caps.minPowerLimitPercent << "% a " << caps.maxPowerLimitPercent << "%)" << std::endl;
        std::cout << "    Status: CoreOffset=" << stat.appliedCoreClockOffsetMHz
                  << " MHz, MemOffset=" << stat.appliedMemoryClockOffsetMHz
                  << " MHz, PowerLimit=" << stat.appliedPowerLimitPercent << "%" << std::endl;
    }

    // 1.1 Injetar Mock AMD Radeon RX 7900 XT (ADLX)
    GpuControlCapabilities mockAmdCaps;
    mockAmdCaps.supported = true;
    mockAmdCaps.vendor = GpuVendor::Amd;
    mockAmdCaps.adapterName = "AMD Radeon RX 7900 XT (ADLX Mock)";
    mockAmdCaps.canControlCoreClock = true;
    mockAmdCaps.canControlMemoryClock = true;
    mockAmdCaps.canControlPowerLimit = true;
    mockAmdCaps.minCoreClockOffsetMHz = -200;
    mockAmdCaps.maxCoreClockOffsetMHz = 250;
    mockAmdCaps.minMemoryClockOffsetMHz = -150;
    mockAmdCaps.maxMemoryClockOffsetMHz = 300;
    mockAmdCaps.minPowerLimitPercent = 60;
    mockAmdCaps.maxPowerLimitPercent = 115;

    gpuMgr.SetMockGpu(GpuVendor::Amd, mockAmdCaps);
    assert(gpuMgr.HasMockGpu());
    assert(gpuMgr.GetGpuCount() > 0);

    auto readAmd = gpuMgr.GetCapabilities(0);
    assert(readAmd.vendor == GpuVendor::Amd);
    assert(readAmd.canControlPowerLimit);
    assert(readAmd.maxPowerLimitPercent == 115);

    // 1.2 Testar validação de limites de hardware seguro (AMD)
    std::string errMsg;
    // Tentar aplicar overclock excessivo (fora dos limites do fabricante)
    bool rejAmd = gpuMgr.ApplyCustomSettings(0, 600, 100, 100, errMsg);
    assert(!rejAmd);
    std::cout << "  Rejeicao segura de clock AMD acima do limite: " << errMsg << std::endl;

    // Tentar aplicar power limit abaixo do mínimo seguro
    bool rejPwr = gpuMgr.ApplyCustomSettings(0, 0, 0, 30, errMsg);
    assert(!rejPwr);
    std::cout << "  Rejeicao segura de Power Limit AMD abaixo de 60%: " << errMsg << std::endl;

    // Aplicar parâmetros dentro dos limites válidos
    bool okAmd = gpuMgr.ApplyCustomSettings(0, 100, 150, 105, errMsg);
    assert(okAmd);
    auto postAmd = gpuMgr.GetStatus(0);
    assert(postAmd.requestedCoreClockOffsetMHz == 100);
    assert(postAmd.appliedCoreClockOffsetMHz == 100);
    assert(postAmd.requestedPowerLimitPercent == 105);

    // 1.3 Injetar Mock Intel Arc A770 (IGCL)
    GpuControlCapabilities mockIntelCaps;
    mockIntelCaps.supported = true;
    mockIntelCaps.vendor = GpuVendor::Intel;
    mockIntelCaps.adapterName = "Intel Arc A770 (IGCL Mock)";
    mockIntelCaps.canControlCoreClock = true;
    mockIntelCaps.canControlPowerLimit = true;
    mockIntelCaps.canControlMemoryClock = false; // Intel Arc não expõe offset livre de VRAM
    mockIntelCaps.minCoreClockOffsetMHz = 0;
    mockIntelCaps.maxCoreClockOffsetMHz = 150;
    mockIntelCaps.minPowerLimitPercent = 70;
    mockIntelCaps.maxPowerLimitPercent = 120;

    gpuMgr.SetMockGpu(GpuVendor::Intel, mockIntelCaps);
    auto readIntel = gpuMgr.GetCapabilities(0);
    assert(readIntel.vendor == GpuVendor::Intel);
    assert(!readIntel.canControlMemoryClock);

    // Testar rejeição se tentar ajustar offset de VRAM na Intel
    bool rejIntelMem = gpuMgr.ApplyCustomSettings(0, 50, 200, 100, errMsg);
    assert(!rejIntelMem);
    std::cout << "  Rejeicao de controle de memoria nao suportado na Intel: " << errMsg << std::endl;

    // Testar perfis pré-definidos (Eco, Balanced, HighPerformance)
    bool okIntelEco = gpuMgr.ApplyProfile(0, GpuPowerProfile::Eco, errMsg);
    assert(okIntelEco);
    auto postIntel = gpuMgr.GetStatus(0);
    assert(postIntel.currentProfile == GpuPowerProfile::Eco);
    assert(postIntel.appliedPowerLimitPercent <= 85);

    gpuMgr.ClearMockGpu();
    std::cout << "[PASS] 1. Interrogacao de GPU, limites de fornecedor e Mocks aprovados!" << std::endl;
}

static void TestCpuMinMaxNormalizedScale() {
    std::cout << "[TEST] 2. Escala Normalizada de CPU Min/Max (0 a 100)..." << std::endl;

    PowerManager pwr;
    pwr.Initialize();

    // 2.1 Aplicar faixa válida (5 a 100)
    bool ok1 = pwr.SetProcessorThrottleRange(5, 100);
    assert(ok1);

    // 2.2 Aplicar faixa econômica (0 a 60)
    bool ok2 = pwr.SetProcessorThrottleRange(0, 60);
    assert(ok2);

    // 2.3 Rejeição quando Min > Max
    bool rej = pwr.SetProcessorThrottleRange(80, 50);
    assert(!rej);
    std::cout << "  Invariante: Rejeicao de Min(80) > Max(50) confirmada (sem sobrescrever valores)." << std::endl;

    // 2.4 Validação no ModuleAdvancedSettings
    AdvCpuMinMaxConfig cfg;
    cfg.minPercentAC = 90;
    cfg.maxPercentAC = 70;
    std::string valErr;
    bool valid = cfg.Validate(&valErr);
    assert(!valid);
    assert(cfg.minPercentAC == 90); // Não altera silenciosamente o valor salvo!
    assert(cfg.maxPercentAC == 70);
    std::cout << "  ModuleAdvancedSettings rejeita conflito min>max com mensagem: " << valErr << std::endl;

    std::cout << "[PASS] 2. Escala Normalizada de CPU Min/Max aprovada!" << std::endl;
}

static void TestGameProfilesDatabaseAndEdit() {
    std::cout << "[TEST] 3. Banco de Dados SQLite: Criacao, Edicao e Exclusao de Perfis de Jogo..." << std::endl;

    std::string dbPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "test_game_profiles.db");
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);

    auto& db = DatabaseManager::Instance();
    assert(db.Open(dbPath));

    // 3.1 Criar perfil de jogo
    GameProfileRecord prof;
    prof.exeName = "Cyberpunk2077.exe";
    prof.gameName = "Cyberpunk 2077";
    prof.fullPath = "C:\\Games\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe";
    prof.cpuProfile = "Alto Desempenho";
    prof.cpuMinPercent = 10;
    prof.cpuMaxPercent = 100;
    prof.gpuProfile = "HighPerformance";
    prof.gpuPowerLimitPercent = 105;
    prof.gpuCoreOffsetMHz = 75;
    prof.gpuMemOffsetMHz = 150;
    prof.powerSchemeGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
    prof.preventDiskSleep = true;
    prof.preventSystemSleep = true;
    prof.enabled = true;

    assert(db.SaveGameProfile(prof));

    // 3.2 Consultar perfil salvo
    GameProfileRecord loaded;
    assert(db.GetGameProfile("cyberpunk2077.exe", loaded));
    assert(loaded.gameName == "Cyberpunk 2077");
    assert(loaded.cpuMinPercent == 10);
    assert(loaded.cpuMaxPercent == 100);
    assert(loaded.gpuPowerLimitPercent == 105);
    assert(loaded.preventDiskSleep == true);

    // 3.3 Fluxo de Edição Dedicado (Alterar configurações sem perder as anteriores)
    loaded.cpuMinPercent = 20;
    loaded.gpuPowerLimitPercent = 110;
    loaded.gpuCoreOffsetMHz = 90;
    assert(db.SaveGameProfile(loaded));

    GameProfileRecord edited;
    assert(db.GetGameProfile("Cyberpunk2077.exe", edited));
    assert(edited.cpuMinPercent == 20);
    assert(edited.gpuPowerLimitPercent == 110);
    assert(edited.gpuCoreOffsetMHz == 90);
    assert(edited.preventDiskSleep == true); // Preservou outros campos

    // 3.4 Listar todos
    auto all = db.GetAllGameProfiles();
    assert(!all.empty());
    assert(all[0].exeName == "cyberpunk2077.exe");

    // 3.5 Excluir
    assert(db.DeleteGameProfile("cyberpunk2077.exe"));
    GameProfileRecord missing;
    assert(!db.GetGameProfile("cyberpunk2077.exe", missing));

    db.Close();
    std::filesystem::remove(dbPath, ec);
    std::cout << "[PASS] 3. CRUD e Edicao de Perfis de Jogo no SQLite aprovados!" << std::endl;
}

static void TestSimultaneousGamesAndModeCoordinator() {
    std::cout << "[TEST] 4. Coordenador Central e Politica de Jogos Simultaneos..." << std::endl;

    auto& coord = ModeCoordinator::Instance();
    assert(coord.Initialize());

    // Configurar banco temporário para o teste
    std::string dbPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "test_coord.db");
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    DatabaseManager::Instance().Open(dbPath);

    // Cadastrar Game A (ex: launcher / game 1)
    GameProfileRecord profA;
    profA.exeName = "GameA.exe";
    profA.gameName = "Game Alpha";
    profA.cpuMinPercent = 5;
    profA.cpuMaxPercent = 95;
    profA.gpuProfile = "Balanced";
    profA.preventDiskSleep = true;
    profA.preventSystemSleep = true;
    DatabaseManager::Instance().SaveGameProfile(profA);

    // Cadastrar Game B (ex: game 2)
    GameProfileRecord profB;
    profB.exeName = "GameB.exe";
    profB.gameName = "Game Beta Heavy";
    profB.cpuMinPercent = 15;
    profB.cpuMaxPercent = 100;
    profB.gpuProfile = "HighPerformance";
    profB.preventDiskSleep = true;
    profB.preventSystemSleep = true;
    DatabaseManager::Instance().SaveGameProfile(profB);

    std::string msg;

    // Estado inicial: Normal
    assert(coord.GetCurrentMode() == SystemOperatingMode::Normal);

    // 4.1 Iniciar Game A
    assert(coord.OnGameStarted("GameA.exe", msg));
    assert(coord.IsGameModeActive());
    assert(coord.GetActiveGameCount() == 1);
    assert(coord.GetCurrentMode() == SystemOperatingMode::Gaming);
    std::cout << "  Game A iniciado -> " << msg << std::endl;

    // 4.2 Iniciar Game B (Simultâneo)
    assert(coord.OnGameStarted("GameB.exe", msg));
    assert(coord.IsGameModeActive());
    assert(coord.GetActiveGameCount() == 2);
    std::cout << "  Game B iniciado (2 jogos simultaneos) -> " << msg << std::endl;

    // 4.3 Fechar Game A (Game B continua ativo!)
    // POLÍTICA: NÃO deve restaurar configurações para desktop enquanto Game B estiver rodando!
    assert(coord.OnGameStopped("GameA.exe", msg));
    assert(coord.IsGameModeActive());
    assert(coord.GetActiveGameCount() == 1);
    assert(coord.GetCurrentMode() == SystemOperatingMode::Gaming);
    std::cout << "  Game A finalizado -> " << msg << std::endl;

    // 4.4 Fechar Game B (Último jogo finalizado)
    // Agora sim deve restaurar o estado da área de trabalho
    assert(coord.OnGameStopped("GameB.exe", msg));
    assert(!coord.IsGameModeActive());
    assert(coord.GetActiveGameCount() == 0);
    assert(coord.GetCurrentMode() == SystemOperatingMode::Normal);
    std::cout << "  Game B finalizado -> " << msg << std::endl;

    // 4.5 Testar Economia Máxima (Deep Eco)
    assert(coord.SetDeepEcoMode(true, msg));
    assert(coord.IsDeepEcoModeActive());
    assert(coord.GetCurrentMode() == SystemOperatingMode::MaxEconomy);
    std::cout << "  Economia Maxima ativada -> " << msg << std::endl;

    // Se um jogo iniciar durante Economia Máxima, a prioridade é o JOGO!
    assert(coord.OnGameStarted("GameB.exe", msg));
    assert(coord.GetCurrentMode() == SystemOperatingMode::Gaming);

    // Ao fechar o jogo, a governança retorna ordenadamente
    assert(coord.OnGameStopped("GameB.exe", msg));

    // Desativar Economia Máxima
    assert(coord.SetDeepEcoMode(false, msg));
    assert(!coord.IsDeepEcoModeActive());
    assert(coord.GetCurrentMode() == SystemOperatingMode::Normal);
    std::cout << "  Economia Maxima desativada -> " << msg << std::endl;

    DatabaseManager::Instance().Close();
    std::filesystem::remove(dbPath, ec);
    std::cout << "[PASS] 4. Coordenador e Politica de Jogos Simultaneos aprovados!" << std::endl;
}

static void TestStartupFreezeGuardrails() {
    std::cout << "[TEST] 5. Guardrails de Inicializacao Segura (Prevenção de Congelamento AM4/X370)..." << std::endl;

    std::string dbPath = FileUtils::CombinePath(FileUtils::GetAppDataDirectory(), "test_boot_guard.db");
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);

    auto& db = DatabaseManager::Instance();
    assert(db.Open(dbPath));

    // Simular valores extremos corrompidos de sessao anterior que causavam congelamento no boot
    db.SetSetting("adv_pwr_force_eco", "1");
    db.SetSetting("adv_cpu_min_ac", "5");
    db.SetSetting("adv_cpu_max_ac", "37");
    db.SetSetting("adv_cp_active_ac", "25");
    db.SetSetting("adv_cp_force_max", "1");
    db.SetSetting("adv_proc_prio_nowin", "64"); // IDLE_PRIORITY_CLASS

    auto& adv = ModuleAdvancedSettings::Instance();
    assert(adv.Load());

    // Verificar que o Boot Safety Guard neutralizou os parâmetros perigosos na partida
    assert(!adv.powerPlans.forceEcoPlan);
    assert(adv.cpuMinMax.maxPercentAC == 100);
    assert(adv.coreParking.minCoresActiveAC == 100);
    assert(!adv.coreParking.forceMaxParking);
    assert(adv.processes.prioNoVisibleWindow == 32); // Restaurado para NORMAL_PRIORITY_CLASS

    // Verificar proteção de processos críticos de entrada/driver/sistema
    assert(ProcessManager::IsCriticalProcess("ctfmon.exe"));
    assert(ProcessManager::IsCriticalProcess("WmiPrvSE.exe"));
    assert(ProcessManager::IsCriticalProcess("NVDisplay.Container.exe"));
    assert(ProcessManager::IsCriticalProcess("RuntimeBroker.exe"));
    assert(ProcessManager::IsCriticalProcess("audiodg.exe"));

    // Verificar que PowerManager grava e lê PROCTHROTTLEMIN oficial (893dee8e-2bef-41e0-89c6-b55d0929964c)
    PowerManager pwr;
    pwr.Initialize();
    assert(pwr.SetProcessorThrottleRange(5, 100));
    auto activeScheme = pwr.GetActiveScheme();
    uint32_t minAc = 0, maxAc = 0, minDc = 0, maxDc = 0;
    assert(pwr.ReadSchemeProcessorThrottle(activeScheme.guid, minAc, maxAc, minDc, maxDc));
    assert(minAc == 5);
    assert(maxAc == 100);

    db.Close();
    std::filesystem::remove(dbPath, ec);
    std::cout << "[PASS] 5. Guardrails de Inicializacao Segura aprovados!" << std::endl;
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "=== OPIT-CORE: TESTE EXPANDIDO DE GPU E PERFIS DE JOGO ===" << std::endl;
    std::cout << "==========================================================" << std::endl;

    try {
        TestGpuDetectionAndMocks();
        TestCpuMinMaxNormalizedScale();
        TestGameProfilesDatabaseAndEdit();
        TestSimultaneousGamesAndModeCoordinator();
        TestStartupFreezeGuardrails();

        std::cout << "==========================================================" << std::endl;
        std::cout << "TODOS OS 5 TESTES DE GPU, JOGOS, COORDENACAO E BOOT GUARD PASSARAM COM SUCESSO!" << std::endl;
        std::cout << "==========================================================" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FALHA CRITICA: Excecao no teste: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FALHA CRITICA: Excecao desconhecida no teste!" << std::endl;
        return 2;
    }
}
