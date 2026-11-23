// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

// Prova das bandas de plano derivadas dos limites Min/Max salvos na janela "Configuracao Avancada:
// CPU Min/Max", nos tres perfis declarados pela usuaria:
//   desktop / servidor (sem bateria), na tomada: 0-35 / 36-78 / 79-100
//   notebook, na tomada:                         0-25 / 26-50 / 51-75
//   notebook, na bateria:                        0-20 / 21-35 / 36-60
// Nada aqui toca no sistema: o que se confere e a tabela semeada pela classe de hardware e a funcao
// pura que escolhe o plano a partir dela.
#include "adaptive/AdaptiveEngine.h"
#include "core/ModuleAdvancedSettings.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace opitcore;

namespace {

int g_failures = 0;

struct Case {
    double usage;
    int expected;
    const char* label;
};

struct Profile {
    const char* name;
    AdaptiveEngine::PlanBands bands;
    double expectEcoMax;
    double expectBalancedMin;
    double expectHighMin;
    std::vector<Case> cases;
};

const char* StageName(int stage) {
    switch (stage) {
    case 0: return "Economico";
    case 1: return "Balanceado";
    case 2: return "Alto Desempenho";
    default: return "DESCONHECIDO";
    }
}

// A banda sai dos mesmos objetos que a janela de CPU Min/Max escreve: assim a prova cai junto se
// alguem mudar o perfil por um caminho e o orquestrador por outro.
AdaptiveEngine::PlanBands BandsOf(const AdvCpuMinMaxConfig& cfg, bool dc) {
    return AdaptiveEngine::BandsFromLimits(cfg.balancedPlan.Min(dc), cfg.highPerfPlan.Min(dc));
}

void CheckLimit(const char* what, uint32_t got, uint32_t expected) {
    if (got == expected) {
        std::cout << "[PASS] " << what << " = " << got << "%" << std::endl;
    } else {
        std::cout << "[FAIL] " << what << " = " << got << "% (esperado " << expected << "%)" << std::endl;
        g_failures++;
    }
}

} // namespace

int main() {
    AdvCpuMinMaxConfig desktop;
    desktop.ApplyStandardProfile(false);
    AdvCpuMinMaxConfig notebook;
    notebook.ApplyStandardProfile(true);

    // 1. Os limites gravados em cada plano sao exatamente os numeros declarados pela usuaria.
    std::cout << "-- perfil desktop/servidor (sem bateria) --" << std::endl;
    CheckLimit("economico AC min/max", desktop.ecoPlan.maxPercentAC, 35);
    CheckLimit("economico AC min", desktop.ecoPlan.minPercentAC, 0);
    CheckLimit("balanceado AC min", desktop.balancedPlan.minPercentAC, 36);
    CheckLimit("balanceado AC max", desktop.balancedPlan.maxPercentAC, 78);
    CheckLimit("alto desempenho AC min", desktop.highPerfPlan.minPercentAC, 79);
    CheckLimit("alto desempenho AC max", desktop.highPerfPlan.maxPercentAC, 100);

    std::cout << "-- perfil notebook (bateria) --" << std::endl;
    CheckLimit("economico tomada min/max", notebook.ecoPlan.maxPercentAC, 25);
    CheckLimit("balanceado tomada min", notebook.balancedPlan.minPercentAC, 26);
    CheckLimit("balanceado tomada max", notebook.balancedPlan.maxPercentAC, 50);
    CheckLimit("alto desempenho tomada min", notebook.highPerfPlan.minPercentAC, 51);
    CheckLimit("alto desempenho tomada max", notebook.highPerfPlan.maxPercentAC, 75);
    CheckLimit("economico bateria max", notebook.ecoPlan.maxPercentDC, 20);
    CheckLimit("balanceado bateria min", notebook.balancedPlan.minPercentDC, 21);
    CheckLimit("balanceado bateria max", notebook.balancedPlan.maxPercentDC, 35);
    CheckLimit("alto desempenho bateria min", notebook.highPerfPlan.minPercentDC, 36);
    CheckLimit("alto desempenho bateria max", notebook.highPerfPlan.maxPercentDC, 60);

    // 2. As bandas que o orquestrador usa saem desses mesmos limites, por fonte de energia.
    std::vector<Profile> profiles = {
        {"desktop/servidor na tomada", BandsOf(desktop, false), 35, 36, 79,
         {{0.0, 0, "piso da banda economica"}, {20.0, 0, "meio da banda economica"},
          {35.0, 0, "topo inclusive de 0-35"}, {35.9, 0, "ainda dentro de 0-35"},
          {36.0, 1, "inicio inclusive de 36-78"}, {57.0, 1, "meio da banda balanceada"},
          {78.0, 1, "topo inclusive de 36-78"}, {78.9, 1, "ainda dentro de 36-78"},
          {79.0, 2, "inicio inclusive de 79-100"}, {92.0, 2, "meio da banda de desempenho"},
          {100.0, 2, "teto de uso"}}},
        {"notebook na tomada", BandsOf(notebook, false), 25, 26, 51,
         {{0.0, 0, "piso da banda economica"}, {25.0, 0, "topo inclusive de 0-25"},
          {25.9, 0, "ainda dentro de 0-25"}, {26.0, 1, "inicio inclusive de 26-50"},
          {38.0, 1, "meio da banda balanceada"}, {50.0, 1, "topo inclusive de 26-50"},
          {50.9, 1, "ainda dentro de 26-50"}, {51.0, 2, "inicio inclusive de 51-75"},
          {100.0, 2, "teto de uso"}}},
        {"notebook na bateria", BandsOf(notebook, true), 20, 21, 36,
         {{0.0, 0, "piso da banda economica"}, {20.0, 0, "topo inclusive de 0-20"},
          {20.9, 0, "ainda dentro de 0-20"}, {21.0, 1, "inicio inclusive de 21-35"},
          {28.0, 1, "meio da banda balanceada"}, {35.0, 1, "topo inclusive de 21-35"},
          {35.9, 1, "ainda dentro de 21-35"}, {36.0, 2, "inicio inclusive de 36-60"},
          {100.0, 2, "teto de uso"}}},
    };

    for (const auto& p : profiles) {
        std::cout << "-- bandas vigentes: " << p.name << " --" << std::endl;
        const bool tableOk = p.bands.ecoMax == p.expectEcoMax && p.bands.balancedMin == p.expectBalancedMin &&
                             p.bands.highMin == p.expectHighMin && p.bands.balancedMax() == p.expectHighMin - 1;
        std::cout << (tableOk ? "[PASS] " : "[FAIL] ") << p.name << ": bandas vigentes 0-" << (int)p.bands.ecoMax <<
                  "% / " << (int)p.bands.balancedMin << "-" << (int)p.bands.balancedMax() << "% / " <<
                  (int)p.bands.highMin << "-100%";
        if (tableOk) {
            std::cout << std::endl;
        } else {
            std::cout << " (esperado 0-" << (int)p.expectEcoMax << " / " << (int)p.expectBalancedMin << "-" <<
                      (int)(p.expectHighMin - 1) << " / " << (int)p.expectHighMin << "-100%)";
            std::cout << std::endl;
            g_failures++;
        }

        int previousStage = -1;
        for (const auto& c : p.cases) {
            const int got = AdaptiveEngine::SchemeStageForCpuUsage(c.usage, p.bands);
            const bool ok = (got == c.expected);
            std::cout << (ok ? "[PASS] " : "[FAIL] ") << p.name << ": uso " << c.usage << "% -> plano " << StageName(got)
                      << " (esperado " << StageName(c.expected) << ") - " << c.label << std::endl;
            if (!ok) g_failures++;
            if (got < previousStage) {
                std::cout << "[FAIL] " << p.name << ": uso " << c.usage << "% devolveu plano mais baixo que o anterior." << std::endl;
                g_failures++;
            }
            previousStage = got;
        }

        // Varredura total: nenhum uso pode ficar sem plano, e o topo tem que ser desempenho.
        bool sweepClean = true;
        for (double u = 0.0; u <= 100.0; u += 0.5) {
            const int got = AdaptiveEngine::SchemeStageForCpuUsage(u, p.bands);
            if (got < 0 || got > 2) {
                std::cout << "[FAIL] " << p.name << ": uso " << u << "% nao pertence a nenhuma banda." << std::endl;
                g_failures++;
                sweepClean = false;
            }
        }
        if (sweepClean) {
            std::cout << "[PASS] " << p.name << ": varredura 0-100% em passos de 0,5% cobriu todo uso." << std::endl;
        }
    }

    // 3. Configuracao inconsistente (edicao manual) nao pode inverter nem abrir buraco nas faixas.
    const AdaptiveEngine::PlanBands invertidas = AdaptiveEngine::BandsFromLimits(90, 10);
    const bool ordemOk = invertidas.balancedMin < invertidas.highMin && invertidas.ecoMax >= 0.0;
    std::cout << (ordemOk ? "[PASS] " : "[FAIL] ") << "pisos invertidos (balanced 90, high 10) viram bandas ordenadas: "
              << (int)invertidas.ecoMax << " / " << (int)invertidas.balancedMin << " / " << (int)invertidas.highMin << std::endl;
    if (!ordemOk) g_failures++;

    const AdaptiveEngine::PlanBands zeradas = AdaptiveEngine::BandsFromLimits(0, 0);
    const bool cobreTudo = AdaptiveEngine::SchemeStageForCpuUsage(0.0, zeradas) == 0 &&
                           AdaptiveEngine::SchemeStageForCpuUsage(100.0, zeradas) == 2;
    std::cout << (cobreTudo ? "[PASS] " : "[FAIL] ") << "pisos zerados (0,0) mantem 0% no economico e 100% no desempenho." << std::endl;
    if (!cobreTudo) g_failures++;

    std::cout << (g_failures == 0 ? "BANDAS DERIVADAS DOS LIMITES MIN/MAX: VERIFICADAS (desktop, notebook tomada, notebook bateria)"
                                  : "BANDAS DERIVADAS DOS LIMITES MIN/MAX: FALHAS DETECTADAS")
              << " (" << g_failures << " falha(s))" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
