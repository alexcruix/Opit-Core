// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "control/GameDetector.h"
#include <iostream>
#include <vector>

int main() {
    opitcore::GameDetector gd;

    // 1. Jogos padrão
    if (!gd.IsGameProcess("cs2.exe")) {
        std::cerr << "Falha: cs2.exe deveria estar nos padroes" << std::endl;
        return 1;
    }
    if (!gd.IsGameProcess("CS2.EXE")) { // Teste case-insensitive
        std::cerr << "Falha: CS2.EXE (maiusculo) deveria corresponder" << std::endl;
        return 1;
    }

    // 2. Adicionar jogo
    gd.AddGame("novo_jogo_custom.exe");
    if (!gd.IsGameProcess("novo_jogo_custom.exe")) {
        std::cerr << "Falha: novo_jogo_custom.exe deveria ser detectado" << std::endl;
        return 1;
    }

    // 3. Varredura de processos
    std::vector<std::string> procs = {"notepad.exe", "explorer.exe", "novo_jogo_custom.exe", "chrome.exe"};
    std::string running = gd.CheckRunningGame(procs);
    if (running != "novo_jogo_custom.exe") {
        std::cerr << "Falha: CheckRunningGame deveria encontrar o jogo em execucao: " << running << std::endl;
        return 1;
    }

    // 4. Remover jogo
    if (!gd.RemoveGame("novo_jogo_custom.exe")) {
        std::cerr << "Falha: RemoveGame deveria retornar true" << std::endl;
        return 1;
    }
    if (gd.IsGameProcess("novo_jogo_custom.exe")) {
        std::cerr << "Falha: Jogo removido nao deveria ser detectado" << std::endl;
        return 1;
    }

    std::cout << "test_control: SUCESSO" << std::endl;
    return 0;
}
