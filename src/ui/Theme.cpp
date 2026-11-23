// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "Theme.h"
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace opitcore {
namespace ui {

Theme& Theme::Instance() {
    static Theme instance;
    if (!instance.m_initialized) {
        instance.Init();
    }
    return instance;
}

void Theme::Init() {
    if (m_initialized) return;

    // Criar Brushes Dark Mode
    brushBgDark          = CreateSolidBrush(BgDark);
    brushSidebar         = CreateSolidBrush(SidebarBg);
    brushSidebarSelected = CreateSolidBrush(SidebarSelected);
    brushSidebarHover    = CreateSolidBrush(SidebarHover);
    brushBgCard          = CreateSolidBrush(BgCard);
    brushBgCardHover     = CreateSolidBrush(BgCardHover);
    brushBgInput         = CreateSolidBrush(BgInput);
    brushBorderSubtle    = CreateSolidBrush(BorderSubtle);
    brushBorderCard      = CreateSolidBrush(BorderCard);
    brushAccentCyan      = CreateSolidBrush(AccentCyan);
    brushStatusSuccess   = CreateSolidBrush(StatusSuccess);

    // Criar Fontes Segoe UI modernas e nítidas com ClearType
    fontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontSection = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontBody = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontBodyBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontSmallBold = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    fontMono = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FIXED_PITCH | FF_MODERN, L"Consolas");

    m_initialized = true;
}

void Theme::Cleanup() {
    if (!m_initialized) return;

    auto safeDeleteBrush = [](HBRUSH& h) { if (h) { DeleteObject(h); h = nullptr; } };
    auto safeDeleteFont  = [](HFONT& h)  { if (h) { DeleteObject(h); h = nullptr; } };

    safeDeleteBrush(brushBgDark);
    safeDeleteBrush(brushSidebar);
    safeDeleteBrush(brushSidebarSelected);
    safeDeleteBrush(brushSidebarHover);
    safeDeleteBrush(brushBgCard);
    safeDeleteBrush(brushBgCardHover);
    safeDeleteBrush(brushBgInput);
    safeDeleteBrush(brushBorderSubtle);
    safeDeleteBrush(brushBorderCard);
    safeDeleteBrush(brushAccentCyan);
    safeDeleteBrush(brushStatusSuccess);

    safeDeleteFont(fontTitle);
    safeDeleteFont(fontSection);
    safeDeleteFont(fontBody);
    safeDeleteFont(fontBodyBold);
    safeDeleteFont(fontSmall);
    safeDeleteFont(fontSmallBold);
    safeDeleteFont(fontMono);

    m_initialized = false;
}

void Theme::EnableImmersiveDarkMode(HWND hwnd) {
    if (!hwnd) return;

    // Windows 10 build 19041+ e Windows 11 usam o atributo DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark)))) {
        // Fallback para builds anteriores do Windows 10 (1903/1909 usavam atributo 19)
        DwmSetWindowAttribute(hwnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &dark, sizeof(dark));
    }
}

void Theme::ApplyDarkExplorerTheme(HWND hwnd) {
    if (!hwnd) return;
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

INT_PTR Theme::HandleCtlColorStatic(HDC hdc, HWND hwnd, COLORREF textCol, COLORREF bgCol) {
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, bgCol);
    SetTextColor(hdc, textCol);
    if (bgCol == BgDark) return (INT_PTR)Instance().brushBgDark;
    if (bgCol == BgCard) return (INT_PTR)Instance().brushBgCard;
    if (bgCol == SidebarBg) return (INT_PTR)Instance().brushSidebar;
    return (INT_PTR)Instance().brushBgDark;
}

INT_PTR Theme::HandleCtlColorEdit(HDC hdc, HWND hwnd) {
    SetBkColor(hdc, BgInput);
    SetTextColor(hdc, TextPrimary);
    return (INT_PTR)Instance().brushBgInput;
}

INT_PTR Theme::HandleCtlColorListBox(HDC hdc, HWND hwnd) {
    SetBkColor(hdc, BgInput);
    SetTextColor(hdc, TextPrimary);
    return (INT_PTR)Instance().brushBgInput;
}

INT_PTR Theme::HandleCtlColorBtn(HDC hdc, HWND hwnd) {
    SetBkColor(hdc, BgCard);
    SetTextColor(hdc, TextPrimary);
    return (INT_PTR)Instance().brushBgCard;
}

void Theme::DrawModernCard(HDC hdc, const RECT& rc, const wchar_t* title,
                          COLORREF accentColor, bool drawBorder) {
    auto& t = Instance();
    
    // Fundo do cartão
    FillRect(hdc, &rc, t.brushBgCard);

    // Borda sutil de profundidade
    if (drawBorder) {
        FrameRect(hdc, &rc, t.brushBorderCard);
    }

    // Linha/pílula superior de destaque visual tecnológico (2px de altura)
    if (accentColor != 0) {
        RECT rcAccent = { rc.left + 8, rc.top, rc.right - 8, rc.top + 2 };
        HBRUSH hAccentBrush = CreateSolidBrush(accentColor);
        FillRect(hdc, &rcAccent, hAccentBrush);
        DeleteObject(hAccentBrush);
    }

    // Título do cartão (se fornecido)
    if (title && *title) {
        HFONT hOldFont = (HFONT)SelectObject(hdc, t.fontSmallBold);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, TextSecondary);
        RECT rcTitle = { rc.left + 16, rc.top + 10, rc.right - 16, rc.top + 26 };
        DrawTextW(hdc, title, -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);
    }
}

void Theme::DrawStatusPill(HDC hdc, const RECT& rc, const wchar_t* text, COLORREF dotColor) {
    auto& t = Instance();
    
    // Fundo da pílula
    HBRUSH hBorderBrush = CreateSolidBrush(BorderSubtle);
    FillRect(hdc, &rc, t.brushSidebarHover);
    FrameRect(hdc, &rc, hBorderBrush);
    DeleteObject(hBorderBrush);

    // Indicador circular (dot)
    int dotSize = 8;
    int dotX = rc.left + 10;
    int dotY = rc.top + (rc.bottom - rc.top - dotSize) / 2;
    HBRUSH hDotBrush = CreateSolidBrush(dotColor);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hDotBrush);
    HPEN hPen = CreatePen(PS_SOLID, 1, dotColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    Ellipse(hdc, dotX, dotY, dotX + dotSize, dotY + dotSize);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hDotBrush);
    DeleteObject(hPen);

    // Texto do status
    if (text && *text) {
        HFONT hOldFont = (HFONT)SelectObject(hdc, t.fontSmall);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, TextPrimary);
        RECT rcText = { dotX + dotSize + 8, rc.top, rc.right - 6, rc.bottom };
        DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);
    }
}

} // namespace ui
} // namespace opitcore
