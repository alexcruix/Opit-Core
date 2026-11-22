#include "core/UnifiedSettingsModel.h"
#include "core/DatabaseManager.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string testDb = "test_unified_settings.db";
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    auto& db = opitcore::DatabaseManager::Instance();
    if (!db.Open(testDb)) {
        std::cerr << "Falha ao abrir banco de teste para UnifiedSettingsModel" << std::endl;
        return 1;
    }

    auto& model = opitcore::UnifiedSettingsModel::Instance();
    
    // 1. Carregar valores padrão
    model.Load();
    if (!model.enableOrchestrator || !model.smartOrchestrator) {
        std::cerr << "Falha: valores padrao de ativacao global incorretos" << std::endl;
        return 1;
    }
    if (!model.subSmartServices) {
        std::cerr << "Falha: subSmartServices deveria ser true por padrao" << std::endl;
        return 1;
    }
    if (model.levelServices.level != "med" || !model.levelServices.autoSmartMode) {
        std::cerr << "Falha: levelServices inicial divergente (esperado med e auto=true)" << std::endl;
        return 1;
    }

    // 2. Modificar estados reativos
    model.modDeepEco = false;
    model.pauseIdleDevicesEco = true;
    model.levelCpuMinMax.level = "max";
    model.levelCpuMinMax.autoSmartMode = true;
    model.levelServices.level = "ultra";
    model.svchostGrouping = 1;

    // 3. Salvar no banco SQLite
    if (!model.Save()) {
        std::cerr << "Falha ao salvar UnifiedSettingsModel no banco" << std::endl;
        return 1;
    }

    // 4. Resetar em memória e recarregar
    model.modDeepEco = true;
    model.pauseIdleDevicesEco = false;
    model.levelCpuMinMax.level = "min";
    model.levelServices.level = "med";
    model.svchostGrouping = 0;

    model.Load();

    if (model.modDeepEco != false) {
        std::cerr << "Falha na persistencia: modDeepEco deveria ser false" << std::endl;
        return 1;
    }
    if (!model.pauseIdleDevicesEco) {
        std::cerr << "Falha na persistencia: pauseIdleDevicesEco deveria ser true" << std::endl;
        return 1;
    }
    if (model.levelCpuMinMax.level != "max" || !model.levelCpuMinMax.autoSmartMode) {
        std::cerr << "Falha na persistencia: levelCpuMinMax divergente" << std::endl;
        return 1;
    }
    if (model.levelServices.level != "ultra") {
        std::cerr << "Falha na persistencia: levelServices deveria ser ultra" << std::endl;
        return 1;
    }
    if (model.svchostGrouping != 1) {
        std::cerr << "Falha na persistencia: svchostGrouping deveria ser 1" << std::endl;
        return 1;
    }

    db.Close();
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    std::cout << "test_unified_settings: SUCESSO (Modelo reativo e persistencia validados)" << std::endl;
    return 0;
}
