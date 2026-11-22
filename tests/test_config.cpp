#include "core/Config.h"
#include <iostream>
#include <cassert>

int main() {
    auto& cfg = opitcore::Config::Instance();

    // 1. Perfis
    cfg.SetProfile(opitcore::Config::Profile::HighPerformance);
    if (cfg.GetProfile() != opitcore::Config::Profile::HighPerformance) {
        std::cerr << "Falha: Perfil HighPerformance esperado" << std::endl;
        return 1;
    }
    if (opitcore::Config::ProfileToString(cfg.GetProfile()) != "HighPerformance") {
        std::cerr << "Falha: ProfileToString incorreto" << std::endl;
        return 1;
    }

    cfg.SetProfile(opitcore::Config::Profile::Eco);
    if (cfg.GetProfile() != opitcore::Config::Profile::Eco) {
        std::cerr << "Falha: Perfil Eco esperado" << std::endl;
        return 1;
    }

    // 2. Limites térmicos
    cfg.SetThermalLimitC(85.0);
    if (cfg.GetThermalLimitC() != 85.0) {
        std::cerr << "Falha: Limite termico incorreto" << std::endl;
        return 1;
    }

    // 3. Jogos customizados
    cfg.AddCustomGame("my_game.exe");
    auto games = cfg.GetCustomGames();
    bool found = false;
    for (const auto& g : games) {
        if (g == "my_game.exe") found = true;
    }
    if (!found) {
        std::cerr << "Falha: Jogo customizado nao adicionado" << std::endl;
        return 1;
    }

    cfg.RemoveCustomGame("my_game.exe");

    // 4. Salvar e carregar
    std::string testPath = "test_opitcore_temp.json";
    if (!cfg.Save(testPath)) {
        std::cerr << "Falha ao salvar config" << std::endl;
        return 1;
    }
    if (!cfg.Load(testPath)) {
        std::cerr << "Falha ao carregar config" << std::endl;
        return 1;
    }
    std::filesystem::remove(testPath);

    std::cout << "test_config: SUCESSO" << std::endl;
    return 0;
}
