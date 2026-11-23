// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "core/DatabaseManager.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string testDb = "test_opitcore_database.db";
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    auto& db = opitcore::DatabaseManager::Instance();
    if (!db.Open(testDb)) {
        std::cerr << "Falha ao abrir banco de dados SQLite de teste" << std::endl;
        return 1;
    }

    // 1. Validar versão do schema (deve ser 2 devido a tabela de especificações documentadas)
    if (db.GetSchemaVersion() < 2) {
        std::cerr << "Falha: SchemaVersion < 2 (versao atual: " << db.GetSchemaVersion() << ")" << std::endl;
        return 1;
    }

    // 2. Testar configurações chave-valor
    if (!db.SetSetting("theme", "dark")) {
        std::cerr << "Falha ao gravar configuracao no SQLite" << std::endl;
        return 1;
    }
    std::string val = db.GetSetting("theme", "default");
    if (val != "dark") {
        std::cerr << "Falha ao ler configuracao do SQLite: " << val << std::endl;
        return 1;
    }

    // 3. Testar regras de jogos
    if (!db.AddGameRule("test_game.exe", "HighPerformance")) {
        std::cerr << "Falha ao adicionar regra de jogo no SQLite" << std::endl;
        return 1;
    }
    auto rules = db.GetGameRules();
    bool found = false;
    for (const auto& r : rules) {
        if (r.exeName == "test_game.exe") found = true;
    }
    if (!found) {
        std::cerr << "Falha: Regra de jogo nao encontrada no SQLite" << std::endl;
        return 1;
    }

    if (!db.RemoveGameRule("test_game.exe")) {
        std::cerr << "Falha ao remover regra de jogo no SQLite" << std::endl;
        return 1;
    }

    // 4. Testar logs e histórico de métricas
    if (!db.LogEvent("INFO", "Teste de auditoria SQLite")) {
        std::cerr << "Falha ao gravar log de auditoria" << std::endl;
        return 1;
    }
    if (!db.RecordMetric(15.5, 45.0, 55.0)) {
        std::cerr << "Falha ao gravar historico de metricas" << std::endl;
        return 1;
    }
    auto history = db.GetMetricsHistory(10);
    if (history.empty()) {
        std::cerr << "Falha: GetMetricsHistory retornou lista vazia" << std::endl;
        return 1;
    }
    if (history[0].cpuUsage < 15.0 || history[0].ramUsage < 40.0) {
        std::cerr << "Falha: Dados de metrica historica divergentes" << std::endl;
        return 1;
    }

    // 5. Testar consulta a Base de Conhecimento de Hardware Documentada
    auto specAmd = db.LookupHardwareSpec("AMD Ryzen 7 5800X 8-Core Processor");
    if (!specAmd.found) {
        std::cerr << "Falha: Ryzen 7 5800X nao encontrado na base documentada" << std::endl;
        return 1;
    }
    std::cout << "  [HW Knowledge] Encontrado: " << specAmd.model << " | TDP: " << specAmd.tdp << std::endl;

    auto specIntel = db.LookupHardwareSpec("Intel(R) Core(TM) i7-13700K CPU @ 3.40GHz");
    if (!specIntel.found) {
        std::cerr << "Falha: Core i7-13700K nao encontrado na base documentada" << std::endl;
        return 1;
    }
    std::cout << "  [HW Knowledge] Encontrado: " << specIntel.model << " | Base: " << specIntel.clockBase << std::endl;

    // 5b. Variantes NVIDIA (desktop x laptop), Arc, CPU mobile e filtro de categoria:
    // "RTX 4060" nao pode casar com "4060 Laptop GPU" nem com "4060 Ti".
    auto specNvidiaPlain = db.LookupHardwareSpec("NVIDIA GeForce RTX 4060", "GPU_");
    if (!specNvidiaPlain.found || specNvidiaPlain.model != "GeForce RTX 4060") {
        std::cerr << "Falha: RTX 4060 desktop deveria resolver para GeForce RTX 4060 (obteve: " << specNvidiaPlain.model << ")" << std::endl;
        return 1;
    }
    auto specNvidiaLaptop = db.LookupHardwareSpec("NVIDIA GeForce RTX 4060 Laptop GPU", "GPU_");
    if (!specNvidiaLaptop.found || specNvidiaLaptop.model != "GeForce RTX 4060 Laptop GPU") {
        std::cerr << "Falha: RTX 4060 Laptop deveria resolver para GeForce RTX 4060 Laptop GPU (obteve: " << specNvidiaLaptop.model << ")" << std::endl;
        return 1;
    }
    auto specArc = db.LookupHardwareSpec("Intel(R) Arc(TM) A770 Graphics", "GPU_");
    if (!specArc.found) {
        std::cerr << "Falha: Intel Arc A770 nao encontrado na base documentada" << std::endl;
        return 1;
    }
    auto specMobileAmd = db.LookupHardwareSpec("AMD Ryzen 7 5800H with Radeon Graphics", "CPU_");
    if (!specMobileAmd.found || specMobileAmd.model != "Ryzen 7 5800H") {
        std::cerr << "Falha: Ryzen 7 5800H mobile nao resolvido (obteve: " << specMobileAmd.model << ")" << std::endl;
        return 1;
    }
    auto specFilterGuard = db.LookupHardwareSpec("Intel(R) Core(TM) i7-12700K CPU @ 3.40GHz", "GPU_");
    if (specFilterGuard.found) {
        std::cerr << "Falha: modelo de CPU retornou em busca com filtro GPU_ (filtro de categoria quebrado)" << std::endl;
        return 1;
    }
    std::cout << "  [HW Knowledge] Variantes NVIDIA/Arc/mobile e filtro de categoria: OK" << std::endl;

    // 6. Testar limpeza de métricas
    if (!db.ClearMetricsHistory()) {
        std::cerr << "Falha ao limpar historico de metricas" << std::endl;
        return 1;
    }
    auto historyEmpty = db.GetMetricsHistory(10);
    if (!historyEmpty.empty()) {
        std::cerr << "Falha: Historico de metricas deveria estar vazio apos Clear" << std::endl;
        return 1;
    }

    db.Close();

    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    std::cout << "test_database: SUCESSO (SQLite nativo operacional, Schema v2, Historico e Base de Conhecimento validados)" << std::endl;
    return 0;
}
