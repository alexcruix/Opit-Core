// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "power/PowerManager.h"
#include <iostream>

int main() {
    opitcore::PowerManager pm;
    if (!pm.Initialize()) {
        std::cerr << "Falha ao inicializar PowerManager" << std::endl;
        return 1;
    }

    auto schemes = pm.ListSchemes();
    if (schemes.empty()) {
        std::cerr << "Falha: Nenhum plano de energia encontrado" << std::endl;
        return 1;
    }

    auto active = pm.GetActiveScheme();
    if (active.guid.empty()) {
        std::cerr << "Falha: GUID do plano ativo vazio" << std::endl;
        return 1;
    }

    uint32_t throttle = pm.GetProcessorThrottleLimit();
    if (throttle < 20 || throttle > 100) {
        std::cerr << "Falha: Limite de processador fora da faixa valida: " << throttle << std::endl;
        return 1;
    }

    auto bat = pm.GetBatteryStatus();

    std::cout << "test_power: SUCESSO (Planos: " << schemes.size() 
              << ", Ativo: " << active.name 
              << ", Throttle CPU: " << throttle << "%)" << std::endl;
    return 0;
}
