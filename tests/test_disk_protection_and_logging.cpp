// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include <iostream>
#include <cassert>
#include "power/PowerManager.h"
#include "core/Logger.h"

using namespace opitcore;

int main() {
    std::cout << "Iniciando test_disk_protection_and_logging..." << std::endl;

    // 1. Logger por categorias, lido de volta pela fila local (nada sai da maquina)
    {
        Logger::Instance().SetLevel(LogLevel::Debug);
        OPIT_LOG_DISK(LogLevel::Warning, "Teste: Latencia do HD atingiu 1200ms.");
        OPIT_LOG_POWER(LogLevel::Info, "Teste: Protecao de spin-down ativada.");

        auto recent = Logger::Instance().GetRecentLogs(50);
        assert(!recent.empty());

        bool foundDisk = false;
        bool foundPower = false;

        for (const auto& line : recent) {
            if (line.find("[DISK]") != std::string::npos && line.find("[WARN]") != std::string::npos &&
                line.find("1200ms") != std::string::npos) {
                foundDisk = true;
            }
            if (line.find("[POWER]") != std::string::npos && line.find("[INFO]") != std::string::npos &&
                line.find("spin-down ativada") != std::string::npos) {
                foundPower = true;
            }
        }

        assert(foundDisk && "Evento de disco nao saiu na fila de logs recentes com categoria e nivel.");
        assert(foundPower && "Evento de energia nao saiu na fila de logs recentes com categoria e nivel.");
        std::cout << "[PASS] Logger estruturado por categoria e nivel validado pela leitura local." << std::endl;
    }

    // 2. PowerManager e prevencao de desligamento de disco
    {
        PowerManager pm;
        pm.Initialize();

        bool wasPrevented = pm.IsDiskPowerDownPrevented();
        assert(!wasPrevented);

        pm.PreventDiskPowerDown(true);
        assert(pm.IsDiskPowerDownPrevented());

        pm.PreventDiskPowerDown(false);
        assert(!pm.IsDiskPowerDownPrevented());
        std::cout << "[PASS] Gerenciamento e protecao contra spin-down de disco no PowerManager validados." << std::endl;
    }

    std::cout << "Todos os testes de test_disk_protection_and_logging passaram com sucesso!" << std::endl;
    return 0;
}
