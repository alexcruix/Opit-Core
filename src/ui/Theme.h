#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>

namespace opitcore {
namespace ui {

/**
 * Tokens visuais, paleta de cores Dark Premium, fontes Segoe UI
 * e utilitários de estilização compatíveis com Fluent Design e Windows 10/11.
 */
class Theme {
public:
    // Paleta de Cores Dark Premium (Grafite, Carvão e Ciano Tecnológico)
    static constexpr COLORREF BgDark           = RGB(18, 20, 24);    // #121418 - Fundo principal da janela
    static constexpr COLORREF SidebarBg        = RGB(14, 16, 20);    // #0E1014 - Fundo da navegação lateral
    static constexpr COLORREF SidebarSelected  = RGB(28, 34, 46);    // #1C222E - Fundo do item de menu ativo
    static constexpr COLORREF SidebarHover     = RGB(24, 28, 36);    // #181C24 - Fundo do item em hover
    static constexpr COLORREF BgCard           = RGB(25, 29, 36);    // #191D24 - Superfície de cartões/painéis
    static constexpr COLORREF BgCardHover      = RGB(32, 38, 48);    // #202630 - Hover sobre cartões
    static constexpr COLORREF BgInput          = RGB(20, 23, 29);    // #14171D - Fundo de campos de texto e listas
    static constexpr COLORREF BorderSubtle     = RGB(42, 48, 60);    // #2A303C - Bordas discretas
    static constexpr COLORREF BorderCard       = RGB(50, 58, 72);    // #323A48 - Contorno de cartões
    
    // Cores de Acento e Destaque
    static constexpr COLORREF AccentCyan       = RGB(0, 210, 255);   // #00D2FF - Ciano tecnológico primário
    static constexpr COLORREF AccentBlue       = RGB(0, 136, 255);   // #0088FF - Azul elétrico vibrante
    static constexpr COLORREF AccentCyanDark   = RGB(0, 140, 180);   // Ciano atenuado
    
    // Tipografia e Contraste de Texto
    static constexpr COLORREF TextPrimary      = RGB(242, 245, 248); // #F2F5F8 - Branco/prata de alto contraste
    static constexpr COLORREF TextSecondary    = RGB(160, 168, 182); // #A0A8B6 - Cinza legível para rótulos
    static constexpr COLORREF TextMuted        = RGB(110, 118, 132); // #6E7684 - Cinza suave para notas secundárias
    
    // Estados Semânticos
    static constexpr COLORREF StatusSuccess    = RGB(16, 185, 129);  // #10B981 - Verde ativo/otimizado
    static constexpr COLORREF StatusWarning    = RGB(245, 158, 11);  // #F59E0B - Âmbar atenção
    static constexpr COLORREF StatusError      = RGB(239, 68, 68);   // #EF4444 - Vermelho erro/crítico
    static constexpr COLORREF StatusDisabled   = RGB(90, 96, 108);   // #5A606C - Estado inativo/indisponível

    static Theme& Instance();

    void Init();
    void Cleanup();

    // Brushes cacheados
    HBRUSH brushBgDark{nullptr};
    HBRUSH brushSidebar{nullptr};
    HBRUSH brushSidebarSelected{nullptr};
    HBRUSH brushSidebarHover{nullptr};
    HBRUSH brushBgCard{nullptr};
    HBRUSH brushBgCardHover{nullptr};
    HBRUSH brushBgInput{nullptr};
    HBRUSH brushBorderSubtle{nullptr};
    HBRUSH brushBorderCard{nullptr};
    HBRUSH brushAccentCyan{nullptr};
    HBRUSH brushStatusSuccess{nullptr};

    // Fontes Segoe UI modernas com ClearType
    HFONT fontTitle{nullptr};      // 20pt Bold - Títulos principais
    HFONT fontSection{nullptr};    // 16pt Bold - Subtítulos e seções
    HFONT fontBody{nullptr};       // 14pt Normal - Texto corrente
    HFONT fontBodyBold{nullptr};   // 14pt Bold - Destaques e botões
    HFONT fontSmall{nullptr};      // 12pt Normal - Badges e notas
    HFONT fontSmallBold{nullptr};  // 12pt Bold - Rótulos de cartões
    HFONT fontMono{nullptr};       // 13pt Consolas - Telemetria e logs

    // Métodos estáticos auxiliares
    static void EnableImmersiveDarkMode(HWND hwnd);
    static void ApplyDarkExplorerTheme(HWND hwnd);
    
    // Roteadores de cores para mensagens Win32
    static INT_PTR HandleCtlColorStatic(HDC hdc, HWND hwnd, COLORREF textCol = TextPrimary, COLORREF bgCol = BgDark);
    static INT_PTR HandleCtlColorEdit(HDC hdc, HWND hwnd);
    static INT_PTR HandleCtlColorListBox(HDC hdc, HWND hwnd);
    static INT_PTR HandleCtlColorBtn(HDC hdc, HWND hwnd);

    // Utilitários de desenho moderno
    static void DrawModernCard(HDC hdc, const RECT& rc, const wchar_t* title = nullptr,
                               COLORREF accentColor = AccentCyan, bool drawBorder = true);
    static void DrawStatusPill(HDC hdc, const RECT& rc, const wchar_t* text, COLORREF dotColor);

private:
    Theme() = default;
    ~Theme() { Cleanup(); }
    bool m_initialized{false};
};

} // namespace ui
} // namespace opitcore
