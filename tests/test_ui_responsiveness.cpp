// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include <iostream>
#include <vector>
#include <cassert>
#include "ui/Theme.h"

struct Resolution {
    int w;
    int h;
    const char* label;
};

int main() {
    std::cout << "[TEST] Iniciando verificacao de Responsividade e Identidade Visual..." << std::endl;

    // 1. Validar tokens de design e identidade visual
    auto& theme = opitcore::ui::Theme::Instance();
    if (opitcore::ui::Theme::BgDark != RGB(18, 20, 24)) {
        std::cerr << "Falha: cor BgDark divergente da especificacao Dark Premium" << std::endl;
        return 1;
    }
    if (opitcore::ui::Theme::AccentCyan != RGB(0, 210, 255)) {
        std::cerr << "Falha: cor AccentCyan divergente do ciano tecnologico" << std::endl;
        return 1;
    }
    if (opitcore::ui::Theme::TextPrimary != RGB(242, 245, 248)) {
        std::cerr << "Falha: cor TextPrimary sem contraste suficiente" << std::endl;
        return 1;
    }

    // 2. Testar resoluções obrigatórias de acordo com o requisito do projeto:
    // 800x600, 1024x768, 1280x720, 1366x768, 1920x1080
    std::vector<Resolution> resolutions = {
        { 800,  600,  "800x600 (Minimo suportado)" },
        { 1024, 768,  "1024x768 (XGA)" },
        { 1280, 720,  "1280x720 (HD 720p)" },
        { 1366, 768,  "1366x768 (Notebooks comuns)" },
        { 1920, 1080, "1920x1080 (Full HD)" }
    };

    for (const auto& res : resolutions) {
        std::cout << "  Testando geometria para " << res.label << "... ";

        // Validar limites minimos de rastreamento
        if (res.w < 800 || res.h < 600) {
            std::cerr << "Falha: resolucao abaixo do minimo suportado (800x600)" << std::endl;
            return 1;
        }

        // Simular cálculo de layout responsivo da MainWindow
        int sidebarW = (res.w < 900) ? 190 : 230;
        int cx = sidebarW + 24;
        int cy = 20;
        int cw = res.w - cx - 24;
        int ch = res.h - cy - 20;

        if (cw <= 0 || ch <= 0) {
            std::cerr << "Falha: area de conteudo com dimensao negativa em " << res.label << std::endl;
            return 1;
        }

        // Validação da distribuição dos cartões do Dashboard
        int cardW = (cw >= 640) ? (cw - 16) / 2 : cw;
        int col2X = (cw >= 640) ? (cx + cardW + 16) : cx;

        if (cardW <= 0) {
            std::cerr << "Falha: largura de cartao invalida em " << res.label << std::endl;
            return 1;
        }
        if (cw >= 640 && (col2X + cardW > res.w)) {
            std::cerr << "Falha: estouro horizontal de colunas em " << res.label << std::endl;
            return 1;
        }

        // Validação do dimensionamento do ListView em abas de dados (Processos, Jogos, Logs)
        int listW = cw;
        int listH = ch - 110;
        if (listW < 300 || listH < 150) {
            std::cerr << "Falha: altura de lista insuficiente em " << res.label << std::endl;
            return 1;
        }

        // Validação do Painel Unificado para telas de baixa altura
        int contentHeightUnified = 840;
        int maxAvailableH = res.h - 40;
        int winH = std::min(contentHeightUnified + 40, maxAvailableH);
        bool requiresScroll = (winH < contentHeightUnified + 30);

        if (res.h <= 768 && !requiresScroll) {
            std::cerr << "Falha: rolagem vertical deveria estar habilitada para alturas <= 768p" << std::endl;
            return 1;
        }

        std::cout << "[OK: cw=" << cw << ", ch=" << ch << ", cardW=" << cardW << "]" << std::endl;
    }

    std::cout << "[SUCESSO] Todos os testes de Responsividade e UI/UX passaram com exito!" << std::endl;
    return 0;
}
