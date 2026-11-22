#include "FanCurveEditorDialog.h"
#include "Theme.h"
#include "fans/FanControlManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "utils/StringUtils.h"
#include <commctrl.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace opitcore {

static const UINT ID_PRESET_SILENT     = 2101;
static const UINT ID_PRESET_BALANCED   = 2102;
static const UINT ID_PRESET_AGGRESSIVE = 2103;
static const UINT ID_BTN_RESET_CURVE   = 2104;
static const UINT ID_BTN_OK            = 2105;
static const UINT ID_BTN_APPLY         = 2106;
static const UINT ID_BTN_CANCEL        = 2107;
static const UINT ID_TIMER_LIVE_TEMP   = 2108;
static const UINT ID_COMBO_TARGET_DEV  = 2109;

static const UINT ID_MENU_REMOVE_POINT = 2201;
static const UINT ID_MENU_ADD_POINT    = 2202;

static std::string s_targetDeviceId;
static FanCurveConfig s_workingCurve;
static int s_draggedPointIndex = -1;
static int s_hoveredPointIndex = -1;
static double s_rightClickTemp = 0.0;
static uint32_t s_rightClickSpeed = 0;
static HWND s_hDlg = nullptr;
static HWND s_comboTargetDev = nullptr;
static HWND s_editHyst = nullptr;
static HWND s_editEmerg = nullptr;
static HWND s_lblApplyFeedback = nullptr;

// Coordenadas da área gráfica na área cliente
static const RECT s_rcGraph = { 55, 75, 665, 420 };
// Coordenadas do painel lateral de resumo de pontos
static const RECT s_rcSidePanel = { 680, 75, 895, 420 };

static inline int TempToX(double tempC) {
    double t = std::clamp(tempC, 20.0, 100.0);
    double ratio = (t - 20.0) / 80.0;
    return s_rcGraph.left + static_cast<int>(ratio * (s_rcGraph.right - s_rcGraph.left));
}

static inline double XToTemp(int x) {
    int clX = std::clamp(x, (int)s_rcGraph.left, (int)s_rcGraph.right);
    double ratio = static_cast<double>(clX - s_rcGraph.left) / (s_rcGraph.right - s_rcGraph.left);
    return std::round(20.0 + ratio * 80.0);
}

static inline int SpeedToY(uint32_t speed) {
    uint32_t s = std::clamp(speed, 0u, 100u);
    double ratio = static_cast<double>(s) / 100.0;
    return s_rcGraph.bottom - static_cast<int>(ratio * (s_rcGraph.bottom - s_rcGraph.top));
}

static inline uint32_t YToSpeed(int y) {
    int clY = std::clamp(y, (int)s_rcGraph.top, (int)s_rcGraph.bottom);
    double ratio = static_cast<double>(s_rcGraph.bottom - clY) / (s_rcGraph.bottom - s_rcGraph.top);
    return static_cast<uint32_t>(std::clamp(std::round(ratio * 100.0), 0.0, 100.0));
}

static uint32_t InterpolateCurveSpeed(const FanCurveConfig& curve, double currentTempC) {
    if (curve.points.empty()) return 50;
    if (currentTempC <= curve.points.front().tempC) return curve.points.front().speedPercent;
    if (currentTempC >= curve.points.back().tempC) return curve.points.back().speedPercent;

    for (size_t i = 0; i < curve.points.size() - 1; ++i) {
        const auto& p1 = curve.points[i];
        const auto& p2 = curve.points[i + 1];
        if (currentTempC >= p1.tempC && currentTempC <= p2.tempC) {
            double range = p2.tempC - p1.tempC;
            if (range <= 0.001) return p2.speedPercent;
            double factor = (currentTempC - p1.tempC) / range;
            return static_cast<uint32_t>(std::clamp(p1.speedPercent + factor * (static_cast<double>(p2.speedPercent) - p1.speedPercent), 0.0, 100.0));
        }
    }
    return 50;
}

static int HitTestPoint(int x, int y, int thresholdPx = 14) {
    for (size_t i = 0; i < s_workingCurve.points.size(); ++i) {
        int ptX = TempToX(s_workingCurve.points[i].tempC);
        int ptY = SpeedToY(s_workingCurve.points[i].speedPercent);
        int dx = x - ptX;
        int dy = y - ptY;
        if (dx * dx + dy * dy <= thresholdPx * thresholdPx) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static bool SaveWorkingCurve() {
    wchar_t bh[32]{}, be[32]{};
    GetWindowTextW(s_editHyst, bh, 32);
    GetWindowTextW(s_editEmerg, be, 32);

    int hVal = _wtoi(bh);
    double eVal = _wtof(be);
    if (hVal < 1) hVal = 1;
    if (hVal > 10) hVal = 10;
    if (eVal < 70.0) eVal = 70.0;
    if (eVal > 105.0) eVal = 105.0;

    s_workingCurve.hysteresisC = hVal;
    s_workingCurve.emergencyTempC = eVal;
    s_workingCurve.Validate();

    auto& mgr = FanControlManager::Instance();
    mgr.SetDeviceCurve(s_targetDeviceId, s_workingCurve);
    mgr.SetDeviceMode(s_targetDeviceId, FanControlMode::CustomCurve);
    mgr.SaveConfigurations();
    return true;
}

static void DrawGraph(HDC hdc, HWND hwnd) {
    auto& theme = ui::Theme::Instance();

    // 1. Fundo do gráfico (Dark Card)
    HBRUSH bgChartBrush = CreateSolidBrush(RGB(22, 26, 34));
    FillRect(hdc, &s_rcGraph, bgChartBrush);
    DeleteObject(bgChartBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(55, 65, 82));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, s_rcGraph.left, s_rcGraph.top, s_rcGraph.right, s_rcGraph.bottom);

    // 2. Linhas de grade (Grid)
    HPEN gridPen = CreatePen(PS_DOT, 1, RGB(38, 46, 60));
    SelectObject(hdc, gridPen);

    HFONT oldFont = (HFONT)SelectObject(hdc, theme.fontSmall);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(140, 150, 168));

    // Grade Vertical (Temperaturas: 20°C a 100°C)
    for (int t = 20; t <= 100; t += 10) {
        int gx = TempToX(t);
        MoveToEx(hdc, gx, s_rcGraph.top, nullptr);
        LineTo(hdc, gx, s_rcGraph.bottom);

        std::wstring lbl = std::to_wstring(t) + L"°C";
        RECT rcLbl = { gx - 20, s_rcGraph.bottom + 4, gx + 20, s_rcGraph.bottom + 20 };
        DrawTextW(hdc, lbl.c_str(), -1, &rcLbl, DT_CENTER | DT_SINGLELINE);
    }

    // Grade Horizontal (Velocidade %: 0% a 100%)
    for (int s = 0; s <= 100; s += 20) {
        int gy = SpeedToY(s);
        MoveToEx(hdc, s_rcGraph.left, gy, nullptr);
        LineTo(hdc, s_rcGraph.right, gy);

        std::wstring lbl = std::to_wstring(s) + L"%";
        RECT rcLbl = { s_rcGraph.left - 42, gy - 7, s_rcGraph.left - 6, gy + 9 };
        DrawTextW(hdc, lbl.c_str(), -1, &rcLbl, DT_RIGHT | DT_SINGLELINE);
    }

    DeleteObject(gridPen);

    // 3. Área Preenchida sob a curva
    if (s_workingCurve.points.size() >= 2) {
        std::vector<POINT> polyPts;
        polyPts.push_back({ (LONG)TempToX(s_workingCurve.points.front().tempC), (LONG)s_rcGraph.bottom });
        for (const auto& p : s_workingCurve.points) {
            polyPts.push_back({ (LONG)TempToX(p.tempC), (LONG)SpeedToY(p.speedPercent) });
        }
        polyPts.push_back({ (LONG)TempToX(s_workingCurve.points.back().tempC), (LONG)s_rcGraph.bottom });

        HBRUSH fillBrush = CreateSolidBrush(RGB(15, 45, 65));
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        SelectObject(hdc, fillBrush);
        SelectObject(hdc, nullPen);
        Polygon(hdc, polyPts.data(), static_cast<int>(polyPts.size()));
        DeleteObject(fillBrush);
        DeleteObject(nullPen);
    }

    // 4. Linha da Curva em Ciano Neon
    HPEN curvePen = CreatePen(PS_SOLID, 3, RGB(0, 210, 255));
    SelectObject(hdc, curvePen);

    if (!s_workingCurve.points.empty()) {
        MoveToEx(hdc, TempToX(s_workingCurve.points[0].tempC), SpeedToY(s_workingCurve.points[0].speedPercent), nullptr);
        for (size_t i = 1; i < s_workingCurve.points.size(); ++i) {
            LineTo(hdc, TempToX(s_workingCurve.points[i].tempC), SpeedToY(s_workingCurve.points[i].speedPercent));
        }
    }
    DeleteObject(curvePen);

    // 5. Linha indicadora de Temperatura em Tempo Real
    FanDeviceInfo devInfo;
    if (FanControlManager::Instance().GetDeviceInfo(s_targetDeviceId, devInfo)) {
        if (devInfo.latestReading.hasTemperature && devInfo.latestReading.temperatureC >= 20.0) {
            double curT = devInfo.latestReading.temperatureC;
            int curX = TempToX(curT);

            HPEN livePen = CreatePen(PS_SOLID, 2, RGB(16, 185, 129));
            SelectObject(hdc, livePen);
            MoveToEx(hdc, curX, s_rcGraph.top, nullptr);
            LineTo(hdc, curX, s_rcGraph.bottom);
            DeleteObject(livePen);

            uint32_t liveSpeed = InterpolateCurveSpeed(s_workingCurve, curT);
            int liveY = SpeedToY(liveSpeed);

            HBRUSH liveDotBrush = CreateSolidBrush(RGB(16, 185, 129));
            HPEN liveDotPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(hdc, liveDotBrush);
            SelectObject(hdc, liveDotPen);
            Ellipse(hdc, curX - 6, liveY - 6, curX + 6, liveY + 6);
            DeleteObject(liveDotBrush);
            DeleteObject(liveDotPen);

            std::wstring curTxt = L"● Temp Atual: " + std::to_wstring(static_cast<int>(curT)) + L"°C (" + std::to_wstring(liveSpeed) + L"%)";
            SetTextColor(hdc, RGB(16, 185, 129));
            RECT rcCur = { curX - 50, s_rcGraph.top - 18, curX + 120, s_rcGraph.top };
            DrawTextW(hdc, curTxt.c_str(), -1, &rcCur, DT_LEFT | DT_SINGLELINE);
        }
    }

    // 6. Pontos Interativos (Bolinhas)
    HBRUSH normalBrush = CreateSolidBrush(RGB(0, 210, 255));
    HBRUSH hoverBrush  = CreateSolidBrush(RGB(255, 255, 255));
    HPEN ptPen         = CreatePen(PS_SOLID, 2, RGB(18, 20, 24));

    for (size_t i = 0; i < s_workingCurve.points.size(); ++i) {
        int px = TempToX(s_workingCurve.points[i].tempC);
        int py = SpeedToY(s_workingCurve.points[i].speedPercent);
        bool isHover = (static_cast<int>(i) == s_hoveredPointIndex || static_cast<int>(i) == s_draggedPointIndex);
        int r = isHover ? 8 : 6;

        SelectObject(hdc, isHover ? hoverBrush : normalBrush);
        SelectObject(hdc, ptPen);
        Ellipse(hdc, px - r, py - r, px + r, py + r);

        if (isHover) {
            std::wstring badge = std::to_wstring(static_cast<int>(s_workingCurve.points[i].tempC)) + L"°C : " +
                                 std::to_wstring(s_workingCurve.points[i].speedPercent) + L"%";
            RECT rcBadge = { px - 40, py - 30, px + 40, py - 10 };
            HBRUSH badgeBg = CreateSolidBrush(RGB(10, 14, 20));
            HPEN badgeBorder = CreatePen(PS_SOLID, 1, RGB(0, 210, 255));
            SelectObject(hdc, badgeBg);
            SelectObject(hdc, badgeBorder);
            RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 6, 6);
            DeleteObject(badgeBg);
            DeleteObject(badgeBorder);

            SetTextColor(hdc, RGB(0, 210, 255));
            SelectObject(hdc, theme.fontSmallBold);
            DrawTextW(hdc, badge.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, theme.fontSmall);
        }
    }

    DeleteObject(normalBrush);
    DeleteObject(hoverBrush);
    DeleteObject(ptPen);
    DeleteObject(borderPen);

    // Rótulo do Eixo X
    SetTextColor(hdc, RGB(180, 190, 210));
    SelectObject(hdc, theme.fontSmallBold);
    RECT rcXTitle = { s_rcGraph.left, s_rcGraph.bottom + 22, s_rcGraph.right, s_rcGraph.bottom + 38 };
    DrawTextW(hdc, L"Temperatura do Hardware (°C)", -1, &rcXTitle, DT_CENTER | DT_SINGLELINE);

    // =========================================================================
    // 7. PAINEL LATERAL DIREITO: RESUMO E COORDENADAS DOS PONTOS (SEM NADA ESCONDIDO)
    // =========================================================================
    HBRUSH sideBg = CreateSolidBrush(RGB(22, 26, 34));
    FillRect(hdc, &s_rcSidePanel, sideBg);
    DeleteObject(sideBg);

    HPEN sideBorder = CreatePen(PS_SOLID, 1, RGB(55, 65, 82));
    SelectObject(hdc, sideBorder);
    Rectangle(hdc, s_rcSidePanel.left, s_rcSidePanel.top, s_rcSidePanel.right, s_rcSidePanel.bottom);
    DeleteObject(sideBorder);

    // Cabeçalho do Painel Lateral
    RECT rcSideTitle = { s_rcSidePanel.left + 12, s_rcSidePanel.top + 10, s_rcSidePanel.right - 12, s_rcSidePanel.top + 30 };
    SetTextColor(hdc, ui::Theme::AccentCyan);
    SelectObject(hdc, theme.fontBodyBold);
    DrawTextW(hdc, L"📋 Pontos da Curva", -1, &rcSideTitle, DT_LEFT | DT_SINGLELINE);

    // Lista de Pontos Formatados
    int rowY = s_rcSidePanel.top + 36;
    SelectObject(hdc, theme.fontSmall);
    SetTextColor(hdc, ui::Theme::TextPrimary);

    for (size_t i = 0; i < s_workingCurve.points.size(); ++i) {
        if (rowY + 20 > s_rcSidePanel.bottom - 90) break;
        std::wstring ptRow = L"P" + std::to_wstring(i + 1) + L":  " +
                             std::to_wstring(static_cast<int>(s_workingCurve.points[i].tempC)) + L" °C   ➔   " +
                             std::to_wstring(s_workingCurve.points[i].speedPercent) + L" %";

        RECT rcRow = { s_rcSidePanel.left + 14, rowY, s_rcSidePanel.right - 14, rowY + 18 };
        if (static_cast<int>(i) == s_hoveredPointIndex || static_cast<int>(i) == s_draggedPointIndex) {
            SetTextColor(hdc, ui::Theme::AccentCyan);
        } else {
            SetTextColor(hdc, ui::Theme::TextPrimary);
        }
        DrawTextW(hdc, ptRow.c_str(), -1, &rcRow, DT_LEFT | DT_SINGLELINE);
        rowY += 20;
    }

    // Dicas de Utilização no Rodapé do Painel Lateral
    RECT rcHelp = { s_rcSidePanel.left + 10, s_rcSidePanel.bottom - 80, s_rcSidePanel.right - 10, s_rcSidePanel.bottom - 8 };
    SetTextColor(hdc, ui::Theme::TextSecondary);
    SelectObject(hdc, theme.fontSmall);
    const wchar_t* helpText = L"💡 Como editar:\n"
                              L"• Arraste a bolinha com o botão esquerdo para mudar.\n"
                              L"• Clique com botão direito para adicionar ou excluir ponto.";
    DrawTextW(hdc, helpText, -1, &rcHelp, DT_LEFT | DT_WORDBREAK);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK FanCurveEditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& theme = ui::Theme::Instance();

    switch (msg) {
    case WM_CREATE: {
        s_hDlg = hwnd;

        // Seletor de Ventoinha / Hardware no topo
        HWND lblDev = CreateWindowW(L"STATIC", L"Ventoinha:", WS_CHILD | WS_VISIBLE,
                                    540, 20, 80, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblDev, WM_SETFONT, (WPARAM)theme.fontBodyBold, TRUE);

        s_comboTargetDev = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         625, 17, 270, 200, hwnd, (HMENU)(INT_PTR)ID_COMBO_TARGET_DEV, hInst, nullptr);
        SendMessageW(s_comboTargetDev, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        auto devList = FanControlManager::Instance().GetDevices();
        int curSel = 0;
        for (size_t i = 0; i < devList.size(); ++i) {
            std::wstring item = StringUtils::ToWide(devList[i].name) + L" [" + StringUtils::ToWide(devList[i].vendor) + L"]";
            SendMessageW(s_comboTargetDev, CB_ADDSTRING, 0, (LPARAM)item.c_str());
            if (devList[i].id == s_targetDeviceId) {
                curSel = (int)i;
            }
        }
        SendMessageW(s_comboTargetDev, CB_SETCURSEL, curSel, 0);

        // 1. Linha de Presets
        HWND lblPresets = CreateWindowW(L"STATIC", L"Presets Rápidos:", WS_CHILD | WS_VISIBLE,
                                        55, 452, 115, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblPresets, WM_SETFONT, (WPARAM)theme.fontBodyBold, TRUE);

        HWND btnSilent = CreateWindowW(L"BUTTON", L"🤫 Silencioso", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       175, 448, 115, 28, hwnd, (HMENU)(INT_PTR)ID_PRESET_SILENT, hInst, nullptr);
        SendMessageW(btnSilent, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND btnBal = CreateWindowW(L"BUTTON", L"⚖️ Balanceado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    298, 448, 115, 28, hwnd, (HMENU)(INT_PTR)ID_PRESET_BALANCED, hInst, nullptr);
        SendMessageW(btnBal, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND btnAggr = CreateWindowW(L"BUTTON", L"🚀 Desempenho / Game", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     421, 448, 160, 28, hwnd, (HMENU)(INT_PTR)ID_PRESET_AGGRESSIVE, hInst, nullptr);
        SendMessageW(btnAggr, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND btnReset = CreateWindowW(L"BUTTON", L"🔄 Resetar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      589, 448, 95, 28, hwnd, (HMENU)(INT_PTR)ID_BTN_RESET_CURVE, hInst, nullptr);
        SendMessageW(btnReset, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        // 2. Parâmetros de Histerese e Emergência
        HWND lblH = CreateWindowW(L"STATIC", L"Histerese (°C):", WS_CHILD | WS_VISIBLE,
                                  55, 492, 95, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblH, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);
        s_editHyst = CreateWindowW(L"EDIT", std::to_wstring(s_workingCurve.hysteresisC).c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                   152, 490, 45, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editHyst, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND lblE = CreateWindowW(L"STATIC", L"Temp. Emergência (°C):", WS_CHILD | WS_VISIBLE,
                                  215, 492, 150, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblE, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);
        s_editEmerg = CreateWindowW(L"EDIT", std::to_wstring(static_cast<int>(s_workingCurve.emergencyTempC)).c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                    370, 490, 45, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editEmerg, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        s_lblApplyFeedback = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           430, 492, 450, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblApplyFeedback, WM_SETFONT, (WPARAM)theme.fontSmallBold, TRUE);

        // 3. Linha Inferior com Botões Padrão Windows (OK, Aplicar, Cancelar)
        HWND btnCancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       605, 532, 90, 32, hwnd, (HMENU)(INT_PTR)ID_BTN_CANCEL, hInst, nullptr);
        SendMessageW(btnCancel, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND btnApply = CreateWindowW(L"BUTTON", L"Aplicar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      703, 532, 90, 32, hwnd, (HMENU)(INT_PTR)ID_BTN_APPLY, hInst, nullptr);
        SendMessageW(btnApply, WM_SETFONT, (WPARAM)theme.fontBody, TRUE);

        HWND btnOk = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   801, 532, 94, 32, hwnd, (HMENU)(INT_PTR)ID_BTN_OK, hInst, nullptr);
        SendMessageW(btnOk, WM_SETFONT, (WPARAM)theme.fontBodyBold, TRUE);

        SetTimer(hwnd, ID_TIMER_LIVE_TEMP, 1000, nullptr);
        return 0;
    }

    case WM_TIMER: {
        if (wp == ID_TIMER_LIVE_TEMP) {
            FanControlManager::Instance().RefreshTelemetryOnly();
            InvalidateRect(hwnd, &s_rcGraph, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP oldBm = (HBITMAP)SelectObject(memDC, memBm);

        FillRect(memDC, &rcClient, theme.brushBgDark);

        // Header Superior
        SetBkMode(memDC, TRANSPARENT);
        HFONT oldFont = (HFONT)SelectObject(memDC, theme.fontTitle);
        SetTextColor(memDC, ui::Theme::AccentCyan);
        RECT rcTitle = { 55, 12, 530, 38 };
        DrawTextW(memDC, L"📈 Editor Visual de Curva de Ventoinha", -1, &rcTitle, DT_LEFT | DT_SINGLELINE);

        SelectObject(memDC, theme.fontSmall);
        SetTextColor(memDC, ui::Theme::TextSecondary);
        FanDeviceInfo dev;
        std::wstring subTitle = L"Dispositivo: " + StringUtils::ToWide(s_targetDeviceId);
        if (FanControlManager::Instance().GetDeviceInfo(s_targetDeviceId, dev)) {
            subTitle = L"Dispositivo: " + StringUtils::ToWide(dev.name) + L" (" + StringUtils::ToWide(dev.vendor) + L")";
            if (dev.latestReading.hasTemperature) {
                subTitle += L" | Temp: " + std::to_wstring(static_cast<int>(dev.latestReading.temperatureC)) + L"°C";
            }
            if (dev.latestReading.hasRpm) {
                subTitle += L" | RPM: " + std::to_wstring(dev.latestReading.rpm);
            }
        }
        RECT rcSub = { 55, 40, 530, 62 };
        DrawTextW(memDC, subTitle.c_str(), -1, &rcSub, DT_LEFT | DT_SINGLELINE);

        SelectObject(memDC, oldFont);

        DrawGraph(memDC, hwnd);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp);
        int y = HIWORD(lp);

        int hit = HitTestPoint(x, y);
        if (hit >= 0) {
            s_draggedPointIndex = hit;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(lp);
        int y = HIWORD(lp);

        if (s_draggedPointIndex >= 0 && s_draggedPointIndex < (int)s_workingCurve.points.size()) {
            double newTemp = XToTemp(x);
            uint32_t newSpeed = YToSpeed(y);

            double minTemp = (s_draggedPointIndex == 0) ? 20.0 : (s_workingCurve.points[s_draggedPointIndex - 1].tempC + 2.0);
            double maxTemp = (s_draggedPointIndex == (int)s_workingCurve.points.size() - 1) ? 100.0 : (s_workingCurve.points[s_draggedPointIndex + 1].tempC - 2.0);

            if (minTemp > maxTemp) minTemp = maxTemp;
            s_workingCurve.points[s_draggedPointIndex].tempC = std::clamp(newTemp, minTemp, maxTemp);
            s_workingCurve.points[s_draggedPointIndex].speedPercent = newSpeed;

            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            int prevHover = s_hoveredPointIndex;
            s_hoveredPointIndex = HitTestPoint(x, y);
            if (prevHover != s_hoveredPointIndex) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (s_draggedPointIndex >= 0) {
            s_draggedPointIndex = -1;
            ReleaseCapture();
            std::sort(s_workingCurve.points.begin(), s_workingCurve.points.end());
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        int x = LOWORD(lp);
        int y = HIWORD(lp);

        int hit = HitTestPoint(x, y);
        POINT screenPt = { x, y };
        ClientToScreen(hwnd, &screenPt);

        if (hit >= 0) {
            HMENU hMenu = CreatePopupMenu();
            if (s_workingCurve.points.size() > 2) {
                std::wstring itemTxt = L"❌ Remover este ponto (" +
                                       std::to_wstring(static_cast<int>(s_workingCurve.points[hit].tempC)) + L"°C : " +
                                       std::to_wstring(s_workingCurve.points[hit].speedPercent) + L"%)";
                AppendMenuW(hMenu, MF_STRING, ID_MENU_REMOVE_POINT, itemTxt.c_str());
            } else {
                AppendMenuW(hMenu, MF_GRAYED, 0, L"⚠️ Mínimo de 2 pontos obrigatório");
            }

            int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            if (sel == ID_MENU_REMOVE_POINT) {
                s_workingCurve.points.erase(s_workingCurve.points.begin() + hit);
                s_hoveredPointIndex = -1;
                s_draggedPointIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (x >= s_rcGraph.left && x <= s_rcGraph.right && y >= s_rcGraph.top && y <= s_rcGraph.bottom) {
            s_rightClickTemp = XToTemp(x);
            s_rightClickSpeed = YToSpeed(y);

            HMENU hMenu = CreatePopupMenu();
            std::wstring itemTxt = L"➕ Adicionar ponto aqui (" +
                                   std::to_wstring(static_cast<int>(s_rightClickTemp)) + L"°C : " +
                                   std::to_wstring(s_rightClickSpeed) + L"%)";
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ADD_POINT, itemTxt.c_str());

            int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            if (sel == ID_MENU_ADD_POINT) {
                s_workingCurve.points.push_back({ s_rightClickTemp, s_rightClickSpeed });
                std::sort(s_workingCurve.points.begin(), s_workingCurve.points.end());
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_COMBO_TARGET_DEV && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(s_comboTargetDev, CB_GETCURSEL, 0, 0);
            auto devList = FanControlManager::Instance().GetDevices();
            if (sel >= 0 && sel < (int)devList.size()) {
                s_targetDeviceId = devList[sel].id;
                FanDeviceInfo dev;
                if (FanControlManager::Instance().GetDeviceInfo(s_targetDeviceId, dev)) {
                    s_workingCurve = dev.curveConfig;
                    if (s_workingCurve.points.empty()) {
                        s_workingCurve = FanCurveConfig{};
                    }
                    SetWindowTextW(s_editHyst, std::to_wstring(s_workingCurve.hysteresisC).c_str());
                    SetWindowTextW(s_editEmerg, std::to_wstring(static_cast<int>(s_workingCurve.emergencyTempC)).c_str());
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        if (id == ID_PRESET_SILENT) {
            s_workingCurve.points = {
                { 35.0, 20 },
                { 55.0, 35 },
                { 70.0, 50 },
                { 82.0, 75 },
                { 92.0, 100 }
            };
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (id == ID_PRESET_BALANCED || id == ID_BTN_RESET_CURVE) {
            s_workingCurve.points = {
                { 35.0, 30 },
                { 50.0, 45 },
                { 65.0, 65 },
                { 75.0, 85 },
                { 85.0, 100 }
            };
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (id == ID_PRESET_AGGRESSIVE) {
            s_workingCurve.points = {
                { 35.0, 45 },
                { 48.0, 65 },
                { 60.0, 85 },
                { 70.0, 100 },
                { 85.0, 100 }
            };
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (id == ID_BTN_APPLY) {
            SaveWorkingCurve();
            if (s_lblApplyFeedback) {
                SetWindowTextW(s_lblApplyFeedback, L"✓ Curva aplicada na ventoinha com sucesso!");
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (id == ID_BTN_OK) {
            SaveWorkingCurve();
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == ID_BTN_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_LIVE_TEMP);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool FanCurveEditorDialog::Show(HWND hParent, const std::string& deviceId) {
    auto& mgr = FanControlManager::Instance();
    std::vector<FanDeviceInfo> devices = mgr.GetDevices();
    if (devices.empty()) {
        MessageBoxW(hParent, L"Nenhum controlador de ventilador detectado no sistema para configurar curvas.",
                    L"Aviso", MB_OK | MB_ICONWARNING);
        return false;
    }

    s_targetDeviceId = deviceId;
    if (s_targetDeviceId.empty()) {
        s_targetDeviceId = devices.front().id;
        for (const auto& d : devices) {
            if (d.capabilities.canControlSpeed) {
                s_targetDeviceId = d.id;
                break;
            }
        }
    }

    FanDeviceInfo dev;
    if (mgr.GetDeviceInfo(s_targetDeviceId, dev)) {
        s_workingCurve = dev.curveConfig;
    } else {
        s_workingCurve = FanCurveConfig{};
    }
    s_workingCurve.Validate();

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(nullptr);

    int cxBig = GetSystemMetrics(SM_CXICON);
    int cyBig = GetSystemMetrics(SM_CYICON);
    int cxSm = GetSystemMetrics(SM_CXSMICON);
    int cySm = GetSystemMetrics(SM_CYSMICON);
    if (cxSm < 16) cxSm = 16;
    if (cySm < 16) cySm = 16;

    HICON hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, cxBig, cyBig, LR_SHARED);
    HICON hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, cxSm, cySm, LR_SHARED);

    if (!hIconBig || !hIconSm) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) exeDir = exeDir.substr(0, lastSlash);

        std::vector<std::wstring> searchBases = {
            exeDir + L"\\assets\\icons\\",
            exeDir + L"\\..\\..\\assets\\icons\\",
            L"assets\\icons\\"
        };
        for (const auto& base : searchBases) {
            std::wstring full = base + L"opit_balanced.ico";
            if (!hIconBig) hIconBig = (HICON)LoadImageW(nullptr, full.c_str(), IMAGE_ICON, cxBig, cyBig, LR_LOADFROMFILE);
            if (!hIconSm)  hIconSm  = (HICON)LoadImageW(nullptr, full.c_str(), IMAGE_ICON, cxSm, cySm, LR_LOADFROMFILE);
            if (hIconBig && hIconSm) break;
        }
    }
    if (!hIconBig) hIconBig = LoadIcon(nullptr, IDI_APPLICATION);
    if (!hIconSm)  hIconSm  = hIconBig;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FanCurveEditorWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"OpitCoreFanCurveEditorClass";
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = ui::Theme::Instance().brushBgDark;
    RegisterClassExW(&wc);

    // Ajustar tamanho da janela para garantir que a área cliente tenha exatamente 950x585
    RECT rc = { 0, 0, 950, 585 };
    DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    RECT rcParent{};
    GetWindowRect(hParent, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - winW) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - winH) / 2;
    if (posX < 10) posX = 10;
    if (posY < 10) posY = 10;

    HWND dlg = CreateWindowExW(
        dwExStyle,
        wc.lpszClassName,
        L"Opit-Core — Editor Visual de Curva de Ventoinha",
        dwStyle,
        posX, posY, winW, winH,
        hParent, nullptr, hInst, nullptr
    );

    if (!dlg) return false;

    SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    ui::Theme::EnableImmersiveDarkMode(dlg);
    EnableWindow(hParent, FALSE);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DestroyWindow(dlg);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
    return true;
}

} // namespace opitcore
