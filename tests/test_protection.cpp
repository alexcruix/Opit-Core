// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "protection/ThermalProtection.h"
#include <iostream>

int main() {
    opitcore::ThermalProtection tp;

    // 1. Sem sensor -> Action::None
    if (tp.Evaluate(90.0, false, 80.0) != opitcore::ThermalProtection::Action::None) {
        std::cerr << "Falha: Sem sensor deveria retornar None" << std::endl;
        return 1;
    }

    // 2. Temperatura abaixo do limite -> Action::None
    if (tp.Evaluate(75.0, true, 80.0) != opitcore::ThermalProtection::Action::None) {
        std::cerr << "Falha: Temperatura normal deveria retornar None" << std::endl;
        return 1;
    }

    // 3. Temperatura acima do limite -> Action::Throttle
    if (tp.Evaluate(82.0, true, 80.0) != opitcore::ThermalProtection::Action::Throttle) {
        std::cerr << "Falha: Temperatura acima do limite deveria disparar Throttle" << std::endl;
        return 1;
    }
    if (!tp.IsThrottling()) {
        std::cerr << "Falha: IsThrottling deveria ser true" << std::endl;
        return 1;
    }

    // 4. Temperatura ainda em histerese (78°C com limite 80°C - restauração requer < 75°C) -> Action::None
    if (tp.Evaluate(78.0, true, 80.0) != opitcore::ThermalProtection::Action::None) {
        std::cerr << "Falha: Temperatura na zona de histerese deveria manter Throttle sem novo evento" << std::endl;
        return 1;
    }

    // 5. Temperatura resfriada (< 75°C) -> Action::Restore
    if (tp.Evaluate(72.0, true, 80.0) != opitcore::ThermalProtection::Action::Restore) {
        std::cerr << "Falha: Temperatura resfriada deveria disparar Restore" << std::endl;
        return 1;
    }
    if (tp.IsThrottling()) {
        std::cerr << "Falha: IsThrottling deveria ser false apos Restore" << std::endl;
        return 1;
    }

    std::cout << "test_protection: SUCESSO" << std::endl;
    return 0;
}
