// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "hardware/HardwareDetection.h"
#include <iostream>

int main() {
    auto allHw = opitcore::HardwareDetection::DetectAll();

    // 1. CPU
    if (allHw.cpu.vendor.empty()) {
        std::cerr << "Falha: Vendor da CPU vazio" << std::endl;
        return 1;
    }
    if (allHw.cpu.physicalCores <= 0) {
        std::cerr << "Falha: Nucleos fisicos <= 0" << std::endl;
        return 1;
    }
    if (allHw.cpu.logicalThreads <= 0) {
        std::cerr << "Falha: Threads logicas <= 0" << std::endl;
        return 1;
    }

    // 2. Memória RAM
    if (allHw.memory.totalPhysicalBytes == 0) {
        std::cerr << "Falha: Memoria fisica total = 0" << std::endl;
        return 1;
    }
    if (allHw.memory.memoryLoadPercent > 100) {
        std::cerr << "Falha: Carga de memoria > 100%" << std::endl;
        return 1;
    }

    // 3. SO
    if (allHw.osName.empty()) {
        std::cerr << "Falha: Nome do SO vazio" << std::endl;
        return 1;
    }

    std::cout << "test_hardware: SUCESSO (CPU: " << allHw.cpu.brand 
              << ", RAM: " << (allHw.memory.totalPhysicalBytes / (1024*1024)) << " MB"
              << ", GPUs detectadas: " << allHw.gpus.size() << ")" << std::endl;
    return 0;
}
