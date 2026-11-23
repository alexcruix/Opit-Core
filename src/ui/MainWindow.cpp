// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "MainWindow.h"
#include "UnifiedSettingsWindow.h"
#include "AdvancedModuleDialogs.h"
#include "FanCurveEditorDialog.h"
#include "core/CoreRuntime.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/DatabaseManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/UnifiedSettingsModel.h"
#include "core/NotificationManager.h"
#include "control/ModeCoordinator.h"
#include "adaptive/AdaptiveEngine.h"
#include "platform/windows/ProcessManager.h"
#include "platform/windows/ProcessGovernor.h"
#include "platform/windows/ServiceManager.h"
#include "platform/windows/ElevationHelper.h"
#include "utils/StringUtils.h"

#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#pragma comment(lib, "comctl32.lib")

namespace opitcore {

// Constantes de IDs de controles e mensagens
static const UINT WM_OPIT_TRAY = WM_USER + 10;
static const UINT s_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
static const UINT ID_TIMER_UPDATE = 3001;

// Header do SDK aqui nao declara esta flag (requer _WIN32_WINNT >= 0x0602); o valor e estavel.
#ifndef NIIF_RESPECT_NO_QUIET
#define NIIF_RESPECT_NO_QUIET 0x00000080
#endif

// Menu da bandeja
static const UINT ID_TRAY_OPEN = 4001;
static const UINT ID_TRAY_TOGGLE = 4002;
static const UINT ID_TRAY_PROFILE_ECO = 4003;
static const UINT ID_TRAY_PROFILE_BAL = 4004;
static const UINT ID_TRAY_PROFILE_HIGH = 4005;
static const UINT ID_TRAY_UNIFIED = 4006;
static const UINT ID_TRAY_EXIT = 4007;
static const UINT ID_TRAY_TOGGLE_AI = 4008;

// IDs de botões e comandos
static const UINT ID_BTN_APPLY_PROFILE = 5001;
static const UINT ID_BTN_APPLY_THROTTLE = 5002;
static const UINT ID_BTN_OPTIMIZE_RAM = 5003;
static const UINT ID_BTN_SET_POWER_SCHEME = 5004;
static const UINT ID_BTN_REFRESH_PROC = 5005;
static const UINT ID_BTN_SET_PRIORITY = 5006;
static const UINT ID_BTN_SET_AFFINITY = 5007;
static const UINT ID_BTN_KILL_PROC = 5008;
static const UINT ID_BTN_ADD_GAME = 5009;
static const UINT ID_BTN_REMOVE_GAME = 5010;
static const UINT ID_BTN_APPLY_THERMAL = 5011;
static const UINT ID_BTN_SAVE_CONFIG = 5012;
static const UINT ID_BTN_RESET_DEFAULTS = 5013;
static const UINT ID_BTN_REFRESH_LOGS = 5014;
static const UINT ID_BTN_COPY_DIAG = 5015;
static const UINT ID_COMBO_PROFILE_SEL = 5016;
static const UINT ID_BTN_REFRESH_MON = 5017;
static const UINT ID_BTN_CLEAR_MON = 5018;
static const UINT ID_BTN_OPEN_UNIFIED = 5019;
static const UINT ID_BTN_DEEP_ECO = 5021;
static const UINT ID_BTN_TOGGLE_GAMEMODE = 5022;
static const UINT ID_BTN_CPU_ADVANCED = 5023;
static const UINT ID_BTN_GPU_ADVANCED = 5024;
static const UINT ID_BTN_EDIT_GAME = 5025;
static const UINT ID_BTN_OPEN_AI = 5026;
static const UINT ID_BTN_OPEN_SERVICES_CFG = 5027;
static const UINT ID_EDIT_SVC_SEARCH = 5028;
static const UINT ID_BTN_REFRESH_SVC_LIST = 5029;
static const UINT ID_BTN_CONFIG_SVC_TRIGGERS = 5030;
static const UINT ID_COMBO_SVC_RULE = 5031;
static const UINT ID_BTN_APPLY_SVC_RULE = 5032;
static const UINT ID_BTN_APPLY_ALL_SVC_RULE = 5038;
static const UINT ID_BTN_PAUSE_SVC_NOW = 5033;
static const UINT ID_BTN_RESUME_SVC_NOW = 5034;
static const UINT ID_BTN_RESTORE_ALL_SVCS = 5035;
static const UINT ID_BTN_OPEN_FANS = 5036;
static const UINT ID_BTN_OPEN_FANS_GRAPH = 5037;
static const UINT ID_BTN_COPY_PIX = 5039;

MainWindow& MainWindow::Instance() {
    static MainWindow instance;
    return instance;
}

MainWindow::MainWindow() {
    m_allTabControls = {
        &m_tab0Controls, &m_tab1Controls, &m_tab2Controls, &m_tab3Controls,
        &m_tab4Controls, &m_tab5Controls, &m_tab6Controls, &m_tab7Controls,
        &m_tab8Controls, &m_tab9Controls, &m_tab10Controls, &m_tab11Controls,
        &m_tab12Controls, &m_tab13Controls
    };
}

MainWindow::~MainWindow() {
    if (m_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    }
    FreeAppIcons();
    if (m_hFontNormal) DeleteObject(m_hFontNormal);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontMono) DeleteObject(m_hFontMono);
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
}

void MainWindow::FreeAppIcons() {
    m_hIconMain = nullptr;
    m_hIconTrayBalanced = nullptr;
    m_hIconTrayEco = nullptr;
    m_hIconTrayPerf = nullptr;
    m_hIconTrayAi = nullptr;
}

void MainWindow::LoadAppIcons() {
    FreeAppIcons();

    // 1. Tenta carregar dos recursos embutidos no binario (IDI_OPIT_BALANCED = 101, etc.)
    m_hIconMain = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);

    int cxSm = GetSystemMetrics(SM_CXSMICON);
    int cySm = GetSystemMetrics(SM_CYSMICON);
    if (cxSm < 16) cxSm = 16;
    if (cySm < 16) cySm = 16;

    m_hIconTrayBalanced = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, cxSm, cySm, LR_SHARED);
    m_hIconTrayEco      = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(102), IMAGE_ICON, cxSm, cySm, LR_SHARED);
    m_hIconTrayPerf     = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(103), IMAGE_ICON, cxSm, cySm, LR_SHARED);
    m_hIconTrayAi       = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(104), IMAGE_ICON, cxSm, cySm, LR_SHARED);

    // 2. Se nao estiverem no recurso (execucao em dev/testes), busca arquivos .ico no disco
    if (!m_hIconTrayBalanced || !m_hIconTrayEco || !m_hIconTrayPerf || !m_hIconTrayAi) {
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

        auto tryLoadFile = [&](const std::wstring& filename, int iconW, int iconH) -> HICON {
            for (const auto& base : searchBases) {
                std::wstring full = base + filename;
                HICON hIcon = (HICON)LoadImageW(nullptr, full.c_str(), IMAGE_ICON, iconW, iconH, LR_LOADFROMFILE);
                if (hIcon) return hIcon;
            }
            return nullptr;
        };

        if (!m_hIconMain) {
            m_hIconMain = tryLoadFile(L"opit_balanced.ico", GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
        }
        if (!m_hIconTrayBalanced) {
            m_hIconTrayBalanced = tryLoadFile(L"opit_balanced.ico", cxSm, cySm);
        }
        if (!m_hIconTrayEco) {
            m_hIconTrayEco = tryLoadFile(L"opit_eco.ico", cxSm, cySm);
        }
        if (!m_hIconTrayPerf) {
            m_hIconTrayPerf = tryLoadFile(L"opit_performance.ico", cxSm, cySm);
        }
        if (!m_hIconTrayAi) {
            m_hIconTrayAi = tryLoadFile(L"opit_ai.ico", cxSm, cySm);
        }
    }

    // 3. Fallback de seguranca caso o arquivo nao exista
    if (!m_hIconMain) m_hIconMain = LoadIcon(nullptr, IDI_APPLICATION);
    if (!m_hIconTrayBalanced) m_hIconTrayBalanced = m_hIconMain;
    if (!m_hIconTrayEco) m_hIconTrayEco = m_hIconTrayBalanced;
    if (!m_hIconTrayPerf) m_hIconTrayPerf = m_hIconTrayBalanced;
    if (!m_hIconTrayAi) m_hIconTrayAi = m_hIconTrayBalanced;
}

void MainWindow::ShowTrayBalloon(const std::wstring& title, const std::wstring& message) {
    if (!m_nid.hWnd) return;
    m_nid.uFlags |= NIF_INFO;
    wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
    wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    m_nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_NO_QUIET;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    // O banner vale uma vez: sem tirar o NIF_INFO, o proximo NIM_MODIFY (troca de icone do modo)
    // re-dispararia um balao vazio na tela.
    m_nid.uFlags &= ~NIF_INFO;
}

void MainWindow::UpdateTrayVisual(TrayVisualMode mode, const std::wstring& customTooltip) {
    HICON targetIcon = m_hIconTrayBalanced;
    std::wstring tip = L"Opit-Core — Modo Balanceado";

    switch (mode) {
    case TrayVisualMode::Eco:
        targetIcon = m_hIconTrayEco ? m_hIconTrayEco : m_hIconTrayBalanced;
        tip = L"Opit-Core — Modo Economico";
        break;
    case TrayVisualMode::Performance:
        targetIcon = m_hIconTrayPerf ? m_hIconTrayPerf : m_hIconTrayBalanced;
        tip = L"Opit-Core — Modo Alto Desempenho";
        break;
    case TrayVisualMode::AI:
        targetIcon = m_hIconTrayAi ? m_hIconTrayAi : m_hIconTrayBalanced;
        tip = L"Opit-Core — Inteligencia IA Ativa (Auto-Otimizando)";
        break;
    case TrayVisualMode::Balanced:
    default:
        targetIcon = m_hIconTrayBalanced;
        tip = L"Opit-Core — Modo Balanceado";
        break;
    }

    if (!customTooltip.empty()) {
        tip = customTooltip;
    }

    m_currentTrayMode = mode;
    m_nid.hIcon = targetIcon ? targetIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;
    LoadAppIcons();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    auto& theme = ui::Theme::Instance();
    m_hFontNormal = theme.fontBody;
    m_hFontBold   = theme.fontBodyBold;
    m_hFontTitle  = theme.fontTitle;
    m_hFontMono   = theme.fontMono;
    m_hBgBrush    = theme.brushBgDark;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WndProcStatic;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OpitCoreMainWindowClass";
    wc.hIcon = m_hIconMain ? m_hIconMain : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = m_hIconTrayBalanced ? m_hIconTrayBalanced : wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_hBgBrush;

    RegisterClassExW(&wc);

    RECT rcWork{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int initW = 1100;
    int initH = 720;
    int posX = rcWork.left + (rcWork.right - rcWork.left - initW) / 2;
    int posY = rcWork.top + (rcWork.bottom - rcWork.top - initH) / 2;
    if (posX < rcWork.left + 10) posX = rcWork.left + 10;
    if (posY < rcWork.top + 10) posY = rcWork.top + 10;

    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName,
        L"Opit-Core — Gerenciador Adaptativo de Hardware, Desempenho e Energia",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, initW, initH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    if (m_hIconMain) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_hIconMain);
    }
    if (m_hIconTrayBalanced) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hIconTrayBalanced);
    }

    ui::Theme::EnableImmersiveDarkMode(m_hwnd);
    CreateSidebar(m_hwnd);

    // Fonte de tempo real por quadro (DWM) para a funcao-objetivo adaptativa
    CoreRuntime::Instance().SetFrameTimingWindow(m_hwnd);

    // Configurar icone na bandeja
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_OPIT_TRAY;
    m_nid.hIcon = m_hIconTrayBalanced ? m_hIconTrayBalanced : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(m_nid.szTip, L"Opit-Core — Modo Balanceado");
    Shell_NotifyIconW(NIM_ADD, &m_nid);

    // A IA "olhando" o sistema avisa pelo balao do tray: sink registrado apos o icone existir.
    NotificationManager::Instance().LoadSettings();
    NotificationManager::Instance().SetSink(
        [this](const std::wstring& title, const std::wstring& msg) { ShowTrayBalloon(title, msg); });

    // Inicializar dados de hardware e listas
    PopulateHardwareInfo();
    PopulatePowerSchemes();
    RefreshGameList();
    RefreshProcessList();
    RefreshMonitoringHistory();
    RefreshLogs();

    // Atualizar periodicamente os controles desde o primeiro instante
    UpdatePeriodicData();

    // Timer de 1 segundo para atualizacao visual contínua
    SetTimer(m_hwnd, ID_TIMER_UPDATE, 1000, nullptr);

    RECT rcClient{};
    GetClientRect(m_hwnd, &rcClient);
    LayoutControls(rcClient.right, rcClient.bottom);

    bool startMin = Config::Instance().GetStartMinimized();
    if (startMin) {
        Show(false);
    } else {
        Show(true);
    }

    return true;
}

void MainWindow::Show(bool show) {
    if (show) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        m_visible = true;
    } else {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    for (;;) {
        BOOL gr = GetMessageW(&msg, nullptr, 0, 0);
        if (gr == 0) {
            // WM_QUIT so encerra o aplicativo quando fomos nos que a pedimos (bandeja > Sair,
            // ou o WM_DESTROY desta janela). Dialogos modais tambem chamam PostQuitMessage para
            // acordar os seus proprios loops e essa mensagem pode chegar aqui ja consumida por
            // eles; engula-la e continuar e o que impede o programa de morrer por fechar um
            // dialogo.
            if (!m_quitRequested) continue;
            break;
        }
        if (gr == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct NavItem {
    const wchar_t* icon;
    const wchar_t* title;
};

static const NavItem k_navItems[] = {
    { L"⚡", L"Painel Geral" },
    { L"🎯", L"Perfis de Desempenho" },
    { L"💻", L"CPU & Core Parking" },
    { L"🎮", L"Placa de Vídeo (GPU)" },
    { L"🌀", L"Memória RAM" },
    { L"🔋", L"Planos de Energia" },
    { L"⚙️", L"Processos do Sistema" },
    { L"🛠️", L"Serviços do Windows" },
    { L"🚀", L"Otimização de Jogos" },
    { L"📊", L"Monitoramento" },
    { L"🛡️", L"Guardião Térmico" },
    { L"⚙️", L"Configurações Gerais" },
    { L"🔍", L"Diagnóstico & Logs" },
    { L"🌐", L"Sobre & Código Aberto" }
};
static const int k_navItemCount = sizeof(k_navItems) / sizeof(k_navItems[0]);

LRESULT CALLBACK MainWindow::SidebarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcWindow = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        // Double buffer para pintura fluida sem flicker
        HDC hdc = CreateCompatibleDC(hdcWindow);
        HBITMAP hbm = CreateCompatibleBitmap(hdcWindow, rcClient.right, rcClient.bottom);
        HBITMAP hOldBm = (HBITMAP)SelectObject(hdc, hbm);

        auto& theme = ui::Theme::Instance();
        FillRect(hdc, &rcClient, theme.brushSidebar);

        // Header da barra lateral
        SetBkMode(hdc, TRANSPARENT);
        HFONT hOldFont = (HFONT)SelectObject(hdc, theme.fontTitle);
        SetTextColor(hdc, ui::Theme::AccentCyan);
        RECT rcTitle = { 18, 16, rcClient.right - 10, 42 };
        DrawTextW(hdc, L"OPIT-CORE", -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, theme.fontSmall);
        SetTextColor(hdc, ui::Theme::TextMuted);
        RECT rcSub = { 18, 40, rcClient.right - 10, 58 };
        DrawTextW(hdc, L"Motor Adaptativo v8.1", -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        // Divisor superior
        RECT rcDiv = { 14, 62, rcClient.right - 14, 63 };
        FillRect(hdc, &rcDiv, theme.brushBorderSubtle);

        // Itens de navegação
        int itemY = 74;
        int itemH = 34;
        for (int i = 0; i < k_navItemCount; ++i) {
            RECT rcItem = { 10, itemY, rcClient.right - 10, itemY + itemH };
            bool isCurrent = (pThis && pThis->m_currentTab == i);
            bool isHovered = (pThis && pThis->m_hoveredTab == i);

            if (isCurrent) {
                FillRect(hdc, &rcItem, theme.brushSidebarSelected);
                RECT rcPill = { 10, itemY + 4, 14, itemY + itemH - 4 };
                FillRect(hdc, &rcPill, theme.brushAccentCyan);
                SelectObject(hdc, theme.fontBodyBold);
                SetTextColor(hdc, ui::Theme::AccentCyan);
            } else if (isHovered) {
                FillRect(hdc, &rcItem, theme.brushSidebarHover);
                SelectObject(hdc, theme.fontBody);
                SetTextColor(hdc, ui::Theme::TextPrimary);
            } else {
                SelectObject(hdc, theme.fontBody);
                SetTextColor(hdc, ui::Theme::TextSecondary);
            }

            RECT rcIcon = { 20, itemY, 44, itemY + itemH };
            DrawTextW(hdc, k_navItems[i].icon, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT rcText = { 48, itemY, rcItem.right - 8, itemY + itemH };
            DrawTextW(hdc, k_navItems[i].title, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            itemY += itemH + 2;
        }

        // Rodapé da barra lateral
        int footerY = rcClient.bottom - 75;
        if (footerY > itemY + 10) {
            RECT rcDivF = { 14, footerY, rcClient.right - 14, footerY + 1 };
            FillRect(hdc, &rcDivF, theme.brushBorderSubtle);

            RECT rcPill1 = { 16, footerY + 10, rcClient.right - 16, footerY + 34 };
            ui::Theme::DrawStatusPill(hdc, rcPill1, L"Orquestrador: Ativo", ui::Theme::StatusSuccess);

            RECT rcBtnUnif = { 16, footerY + 40, rcClient.right - 16, footerY + 68 };
            FillRect(hdc, &rcBtnUnif, theme.brushBgCard);
            FrameRect(hdc, &rcBtnUnif, theme.brushBorderCard);
            SelectObject(hdc, theme.fontSmallBold);
            SetTextColor(hdc, ui::Theme::AccentCyan);
            DrawTextW(hdc, L"⚙️ Painel Unificado", -1, &rcBtnUnif, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, hOldFont);
        BitBlt(hdcWindow, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, hOldBm);
        DeleteObject(hbm);
        DeleteDC(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        int y = GET_Y_LPARAM(lParam);
        int hovered = (y - 74) / 36;
        if (hovered < 0 || hovered >= k_navItemCount) hovered = -1;
        if (pThis && pThis->m_hoveredTab != hovered) {
            pThis->m_hoveredTab = hovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (pThis && pThis->m_hoveredTab != -1) {
            pThis->m_hoveredTab = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int y = GET_Y_LPARAM(lParam);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        int footerY = rcClient.bottom - 75;
        if (y >= footerY + 40 && y <= footerY + 68) {
            if (pThis && pThis->m_hwnd) {
                SendMessageW(pThis->m_hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_OPEN_UNIFIED, 0), 0);
            }
            return 0;
        }

        int clicked = (y - 74) / 36;
        if (clicked >= 0 && clicked < k_navItemCount && pThis) {
            pThis->SwitchTab(clicked);
            InvalidateRect(hwnd, nullptr, FALSE);
            RECT rcMain;
            GetClientRect(pThis->m_hwnd, &rcMain);
            pThis->LayoutControls(rcMain.right, rcMain.bottom);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::CreateSidebar(HWND hwnd) {
    WNDCLASSEXW swc{};
    swc.cbSize = sizeof(swc);
    swc.lpfnWndProc = MainWindow::SidebarWndProc;
    swc.hInstance = m_hInstance;
    swc.lpszClassName = L"OpitCoreSidebarClass";
    swc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    swc.hbrBackground = ui::Theme::Instance().brushSidebar;
    RegisterClassExW(&swc);

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int sidebarW = (rcClient.right < 900) ? 190 : 230;

    m_sidebarHwnd = CreateWindowExW(
        0, swc.lpszClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, sidebarW, rcClient.bottom,
        hwnd, (HMENU)101, m_hInstance, this
    );
}

void MainWindow::LayoutControls(int width, int height) {
    if (width <= 0 || height <= 0 || !m_hwnd) return;

    int sidebarW = (width < 900) ? 200 : 240;
    if (m_sidebarHwnd) {
        SetWindowPos(m_sidebarHwnd, nullptr, 0, 0, sidebarW, height, SWP_NOZORDER);
        InvalidateRect(m_sidebarHwnd, nullptr, FALSE);
    }

    int cx = sidebarW + 28;
    int cy = 24;
    int cw = width - cx - 28;
    int ch = height - cy - 24;
    if (cw < 400) cw = 400;
    if (ch < 300) ch = 300;

    switch (m_currentTab) {
    case 0: { // Dashboard
        if (m_tab0Controls.size() > 0) SetWindowPos(m_tab0Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);

        // 1. Faixa de status do produto (donationware: sem ativação, sem servidor)
        int licTopY = cy + 32;
        if (m_lblDashStatus) {
            SetWindowPos(m_lblDashStatus, nullptr, cx, licTopY, cw, 32, SWP_NOZORDER);
        }
        int hwStartY = licTopY + 42;

        // 2. Telemetria de Hardware (Colunas ou Coluna Única)
        int yQuick = hwStartY;
        if (cw >= 700) {
            int cardW = (cw - 24) / 2;
            int col2X = cx + cardW + 24;

            // Coluna 1: Processador, GPU, Modo Jogo
            int y1 = hwStartY;
            if (m_lblDashCpu)    SetWindowPos(m_lblDashCpu,    nullptr, cx, y1, cardW, 20, SWP_NOZORDER);
            if (m_progDashCpu)   SetWindowPos(m_progDashCpu,   nullptr, cx, y1 + 24, cardW, 14, SWP_NOZORDER);
            if (m_lblDashDocCpu) SetWindowPos(m_lblDashDocCpu, nullptr, cx, y1 + 46, cardW, 20, SWP_NOZORDER);
            if (m_lblDashTemp)   SetWindowPos(m_lblDashTemp,   nullptr, cx, y1 + 70, cardW, 20, SWP_NOZORDER);
            if (m_lblDashGpu)    SetWindowPos(m_lblDashGpu,    nullptr, cx, y1 + 94, cardW, 22, SWP_NOZORDER);
            if (m_lblDashGame)   SetWindowPos(m_lblDashGame,   nullptr, cx, y1 + 120, cardW, 20, SWP_NOZORDER);
            int col1Bottom = y1 + 144;

            // Coluna 2: RAM, Energia, Perfil
            int y2 = hwStartY;
            if (m_lblDashRam)     SetWindowPos(m_lblDashRam,     nullptr, col2X, y2, cardW, 20, SWP_NOZORDER);
            if (m_progDashRam)    SetWindowPos(m_progDashRam,    nullptr, col2X, y2 + 24, cardW, 14, SWP_NOZORDER);
            if (m_lblDashPower)   SetWindowPos(m_lblDashPower,   nullptr, col2X, y2 + 46, cardW, 20, SWP_NOZORDER);
            if (m_lblDashProfile) SetWindowPos(m_lblDashProfile, nullptr, col2X, y2 + 70, cardW, 20, SWP_NOZORDER);
            int col2Bottom = y2 + 96;

            yQuick = (std::max)(col1Bottom, col2Bottom) + 16;
        } else {
            int y = hwStartY;
            if (m_lblDashCpu)    SetWindowPos(m_lblDashCpu,    nullptr, cx, y, cw, 20, SWP_NOZORDER);
            if (m_progDashCpu)   SetWindowPos(m_progDashCpu,   nullptr, cx, y + 24, cw, 14, SWP_NOZORDER);
            if (m_lblDashDocCpu) SetWindowPos(m_lblDashDocCpu, nullptr, cx, y + 46, cw, 20, SWP_NOZORDER);
            if (m_lblDashTemp)   SetWindowPos(m_lblDashTemp,   nullptr, cx, y + 70, cw, 20, SWP_NOZORDER);
            if (m_lblDashGpu)    SetWindowPos(m_lblDashGpu,    nullptr, cx, y + 94, cw, 22, SWP_NOZORDER);
            if (m_lblDashGame)   SetWindowPos(m_lblDashGame,   nullptr, cx, y + 120, cw, 20, SWP_NOZORDER);
            y += 146;

            if (m_lblDashRam)     SetWindowPos(m_lblDashRam,     nullptr, cx, y, cw, 20, SWP_NOZORDER);
            if (m_progDashRam)    SetWindowPos(m_progDashRam,    nullptr, cx, y + 24, cw, 14, SWP_NOZORDER);
            if (m_lblDashPower)   SetWindowPos(m_lblDashPower,   nullptr, cx, y + 46, cw, 20, SWP_NOZORDER);
            if (m_lblDashProfile) SetWindowPos(m_lblDashProfile, nullptr, cx, y + 70, cw, 20, SWP_NOZORDER);
            y += 98;

            yQuick = y + 12;
        }

        // 3. Atalhos Rápidos de Perfil & Coordenação
        if (m_tab0Controls.size() > 13) {
            SetWindowPos(m_tab0Controls[13], nullptr, cx, yQuick, cw, 22, SWP_NOZORDER);
        }

        int btnY = yQuick + 28;
        int yCoord = btnY + 36;
        if (cw >= 700) {
            // 5 botões em linha única proporcional
            int gap = 8;
            int btnW = (cw - (gap * 4)) / 5;
            if (btnW < 100) btnW = 100;
            if (m_tab0Controls.size() > 14) SetWindowPos(m_tab0Controls[14], nullptr, cx + (btnW + gap) * 0, btnY, btnW, 30, SWP_NOZORDER);
            if (m_tab0Controls.size() > 15) SetWindowPos(m_tab0Controls[15], nullptr, cx + (btnW + gap) * 1, btnY, btnW, 30, SWP_NOZORDER);
            if (m_tab0Controls.size() > 16) SetWindowPos(m_tab0Controls[16], nullptr, cx + (btnW + gap) * 2, btnY, btnW, 30, SWP_NOZORDER);
            if (m_btnDashDeepEco)           SetWindowPos(m_btnDashDeepEco,   nullptr, cx + (btnW + gap) * 3, btnY, btnW, 30, SWP_NOZORDER);
            if (m_btnDashGameMode)          SetWindowPos(m_btnDashGameMode,  nullptr, cx + (btnW + gap) * 4, btnY, btnW, 30, SWP_NOZORDER);
            yCoord = btnY + 38;
        } else {
            // Em 2 linhas organizadas
            int gap = 8;
            int row1W = (cw - (gap * 2)) / 3;
            if (m_tab0Controls.size() > 14) SetWindowPos(m_tab0Controls[14], nullptr, cx + (row1W + gap) * 0, btnY, row1W, 30, SWP_NOZORDER);
            if (m_tab0Controls.size() > 15) SetWindowPos(m_tab0Controls[15], nullptr, cx + (row1W + gap) * 1, btnY, row1W, 30, SWP_NOZORDER);
            if (m_tab0Controls.size() > 16) SetWindowPos(m_tab0Controls[16], nullptr, cx + (row1W + gap) * 2, btnY, row1W, 30, SWP_NOZORDER);
            
            int row2Y = btnY + 36;
            int row2W = (cw - gap) / 2;
            if (m_btnDashDeepEco)  SetWindowPos(m_btnDashDeepEco,  nullptr, cx, row2Y, row2W, 30, SWP_NOZORDER);
            if (m_btnDashGameMode) SetWindowPos(m_btnDashGameMode, nullptr, cx + row2W + gap, row2Y, row2W, 30, SWP_NOZORDER);
            yCoord = row2Y + 38;
        }

        // 4. Status do Coordenador
        if (m_lblDashCoordinator) {
            SetWindowPos(m_lblDashCoordinator, nullptr, cx, yCoord, cw, 22, SWP_NOZORDER);
        }

        // 5. Botões de Ação Principais (Painel Unificado + IA Copilot lado a lado)
        int yAction = yCoord + 28;
        int actionBtnW = (cw - 12) / 2;
        if (m_tab0Controls.size() > 20) {
            SetWindowPos(m_tab0Controls[20], nullptr, cx, yAction, actionBtnW, 34, SWP_NOZORDER);
        }
        if (m_tab0Controls.size() > 21) {
            SetWindowPos(m_tab0Controls[21], nullptr, cx + actionBtnW + 12, yAction, actionBtnW, 34, SWP_NOZORDER);
        }

        // 6. Nota explicativa no rodapé
        if (m_tab0Controls.size() > 22) {
            SetWindowPos(m_tab0Controls[22], nullptr, cx, yAction + 42, cw, 22, SWP_NOZORDER);
        }
        break;
    }

    case 1: { // Perfis
        if (m_tab1Controls.size() > 0) SetWindowPos(m_tab1Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_tab1Controls.size() > 1) SetWindowPos(m_tab1Controls[1], nullptr, cx, cy + 34, cw, 22, SWP_NOZORDER);
        
        int y1 = cy + 64;
        if (cw >= 560) {
            int comboW = (std::min)(cw - 200, 380);
            if (m_comboProfiles)   SetWindowPos(m_comboProfiles,   nullptr, cx, y1 + 2, comboW, 150, SWP_NOZORDER);
            if (m_btnApplyProfile) SetWindowPos(m_btnApplyProfile, nullptr, cx + comboW + 15, y1, 180, 32, SWP_NOZORDER);
            y1 += 44;
        } else {
            if (m_comboProfiles)   SetWindowPos(m_comboProfiles,   nullptr, cx, y1 + 2, cw, 150, SWP_NOZORDER);
            if (m_btnApplyProfile) SetWindowPos(m_btnApplyProfile, nullptr, cx, y1 + 38, (std::min)(cw, 220), 32, SWP_NOZORDER);
            y1 += 80;
        }

        if (m_lblProfileDesc)   SetWindowPos(m_lblProfileDesc,   nullptr, cx, y1, cw, 48, SWP_NOZORDER);
        if (m_lblProfileStatus) SetWindowPos(m_lblProfileStatus, nullptr, cx, y1 + 54, cw, 24, SWP_NOZORDER);
        if (m_tab1Controls.size() > 6) SetWindowPos(m_tab1Controls[6], nullptr, cx, y1 + 86, cw, 110, SWP_NOZORDER);
        break;
    }

    case 2: { // CPU
        if (m_tab2Controls.size() > 0) SetWindowPos(m_tab2Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblCpuInfo)     SetWindowPos(m_lblCpuInfo,     nullptr, cx, cy + 36, cw, 22, SWP_NOZORDER);
        if (m_lblCpuCores)    SetWindowPos(m_lblCpuCores,    nullptr, cx, cy + 64, cw, 22, SWP_NOZORDER);
        if (m_lblCpuArch)     SetWindowPos(m_lblCpuArch,     nullptr, cx, cy + 92, cw, 22, SWP_NOZORDER);
        if (m_lblCpuDocSpecs) SetWindowPos(m_lblCpuDocSpecs, nullptr, cx, cy + 120, cw, 45, SWP_NOZORDER);
        if (m_lblCpuUsage)    SetWindowPos(m_lblCpuUsage,    nullptr, cx, cy + 175, cw, 22, SWP_NOZORDER);
        if (m_progCpuUsage)   SetWindowPos(m_progCpuUsage,   nullptr, cx, cy + 200, (std::min)(cw, 500), 18, SWP_NOZORDER);
        if (m_tab2Controls.size() > 7) SetWindowPos(m_tab2Controls[7], nullptr, cx, cy + 232, cw, 22, SWP_NOZORDER);

        if (cw >= 540) {
            if (m_editCpuThrottle)  SetWindowPos(m_editCpuThrottle,  nullptr, cx, cy + 260, 90, 28, SWP_NOZORDER);
            if (m_btnApplyThrottle) SetWindowPos(m_btnApplyThrottle, nullptr, cx + 105, cy + 259, 150, 30, SWP_NOZORDER);
            if (m_btnCpuAdvanced)   SetWindowPos(m_btnCpuAdvanced,   nullptr, cx + 265, cy + 259, (std::min)(cw - 265, 230), 30, SWP_NOZORDER);
            if (m_lblThrottleStatus)SetWindowPos(m_lblThrottleStatus,nullptr, cx, cy + 300, cw, 24, SWP_NOZORDER);
        } else {
            if (m_editCpuThrottle)  SetWindowPos(m_editCpuThrottle,  nullptr, cx, cy + 260, 90, 28, SWP_NOZORDER);
            if (m_btnApplyThrottle) SetWindowPos(m_btnApplyThrottle, nullptr, cx + 105, cy + 259, 150, 30, SWP_NOZORDER);
            if (m_btnCpuAdvanced)   SetWindowPos(m_btnCpuAdvanced,   nullptr, cx, cy + 296, (std::min)(cw, 280), 30, SWP_NOZORDER);
            if (m_lblThrottleStatus)SetWindowPos(m_lblThrottleStatus,nullptr, cx, cy + 334, cw, 24, SWP_NOZORDER);
        }
        break;
    }

    case 3: { // GPU
        if (m_tab3Controls.size() > 0) SetWindowPos(m_tab3Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        int gpuListH = (std::max)(ch - 120, 120);
        if (m_listGpu)        SetWindowPos(m_listGpu,        nullptr, cx, cy + 36, cw, gpuListH, SWP_NOZORDER);
        int botGpuY = cy + 36 + gpuListH + 10;
        if (m_lblGpuDocSpecs) SetWindowPos(m_lblGpuDocSpecs, nullptr, cx, botGpuY, cw, 28, SWP_NOZORDER);
        if (m_btnGpuAdvanced) SetWindowPos(m_btnGpuAdvanced, nullptr, cx, botGpuY + 34, (std::min)(cw, 520), 32, SWP_NOZORDER);

        if (m_listGpu) {
            int c0 = (cw > 550) ? (cw - 360) : 220;
            ListView_SetColumnWidth(m_listGpu, 0, (std::max)(c0, 180));
            ListView_SetColumnWidth(m_listGpu, 1, 120);
            ListView_SetColumnWidth(m_listGpu, 2, 120);
            ListView_SetColumnWidth(m_listGpu, 3, 100);
        }
        break;
    }

    case 4: { // RAM
        if (m_tab4Controls.size() > 0) SetWindowPos(m_tab4Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblRamTotal)    SetWindowPos(m_lblRamTotal,    nullptr, cx, cy + 36, cw, 22, SWP_NOZORDER);
        if (m_lblRamUsed)     SetWindowPos(m_lblRamUsed,     nullptr, cx, cy + 64, cw, 22, SWP_NOZORDER);
        if (m_lblRamFree)     SetWindowPos(m_lblRamFree,     nullptr, cx, cy + 92, cw, 22, SWP_NOZORDER);
        if (m_lblRamLoad)     SetWindowPos(m_lblRamLoad,     nullptr, cx, cy + 120, cw, 22, SWP_NOZORDER);
        if (m_progRamLoad)    SetWindowPos(m_progRamLoad,    nullptr, cx, cy + 148, (std::min)(cw, 500), 18, SWP_NOZORDER);
        if (m_btnOptimizeRam) SetWindowPos(m_btnOptimizeRam, nullptr, cx, cy + 182, 220, 34, SWP_NOZORDER);
        if (m_lblRamOptStatus)SetWindowPos(m_lblRamOptStatus,nullptr, cx, cy + 226, cw, 24, SWP_NOZORDER);
        if (m_tab4Controls.size() > 8) SetWindowPos(m_tab4Controls[8], nullptr, cx, cy + 264, cw, 22, SWP_NOZORDER);
        if (m_lblBoardInfo)   SetWindowPos(m_lblBoardInfo,   nullptr, cx, cy + 294, cw, 66, SWP_NOZORDER);
        if (m_lblBiosInfo)    SetWindowPos(m_lblBiosInfo,    nullptr, cx, cy + 364, cw, 44, SWP_NOZORDER);
        int stickH = (std::max)(ch - 420, 100);
        if (m_lblRamSticks)   SetWindowPos(m_lblRamSticks,   nullptr, cx, cy + 412, cw, stickH, SWP_NOZORDER);
        break;
    }

    case 5: { // Energia
        if (m_tab5Controls.size() > 0) SetWindowPos(m_tab5Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblActivePowerScheme) SetWindowPos(m_lblActivePowerScheme, nullptr, cx, cy + 36, cw, 22, SWP_NOZORDER);
        if (m_lblBatteryStatus)     SetWindowPos(m_lblBatteryStatus,     nullptr, cx, cy + 64, cw, 22, SWP_NOZORDER);
        
        // m_tab5Controls[3] é o label "Planos Registrados no Sistema:"
        if (m_tab5Controls.size() > 3) SetWindowPos(m_tab5Controls[3], nullptr, cx, cy + 96, cw, 20, SWP_NOZORDER);

        int yP = cy + 122;
        if (cw >= 560) {
            int comboW = (std::min)(cw - 200, 400);
            if (m_comboPowerSchemes) SetWindowPos(m_comboPowerSchemes, nullptr, cx, yP + 2, comboW, 150, SWP_NOZORDER);
            if (m_btnSetPowerScheme) SetWindowPos(m_btnSetPowerScheme, nullptr, cx + comboW + 15, yP, 180, 32, SWP_NOZORDER);
            yP += 42;
        } else {
            if (m_comboPowerSchemes) SetWindowPos(m_comboPowerSchemes, nullptr, cx, yP + 2, cw, 150, SWP_NOZORDER);
            if (m_btnSetPowerScheme) SetWindowPos(m_btnSetPowerScheme, nullptr, cx, yP + 36, (std::min)(cw, 220), 32, SWP_NOZORDER);
            yP += 76;
        }
        if (m_lblPowerStatus) SetWindowPos(m_lblPowerStatus, nullptr, cx, yP, cw, 24, SWP_NOZORDER);
        break;
    }

    case 6: { // Processos
        if (m_tab6Controls.size() > 0) SetWindowPos(m_tab6Controls[0], nullptr, cx, cy, (std::max)(cw - 320, 200), 26, SWP_NOZORDER);
        if (m_btnRefreshProc)  SetWindowPos(m_btnRefreshProc,  nullptr, cx + cw - 305, cy, 135, 28, SWP_NOZORDER);
        if (m_btnOpenServices) SetWindowPos(m_btnOpenServices, nullptr, cx + cw - 165, cy, 165, 28, SWP_NOZORDER);

        bool wideMode = (cw >= 720);
        int procListH = wideMode ? (std::max)(ch - 110, 150) : (std::max)(ch - 148, 120);
        if (m_listProcesses)  SetWindowPos(m_listProcesses,  nullptr, cx, cy + 34, cw, procListH, SWP_NOZORDER);

        int btnY = cy + 34 + procListH + 10;
        if (wideMode) {
            int curX = cx;
            if (m_lblProcPriority) SetWindowPos(m_lblProcPriority, nullptr, curX, btnY + 4, 75, 20, SWP_NOZORDER);
            curX += 80;
            if (m_comboPriority)   SetWindowPos(m_comboPriority,   nullptr, curX, btnY + 2, 125, 160, SWP_NOZORDER);
            curX += 132;
            if (m_btnSetPriority)  SetWindowPos(m_btnSetPriority,  nullptr, curX, btnY, 110, 30, SWP_NOZORDER);
            curX += 120;
            if (m_lblProcAffinity) SetWindowPos(m_lblProcAffinity, nullptr, curX, btnY + 4, 90, 20, SWP_NOZORDER);
            curX += 95;
            if (m_comboAffinity)   SetWindowPos(m_comboAffinity,   nullptr, curX, btnY + 2, 130, 160, SWP_NOZORDER);
            curX += 137;
            if (m_btnSetAffinity)  SetWindowPos(m_btnSetAffinity,  nullptr, curX, btnY, 110, 30, SWP_NOZORDER);
            curX += 118;
            if (m_btnKillProc)     SetWindowPos(m_btnKillProc,     nullptr, curX, btnY, (std::min)(cw - (curX - cx), 100), 30, SWP_NOZORDER);

            if (m_lblProcStatus)   SetWindowPos(m_lblProcStatus,   nullptr, cx, btnY + 36, cw, 22, SWP_NOZORDER);
        } else {
            if (m_lblProcPriority) SetWindowPos(m_lblProcPriority, nullptr, cx, btnY + 4, 75, 20, SWP_NOZORDER);
            if (m_comboPriority)   SetWindowPos(m_comboPriority,   nullptr, cx + 80, btnY + 2, 130, 160, SWP_NOZORDER);
            if (m_btnSetPriority)  SetWindowPos(m_btnSetPriority,  nullptr, cx + 218, btnY, 120, 30, SWP_NOZORDER);

            if (m_btnKillProc)     SetWindowPos(m_btnKillProc,     nullptr, cx + 348, btnY, 120, 30, SWP_NOZORDER);

            int row2Y = btnY + 36;
            if (m_lblProcAffinity) SetWindowPos(m_lblProcAffinity, nullptr, cx, row2Y + 4, 90, 20, SWP_NOZORDER);
            if (m_comboAffinity)   SetWindowPos(m_comboAffinity,   nullptr, cx + 95, row2Y + 2, 150, 160, SWP_NOZORDER);
            if (m_btnSetAffinity)  SetWindowPos(m_btnSetAffinity,  nullptr, cx + 252, row2Y, 120, 30, SWP_NOZORDER);

            if (m_lblProcStatus)   SetWindowPos(m_lblProcStatus,   nullptr, cx, row2Y + 36, cw, 22, SWP_NOZORDER);
        }

        if (m_listProcesses) {
            ListView_SetColumnWidth(m_listProcesses, 0, 70);
            int nameW = cw - 70 - 110 - 130 - 120;
            ListView_SetColumnWidth(m_listProcesses, 1, (std::max)(nameW, 160));
            ListView_SetColumnWidth(m_listProcesses, 2, 110);
            ListView_SetColumnWidth(m_listProcesses, 3, 130);
            ListView_SetColumnWidth(m_listProcesses, 4, 120);
        }
        break;
    }

    case 7: { // Serviços do Windows
        if (m_tab7Controls.size() > 0) SetWindowPos(m_tab7Controls[0], nullptr, cx, cy, (std::max)(cw - 300, 200), 26, SWP_NOZORDER);
        int searchW = (std::min)(cw - 290, 260);
        if (m_editSvcSearch)         SetWindowPos(m_editSvcSearch, nullptr, cx, cy + 32, searchW, 28, SWP_NOZORDER);
        if (m_btnRefreshSvcList)     SetWindowPos(m_btnRefreshSvcList, nullptr, cx + searchW + 8, cy + 32, 120, 28, SWP_NOZORDER);
        if (m_btnConfigSvcTriggers)  SetWindowPos(m_btnConfigSvcTriggers, nullptr, cx + searchW + 136, cy + 32, 150, 28, SWP_NOZORDER);

        int svcListH = (std::max)(ch - 190, 120);
        if (m_listServices) SetWindowPos(m_listServices, nullptr, cx, cy + 66, cw, svcListH, SWP_NOZORDER);

        int botY = cy + 66 + svcListH + 10;
        int comboW = (std::min)(cw - 165, 340);
        if (comboW < 180) comboW = 180;
        if (m_lblSvcRulePrompt)  SetWindowPos(m_lblSvcRulePrompt, nullptr, cx, botY + 4, 155, 20, SWP_NOZORDER);
        if (m_comboSvcRule)      SetWindowPos(m_comboSvcRule, nullptr, cx + 160, botY + 2, comboW, 160, SWP_NOZORDER);

        int row2Y = botY + 34;
        int gap = 8;
        int bW = (cw - (gap * 4)) / 5;
        if (bW < 80) bW = 80;
        if (m_btnApplySvcRule)     SetWindowPos(m_btnApplySvcRule, nullptr, cx, row2Y, bW, 28, SWP_NOZORDER);
        if (m_btnApplyAllSvcRules) SetWindowPos(m_btnApplyAllSvcRules, nullptr, cx + (bW + gap), row2Y, bW, 28, SWP_NOZORDER);
        if (m_btnPauseSvcNow)      SetWindowPos(m_btnPauseSvcNow, nullptr, cx + (bW + gap) * 2, row2Y, bW, 28, SWP_NOZORDER);
        if (m_btnResumeSvcNow)     SetWindowPos(m_btnResumeSvcNow, nullptr, cx + (bW + gap) * 3, row2Y, bW, 28, SWP_NOZORDER);
        if (m_btnRestoreAllSvcs)   SetWindowPos(m_btnRestoreAllSvcs, nullptr, cx + (bW + gap) * 4, row2Y, bW, 28, SWP_NOZORDER);

        if (m_lblSvcStatus)        SetWindowPos(m_lblSvcStatus, nullptr, cx, row2Y + 34, cw, 22, SWP_NOZORDER);

        if (m_listServices) {
            ListView_SetColumnWidth(m_listServices, 0, 140);
            int descW = cw - 140 - 100 - 160 - 130;
            ListView_SetColumnWidth(m_listServices, 1, (std::max)(descW, 160));
            ListView_SetColumnWidth(m_listServices, 2, 100);
            ListView_SetColumnWidth(m_listServices, 3, 160);
            ListView_SetColumnWidth(m_listServices, 4, 130);
        }
        break;
    }

    case 8: { // Jogos
        if (m_tab8Controls.size() > 0) SetWindowPos(m_tab8Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_chkAutoGame)    SetWindowPos(m_chkAutoGame,    nullptr, cx, cy + 34, cw, 22, SWP_NOZORDER);

        bool wideMode = (cw >= 680);
        int gameListH = wideMode ? (std::max)(ch - 150, 120) : (std::max)(ch - 195, 100);
        if (m_listGames)      SetWindowPos(m_listGames,      nullptr, cx, cy + 62, cw, gameListH, SWP_NOZORDER);

        int botY = cy + 62 + gameListH + 10;
        if (m_tab8Controls.size() > 3) SetWindowPos(m_tab8Controls[3], nullptr, cx, botY, cw, 20, SWP_NOZORDER);

        if (wideMode) {
            if (m_editGameExe)    SetWindowPos(m_editGameExe,    nullptr, cx, botY + 22, cw - 420, 28, SWP_NOZORDER);
            if (m_btnAddGame)     SetWindowPos(m_btnAddGame,     nullptr, cx + cw - 410, botY + 21, 130, 30, SWP_NOZORDER);
            if (m_btnEditGame)    SetWindowPos(m_btnEditGame,    nullptr, cx + cw - 275, botY + 21, 130, 30, SWP_NOZORDER);
            if (m_btnRemoveGame)  SetWindowPos(m_btnRemoveGame,  nullptr, cx + cw - 140, botY + 21, 140, 30, SWP_NOZORDER);

            if (m_btnToggleGameMode) SetWindowPos(m_btnToggleGameMode, nullptr, cx, botY + 58, 220, 30, SWP_NOZORDER);
            if (m_lblGameStatus)  SetWindowPos(m_lblGameStatus,  nullptr, cx + 230, botY + 62, cw - 230, 22, SWP_NOZORDER);
        } else {
            if (m_editGameExe)    SetWindowPos(m_editGameExe,    nullptr, cx, botY + 22, cw, 26, SWP_NOZORDER);
            int gBtnW = (cw - 16) / 3;
            if (m_btnAddGame)     SetWindowPos(m_btnAddGame,     nullptr, cx, botY + 54, gBtnW, 28, SWP_NOZORDER);
            if (m_btnEditGame)    SetWindowPos(m_btnEditGame,    nullptr, cx + gBtnW + 8, botY + 54, gBtnW, 28, SWP_NOZORDER);
            if (m_btnRemoveGame)  SetWindowPos(m_btnRemoveGame,  nullptr, cx + (gBtnW + 8) * 2, botY + 54, gBtnW, 28, SWP_NOZORDER);

            if (m_btnToggleGameMode) SetWindowPos(m_btnToggleGameMode, nullptr, cx, botY + 88, (std::min)(cw, 200), 28, SWP_NOZORDER);
            if (m_lblGameStatus)  SetWindowPos(m_lblGameStatus,  nullptr, cx + 210, botY + 92, cw - 210, 22, SWP_NOZORDER);
        }

        if (m_listGames) {
            ListView_SetColumnWidth(m_listGames, 0, (std::max)(cw - 220, 180));
            ListView_SetColumnWidth(m_listGames, 1, 200);
        }
        break;
    }

    case 9: { // Monitoramento
        if (m_tab9Controls.size() > 0) SetWindowPos(m_tab9Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblMonRealtime) SetWindowPos(m_lblMonRealtime, nullptr, cx, cy + 30, cw, 20, SWP_NOZORDER);
        if (m_lblMonCpuStats) SetWindowPos(m_lblMonCpuStats, nullptr, cx, cy + 52, cw, 20, SWP_NOZORDER);
        if (m_lblMonRamStats) SetWindowPos(m_lblMonRamStats, nullptr, cx, cy + 74, cw, 20, SWP_NOZORDER);
        if (m_tab9Controls.size() > 4) SetWindowPos(m_tab9Controls[4], nullptr, cx, cy + 98, cw, 20, SWP_NOZORDER);
        if (m_lblMonTrend)    SetWindowPos(m_lblMonTrend,    nullptr, cx, cy + 120, cw, 26, SWP_NOZORDER);
        if (m_tab9Controls.size() > 6) SetWindowPos(m_tab9Controls[6], nullptr, cx, cy + 152, cw, 20, SWP_NOZORDER);

        int botY = cy + ch - 44;
        if (m_listMonHistory) SetWindowPos(m_listMonHistory, nullptr, cx, cy + 176, cw, (std::max)(botY - (cy + 184), 100), SWP_NOZORDER);

        if (cw >= 560) {
            if (m_btnRefreshMonHistory) SetWindowPos(m_btnRefreshMonHistory, nullptr, cx, botY, 150, 30, SWP_NOZORDER);
            if (m_btnClearMonHistory)   SetWindowPos(m_btnClearMonHistory,   nullptr, cx + 160, botY, 140, 30, SWP_NOZORDER);
            if (m_lblMonStatus)         SetWindowPos(m_lblMonStatus,         nullptr, cx + 310, botY + 4, cw - 310, 24, SWP_NOZORDER);
        } else {
            if (m_btnRefreshMonHistory) SetWindowPos(m_btnRefreshMonHistory, nullptr, cx, botY - 14, 140, 28, SWP_NOZORDER);
            if (m_btnClearMonHistory)   SetWindowPos(m_btnClearMonHistory,   nullptr, cx + 150, botY - 14, 140, 28, SWP_NOZORDER);
            if (m_lblMonStatus)         SetWindowPos(m_lblMonStatus,         nullptr, cx, botY + 18, cw, 22, SWP_NOZORDER);
        }

        if (m_listMonHistory) {
            int colRemaining = cw - 70 - 200;
            int colSubW = (colRemaining > 300) ? (colRemaining / 3) : 130;
            ListView_SetColumnWidth(m_listMonHistory, 0, 70);
            ListView_SetColumnWidth(m_listMonHistory, 1, 200);
            ListView_SetColumnWidth(m_listMonHistory, 2, colSubW);
            ListView_SetColumnWidth(m_listMonHistory, 3, colSubW);
            ListView_SetColumnWidth(m_listMonHistory, 4, (std::max)(colSubW, 110));
        }
        break;
    }

    case 10: { // Térmica
        if (m_tab10Controls.size() > 0) SetWindowPos(m_tab10Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblThermalCurrent)     SetWindowPos(m_lblThermalCurrent,     nullptr, cx, cy + 36, cw, 22, SWP_NOZORDER);
        if (m_lblThermalSensorStatus)SetWindowPos(m_lblThermalSensorStatus,nullptr, cx, cy + 64, cw, 22, SWP_NOZORDER);
        if (m_chkThermalEnabled)     SetWindowPos(m_chkThermalEnabled,     nullptr, cx, cy + 96, cw, 24, SWP_NOZORDER);
        
        if (m_tab10Controls.size() > 4) SetWindowPos(m_tab10Controls[4], nullptr, cx, cy + 130, (std::min)(cw, 250), 20, SWP_NOZORDER);

        if (cw >= 560) {
            if (m_editThermalLimit)      SetWindowPos(m_editThermalLimit,      nullptr, cx + 255, cy + 128, 80, 26, SWP_NOZORDER);
            if (m_btnApplyThermal)       SetWindowPos(m_btnApplyThermal,       nullptr, cx + 345, cy + 126, 200, 30, SWP_NOZORDER);
            if (m_lblThermalActionStatus)SetWindowPos(m_lblThermalActionStatus,nullptr, cx, cy + 170, cw, 40, SWP_NOZORDER);
            if (m_btnOpenFansGraph)      SetWindowPos(m_btnOpenFansGraph,      nullptr, cx, cy + 225, (std::min)(cw, 540), 34, SWP_NOZORDER);
            if (m_btnOpenFansConfig)     SetWindowPos(m_btnOpenFansConfig,     nullptr, cx, cy + 266, (std::min)(cw, 540), 30, SWP_NOZORDER);
        } else {
            if (m_editThermalLimit)      SetWindowPos(m_editThermalLimit,      nullptr, cx + 255, cy + 128, 80, 26, SWP_NOZORDER);
            if (m_btnApplyThermal)       SetWindowPos(m_btnApplyThermal,       nullptr, cx, cy + 162, (std::min)(cw, 220), 30, SWP_NOZORDER);
            if (m_lblThermalActionStatus)SetWindowPos(m_lblThermalActionStatus,nullptr, cx, cy + 200, cw, 40, SWP_NOZORDER);
            if (m_btnOpenFansGraph)      SetWindowPos(m_btnOpenFansGraph,      nullptr, cx, cy + 248, cw, 34, SWP_NOZORDER);
            if (m_btnOpenFansConfig)     SetWindowPos(m_btnOpenFansConfig,     nullptr, cx, cy + 288, cw, 30, SWP_NOZORDER);
        }
        break;
    }

    case 11: { // Configurações Gerais
        if (m_tab11Controls.size() > 0) SetWindowPos(m_tab11Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_chkStartWindows)        SetWindowPos(m_chkStartWindows,        nullptr, cx, cy + 32, cw, 22, SWP_NOZORDER);
        if (m_chkStartWindowsAdmin)   SetWindowPos(m_chkStartWindowsAdmin,   nullptr, cx, cy + 56, cw, 22, SWP_NOZORDER);
        if (m_chkStartMinimized)      SetWindowPos(m_chkStartMinimized,      nullptr, cx, cy + 80, cw, 22, SWP_NOZORDER);
        if (m_chkCloseToTray)         SetWindowPos(m_chkCloseToTray,         nullptr, cx, cy + 104, cw, 22, SWP_NOZORDER);
        if (m_chkDisableTelemetry)    SetWindowPos(m_chkDisableTelemetry,    nullptr, cx, cy + 128, cw, 22, SWP_NOZORDER);
        if (m_chkSmartGamingServices) SetWindowPos(m_chkSmartGamingServices, nullptr, cx, cy + 152, cw, 22, SWP_NOZORDER);
        
        if (m_tab11Controls.size() > 7) SetWindowPos(m_tab11Controls[7], nullptr, cx, cy + 182, (std::min)(cw, 320), 22, SWP_NOZORDER);
        if (m_comboInterval)          SetWindowPos(m_comboInterval,          nullptr, cx + 330, cy + 180, (std::min)(cw - 330, 180), 100, SWP_NOZORDER);

        if (m_btnSaveConfig)          SetWindowPos(m_btnSaveConfig,          nullptr, cx, cy + 220, 170, 34, SWP_NOZORDER);
        if (m_btnResetDefaults)       SetWindowPos(m_btnResetDefaults,       nullptr, cx + 180, cy + 220, 170, 34, SWP_NOZORDER);
        if (m_tab11Controls.size() > 11) SetWindowPos(m_tab11Controls[11], nullptr, cx, cy + 262, (std::min)(cw, 460), 34, SWP_NOZORDER);
        if (m_tab11Controls.size() > 12) SetWindowPos(m_tab11Controls[12], nullptr, cx, cy + 302, (std::min)(cw, 460), 32, SWP_NOZORDER);
        if (m_lblConfigStatus)        SetWindowPos(m_lblConfigStatus,        nullptr, cx, cy + 342, cw, 24, SWP_NOZORDER);
        break;
    }

    case 12: { // Diagnóstico & Logs
        if (m_tab12Controls.size() > 0) SetWindowPos(m_tab12Controls[0], nullptr, cx, cy, cw, 26, SWP_NOZORDER);
        if (m_lblDiagElevated) SetWindowPos(m_lblDiagElevated, nullptr, cx, cy + 36, (cw - 16) / 2, 20, SWP_NOZORDER);
        if (m_lblDiagDb)       SetWindowPos(m_lblDiagDb,       nullptr, cx + (cw - 16) / 2 + 16, cy + 36, (cw - 16) / 2, 20, SWP_NOZORDER);

        int botY = cy + ch - 50;
        int logListH = (std::max)(botY - (cy + 74), 100);
        if (m_listLogs)        SetWindowPos(m_listLogs,        nullptr, cx, cy + 66, cw, logListH, SWP_NOZORDER);

        if (cw >= 700) {
            if (m_btnRefreshLogs)  SetWindowPos(m_btnRefreshLogs,  nullptr, cx, botY, 140, 32, SWP_NOZORDER);
            if (m_btnCopyDiag)     SetWindowPos(m_btnCopyDiag,     nullptr, cx + 150, botY, 170, 32, SWP_NOZORDER);
            if (m_tab12Controls.size() > 6) SetWindowPos(m_tab12Controls[6], nullptr, cx + 330, botY, 210, 32, SWP_NOZORDER);
            if (m_lblDiagStatus)   SetWindowPos(m_lblDiagStatus,   nullptr, cx + 550, botY + 5, cw - 550, 22, SWP_NOZORDER);
        } else {
            int bW = (cw - 16) / 3;
            if (m_btnRefreshLogs)  SetWindowPos(m_btnRefreshLogs,  nullptr, cx, botY - 14, bW, 28, SWP_NOZORDER);
            if (m_btnCopyDiag)     SetWindowPos(m_btnCopyDiag,     nullptr, cx + bW + 8, botY - 14, bW, 28, SWP_NOZORDER);
            if (m_tab12Controls.size() > 6) SetWindowPos(m_tab12Controls[6], nullptr, cx + (bW + 8) * 2, botY - 14, bW, 28, SWP_NOZORDER);
            if (m_lblDiagStatus)   SetWindowPos(m_lblDiagStatus,   nullptr, cx, botY + 20, cw, 22, SWP_NOZORDER);
        }
        break;
    }

    case 13: { // Sobre & Código Aberto
        // Invariante de criacao: indice 0 = titulo, 1..N-1 = linhas do corpo, os 5 ultimos = bloco Pix.
        const int tailCount = 5;
        const int bodyLast = (int)m_tab13Controls.size() - tailCount - 1;
        if (m_lblAboutTitle) SetWindowPos(m_lblAboutTitle, nullptr, cx, cy, (std::min)(cw, 820), 26, SWP_NOZORDER);
        int y = cy + 34;
        for (int i = 1; i <= bodyLast; ++i) {
            SetWindowPos(m_tab13Controls[i], nullptr, cx, y, (std::min)(cw, 820), 20, SWP_NOZORDER);
            y += 21;
        }
        int pixY = cy + ch - 112;
        if (m_lblPixHead)                    SetWindowPos(m_lblPixHead, nullptr, cx, pixY, (std::min)(cw, 820), 20, SWP_NOZORDER);
        if (bodyLast + 1 < (int)m_tab13Controls.size())
            SetWindowPos(m_tab13Controls[bodyLast + 1], nullptr, cx, pixY + 22, (std::min)(cw, 820), 20, SWP_NOZORDER);
        if (cw >= 700) {
            if (m_lblPixKey)   SetWindowPos(m_lblPixKey,   nullptr, cx, pixY + 56, 330, 26, SWP_NOZORDER);
            if (m_btnCopyPix)  SetWindowPos(m_btnCopyPix,  nullptr, cx + 346, pixY + 54, 150, 30, SWP_NOZORDER);
            if (m_lblAboutStatus) SetWindowPos(m_lblAboutStatus, nullptr, cx + 512, pixY + 59, (std::max)(cw - 516, 120), 22, SWP_NOZORDER);
        } else {
            if (m_lblPixKey)   SetWindowPos(m_lblPixKey,   nullptr, cx, pixY + 50, 330, 26, SWP_NOZORDER);
            if (m_btnCopyPix)  SetWindowPos(m_btnCopyPix,  nullptr, cx + 340, pixY + 48, 150, 28, SWP_NOZORDER);
            if (m_lblAboutStatus) SetWindowPos(m_lblAboutStatus, nullptr, cx, pixY + 82, cw, 22, SWP_NOZORDER);
        }
        break;
    }
    }
}

void MainWindow::CreateTabControls(HWND hwnd) {
    // Tab Control Principal
    m_tab = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_CLIPSIBLINGS,
                          12, 12, 950, 32, hwnd, (HMENU)(UINT_PTR)100, m_hInstance, nullptr);
    SendMessageW(m_tab, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
    ShowWindow(m_tab, SW_HIDE); // Oculto: a navegacao e feita via Sidebar Moderna

    const wchar_t* tabTitles[] = {
        L"Painel Geral", L"Perfis", L"CPU", L"GPU", L"RAM",
        L"Energia", L"Processos", L"Servicos", L"Jogos", L"Monitoramento", L"Termica",
        L"Configuracoes", L"Diagnostico", L"Sobre"
    };

    for (int i = 0; i < (int)(sizeof(tabTitles) / sizeof(tabTitles[0])); ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)tabTitles[i];
        TabCtrl_InsertItem(m_tab, i, &ti);
    }

    const int startX = 270;

    auto regControl = [this](std::vector<HWND>& tabList, HWND h, HFONT hFont = nullptr) {
        if (!hFont) hFont = m_hFontNormal;
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        ui::Theme::ApplyDarkExplorerTheme(h);
        tabList.push_back(h);
        return h;
    };

    // ==========================================
    // ABA 0: PAINEL GERAL (DASHBOARD)
    // ==========================================
    regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"=== ESTADO GERAL DO SISTEMA E HARDWARE ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 500, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    
    m_lblDashCpu = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Processador: Detectando...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_progDashCpu = regControl(m_tab0Controls, CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, startX, 120, 360, 16, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_progDashCpu, PBM_SETRANGE32, 0, 100);

    m_lblDashRam = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Memoria RAM: Medindo...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 145, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_progDashRam = regControl(m_tab0Controls, CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, startX, 170, 360, 16, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_progDashRam, PBM_SETRANGE32, 0, 100);

    m_lblDashGpu = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Placa de Video (GPU): Detectando...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 198, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDashPower = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Plano de Energia Ativo: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 226, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDashProfile = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Perfil em Execucao: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 254, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDashTemp = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Temperatura CPU: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 282, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDashGame = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Modo Jogo: Nenhum jogo em execucao", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 310, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDashDocCpu = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Base de Conhecimento: Verificando especificacoes oficiais...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 340, 680, 24, hwnd, nullptr, m_hInstance, nullptr));

    m_lblDashStatus = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Opit-Core — Donationware: sem ativação, sem validação de licença", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 368, 680, 34, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);

    regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Atalhos Rapidos de Perfil e Coordenacao:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 400, 320, 20, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Modo Eco", WS_CHILD | BS_PUSHBUTTON, startX, 423, 110, 30, hwnd, (HMENU)(UINT_PTR)ID_TRAY_PROFILE_ECO, m_hInstance, nullptr));
    regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Balanceado", WS_CHILD | BS_PUSHBUTTON, startX + 115, 423, 110, 30, hwnd, (HMENU)(UINT_PTR)ID_TRAY_PROFILE_BAL, m_hInstance, nullptr));
    regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Alto Desempenho", WS_CHILD | BS_PUSHBUTTON, startX + 230, 423, 130, 30, hwnd, (HMENU)(UINT_PTR)ID_TRAY_PROFILE_HIGH, m_hInstance, nullptr));
    m_btnDashDeepEco = regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Economia Maxima", WS_CHILD | BS_PUSHBUTTON, startX + 365, 423, 140, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_DEEP_ECO, m_hInstance, nullptr));
    m_btnDashGameMode = regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Modo Jogo", WS_CHILD | BS_PUSHBUTTON, startX + 510, 423, 120, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_TOGGLE_GAMEMODE, m_hInstance, nullptr));

    m_lblDashCoordinator = regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Coordenador do Sistema: Normal", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 458, 680, 20, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);

    regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"Abrir Painel Unificado (Tela Centralizada)", WS_CHILD | BS_PUSHBUTTON, startX, 482, 340, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_UNIFIED, m_hInstance, nullptr), m_hFontBold);
    regControl(m_tab0Controls, CreateWindowW(L"BUTTON", L"IA Copilot / Diagnostico...", WS_CHILD | BS_PUSHBUTTON, startX + 350, 482, 220, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_AI, m_hInstance, nullptr), m_hFontBold);

    regControl(m_tab0Controls, CreateWindowW(L"STATIC", L"Nota: O Opit-Core opera diretamente sobre as politicas nativas do Windows sem instalar drivers invasivos.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 520, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 1: PERFIS DE DESEMPENHO
    // ==========================================
    regControl(m_tab1Controls, CreateWindowW(L"STATIC", L"=== SELECAO DE PERFIL DE DESEMPENHO DETERMINISTICO ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    regControl(m_tab1Controls, CreateWindowW(L"STATIC", L"Escolha o perfil para aplicar parametros de frequencia, estado do processador e energia:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 95, 600, 20, hwnd, nullptr, m_hInstance, nullptr));
    
    m_comboProfiles = regControl(m_tab1Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, startX, 125, 280, 150, hwnd, (HMENU)(UINT_PTR)ID_COMBO_PROFILE_SEL, m_hInstance, nullptr));
    SendMessageW(m_comboProfiles, CB_ADDSTRING, 0, (LPARAM)L"Eco (Economia de Energia - CPU Cap 75%)");
    SendMessageW(m_comboProfiles, CB_ADDSTRING, 0, (LPARAM)L"Balanceado (Padrao Equilibrado - CPU 100%)");
    SendMessageW(m_comboProfiles, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (Maximo Poder - Sem Amarras)");
    SendMessageW(m_comboProfiles, CB_SETCURSEL, 1, 0);

    m_btnApplyProfile = regControl(m_tab1Controls, CreateWindowW(L"BUTTON", L"Aplicar Perfil Agora", WS_CHILD | BS_PUSHBUTTON, startX + 300, 123, 160, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_APPLY_PROFILE, m_hInstance, nullptr));
    m_lblProfileDesc = regControl(m_tab1Controls, CreateWindowW(L"STATIC", L"Descricao: Perfil equilibrado otimiza a latencia mantendo temperaturas estaveis.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 180, 680, 60, hwnd, nullptr, m_hInstance, nullptr));
    m_lblProfileStatus = regControl(m_tab1Controls, CreateWindowW(L"STATIC", L"Status: Pronto para aplicar.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 260, 600, 22, hwnd, nullptr, m_hInstance, nullptr));

    regControl(m_tab1Controls, CreateWindowW(L"STATIC", L"Politicas Deterministicas:\r\n- Eco: Aplica plano Economia de Energia nativo, limita maximo do processador em 75% e reduz aquecimento.\r\n- Balanceado: Aplica plano Equilibrado do Windows, permite frequencia maxima sob demanda com idle eficiente.\r\n- Alto Desempenho: Ativa plano Alto Desempenho, mantem frequencias estaveis e desativa suspensao de nucleos.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 310, 680, 90, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 2: CPU
    // ==========================================
    regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"=== DETALHES, ARQUITETURA E CONTROLE DA CPU ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblCpuInfo = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Modelo: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_lblCpuCores = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Nucleos Fisicos / Threads: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 125, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblCpuArch = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Arquitetura: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 155, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblCpuDocSpecs = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Especificacoes Documentadas (Base de Conhecimento): Consultando...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 185, 680, 45, hwnd, nullptr, m_hInstance, nullptr));

    m_lblCpuUsage = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Uso em Tempo Real: 0.0%", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 240, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_progCpuUsage = regControl(m_tab2Controls, CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, startX, 265, 450, 18, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_progCpuUsage, PBM_SETRANGE32, 0, 100);

    regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Limite Maximo do Estado do Processador (Throttling % 20 a 100):", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 310, 500, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_editCpuThrottle = regControl(m_tab2Controls, CreateWindowW(L"EDIT", L"100", WS_CHILD | WS_BORDER | ES_NUMBER, startX, 335, 100, 26, hwnd, nullptr, m_hInstance, nullptr));
    m_btnApplyThrottle = regControl(m_tab2Controls, CreateWindowW(L"BUTTON", L"Salvar Limite CPU", WS_CHILD | BS_PUSHBUTTON, startX + 115, 333, 150, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_APPLY_THROTTLE, m_hInstance, nullptr));
    m_btnCpuAdvanced = regControl(m_tab2Controls, CreateWindowW(L"BUTTON", L"Controles Avancados de CPU...", WS_CHILD | BS_PUSHBUTTON, startX + 275, 333, 220, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_CPU_ADVANCED, m_hInstance, nullptr));
    m_lblThrottleStatus = regControl(m_tab2Controls, CreateWindowW(L"STATIC", L"Ajuste aplica diretamente o teto de frequencia nas politicas AC/DC do Windows Powrprof.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 375, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 3: GPU
    // ==========================================
    regControl(m_tab3Controls, CreateWindowW(L"STATIC", L"=== ADAPTADORES GRAFICOS DETECTADOS (DXGI) ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_listGpu = regControl(m_tab3Controls, CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | WS_BORDER, startX, 95, 680, 250, hwnd, nullptr, m_hInstance, nullptr));
    ListView_SetExtendedListViewStyle(m_listGpu, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvcGpu{};
    lvcGpu.mask = LVCF_TEXT | LVCF_WIDTH;
    lvcGpu.cx = 320; lvcGpu.pszText = (LPWSTR)L"Descricao do Adaptador"; ListView_InsertColumn(m_listGpu, 0, &lvcGpu);
    lvcGpu.cx = 170; lvcGpu.pszText = (LPWSTR)L"VRAM Dedicada (MB)"; ListView_InsertColumn(m_listGpu, 1, &lvcGpu);
    lvcGpu.cx = 170; lvcGpu.pszText = (LPWSTR)L"Memoria Compartilhada"; ListView_InsertColumn(m_listGpu, 2, &lvcGpu);
    lvcGpu.cx = 150; lvcGpu.pszText = (LPWSTR)L"Tipo"; ListView_InsertColumn(m_listGpu, 3, &lvcGpu);

    m_lblGpuDocSpecs = regControl(m_tab3Controls, CreateWindowW(L"STATIC", L"Especificacoes Documentadas na Base de Conhecimento: Verificando...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 360, 680, 45, hwnd, nullptr, m_hInstance, nullptr));
    m_btnGpuAdvanced = regControl(m_tab3Controls, CreateWindowW(L"BUTTON", L"Controles Avancados de GPU (Frequencias, Power Limit e Perfis)...", WS_CHILD | BS_PUSHBUTTON, startX, 415, 520, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_GPU_ADVANCED, m_hInstance, nullptr), m_hFontBold);

    // ==========================================
    // ABA 4: RAM
    // ==========================================
    regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"=== MEMORIA RAM E ARMAZENAMENTO DO SISTEMA ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblRamTotal = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Total Fisica: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblRamUsed = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Em Uso: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 125, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblRamFree = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Disponivel: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 155, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblRamLoad = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Carga de Memoria: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 185, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);

    m_progRamLoad = regControl(m_tab4Controls, CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, startX, 210, 450, 18, hwnd, nullptr, m_hInstance, nullptr));
    SendMessage(m_progRamLoad, PBM_SETRANGE32, 0, 100);

    m_btnOptimizeRam = regControl(m_tab4Controls, CreateWindowW(L"BUTTON", L"Otimizar Memoria RAM Agora", WS_CHILD | BS_PUSHBUTTON, startX, 250, 230, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPTIMIZE_RAM, m_hInstance, nullptr));
    m_lblRamOptStatus = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Executa EmptyWorkingSet de forma segura para liberar paginas em cache inativas.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 250, 256, 500, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 4: PLACA-MAE, BIOS E PENTES DE MEMORIA (SMBIOS)
    // ==========================================
    regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"=== PLACA-MAE, BIOS E PENTES DE MEMORIA (SMBIOS DO FIRMWARE) ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 290, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblBoardInfo = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Placa-Mae: Consultando firmware SMBIOS...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 320, 680, 66, hwnd, nullptr, m_hInstance, nullptr));
    m_lblBiosInfo = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"BIOS: Consultando firmware SMBIOS...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 390, 680, 44, hwnd, nullptr, m_hInstance, nullptr));
    m_lblRamSticks = regControl(m_tab4Controls, CreateWindowW(L"STATIC", L"Pentes de Memoria: Consultando firmware SMBIOS...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 438, 680, 150, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 5: ENERGIA
    // ==========================================
    regControl(m_tab5Controls, CreateWindowW(L"STATIC", L"=== PLANOS DE ENERGIA DO WINDOWS E ALIMENTACAO ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblActivePowerScheme = regControl(m_tab5Controls, CreateWindowW(L"STATIC", L"Plano Atual: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_lblBatteryStatus = regControl(m_tab5Controls, CreateWindowW(L"STATIC", L"Alimentacao: Verificando...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 125, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    regControl(m_tab5Controls, CreateWindowW(L"STATIC", L"Planos Registrados no Sistema:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 165, 500, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_comboPowerSchemes = regControl(m_tab5Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, startX, 190, 450, 180, hwnd, nullptr, m_hInstance, nullptr));
    m_btnSetPowerScheme = regControl(m_tab5Controls, CreateWindowW(L"BUTTON", L"Ativar Este Plano", WS_CHILD | BS_PUSHBUTTON, startX + 465, 188, 150, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_SET_POWER_SCHEME, m_hInstance, nullptr));
    m_lblPowerStatus = regControl(m_tab5Controls, CreateWindowW(L"STATIC", L"Status: Pronto", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 235, 600, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 6: PROCESSOS
    // ==========================================
    regControl(m_tab6Controls, CreateWindowW(L"STATIC", L"=== GERENCIAMENTO DE PROCESSOS ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_btnRefreshProc = regControl(m_tab6Controls, CreateWindowW(L"BUTTON", L"Atualizar Lista", WS_CHILD | BS_PUSHBUTTON, startX + 360, 58, 140, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_REFRESH_PROC, m_hInstance, nullptr));
    m_btnOpenServices = regControl(m_tab6Controls, CreateWindowW(L"BUTTON", L"Configurar Servicos...", WS_CHILD | BS_PUSHBUTTON, startX + 510, 58, 170, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_SERVICES_CFG, m_hInstance, nullptr), m_hFontBold);

    m_listProcesses = regControl(m_tab6Controls, CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, startX, 92, 680, 330, hwnd, nullptr, m_hInstance, nullptr));
    ListView_SetExtendedListViewStyle(m_listProcesses, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvcProc{};
    lvcProc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvcProc.cx = 85; lvcProc.pszText = (LPWSTR)L"PID"; ListView_InsertColumn(m_listProcesses, 0, &lvcProc);
    lvcProc.cx = 300; lvcProc.pszText = (LPWSTR)L"Nome do Processo"; ListView_InsertColumn(m_listProcesses, 1, &lvcProc);
    lvcProc.cx = 140; lvcProc.pszText = (LPWSTR)L"Memoria (MB)"; ListView_InsertColumn(m_listProcesses, 2, &lvcProc);
    lvcProc.cx = 160; lvcProc.pszText = (LPWSTR)L"Prioridade Atual"; ListView_InsertColumn(m_listProcesses, 3, &lvcProc);
    lvcProc.cx = 140; lvcProc.pszText = (LPWSTR)L"Status Protegido"; ListView_InsertColumn(m_listProcesses, 4, &lvcProc);

    m_lblProcPriority = regControl(m_tab6Controls, CreateWindowW(L"STATIC", L"Prioridade:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 435, 80, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_comboPriority = regControl(m_tab6Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, startX + 85, 432, 150, 120, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_comboPriority, CB_ADDSTRING, 0, (LPARAM)L"Ociosa (Baixa)");
    SendMessageW(m_comboPriority, CB_ADDSTRING, 0, (LPARAM)L"Abaixo do Normal");
    SendMessageW(m_comboPriority, CB_ADDSTRING, 0, (LPARAM)L"Normal");
    SendMessageW(m_comboPriority, CB_ADDSTRING, 0, (LPARAM)L"Acima do Normal");
    SendMessageW(m_comboPriority, CB_ADDSTRING, 0, (LPARAM)L"Alta");
    SendMessageW(m_comboPriority, CB_SETCURSEL, 2, 0);

    m_btnSetPriority = regControl(m_tab6Controls, CreateWindowW(L"BUTTON", L"Aplicar Prioridade", WS_CHILD | BS_PUSHBUTTON, startX + 245, 430, 130, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_SET_PRIORITY, m_hInstance, nullptr));

    m_lblProcAffinity = regControl(m_tab6Controls, CreateWindowW(L"STATIC", L"Afinidade CPU:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 385, 435, 100, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_comboAffinity = regControl(m_tab6Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, startX + 485, 432, 170, 120, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_comboAffinity, CB_ADDSTRING, 0, (LPARAM)L"Todos os Nucleos");
    SendMessageW(m_comboAffinity, CB_ADDSTRING, 0, (LPARAM)L"Nucleos 0 a 3");
    SendMessageW(m_comboAffinity, CB_ADDSTRING, 0, (LPARAM)L"Nucleos Pares (P-Cores)");
    SendMessageW(m_comboAffinity, CB_ADDSTRING, 0, (LPARAM)L"Nucleo 0 Apenas");
    SendMessageW(m_comboAffinity, CB_SETCURSEL, 0, 0);

    m_btnSetAffinity = regControl(m_tab6Controls, CreateWindowW(L"BUTTON", L"Aplicar Afinidade", WS_CHILD | BS_PUSHBUTTON, startX + 665, 430, 120, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_SET_AFFINITY, m_hInstance, nullptr));
    m_btnKillProc = regControl(m_tab6Controls, CreateWindowW(L"BUTTON", L"Finalizar", WS_CHILD | BS_PUSHBUTTON, startX + 795, 430, 80, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_KILL_PROC, m_hInstance, nullptr));

    m_lblProcStatus = regControl(m_tab6Controls, CreateWindowW(L"STATIC", L"Processos vitais do sistema (CSRSS, LSASS, Explorer) estao protegidos contra encerramento.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 475, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 7: SERVIÇOS DO WINDOWS
    // ==========================================
    regControl(m_tab7Controls, CreateWindowW(L"STATIC", L"=== GERENCIAMENTO DE SERVICOS DO WINDOWS ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);

    m_editSvcSearch = regControl(m_tab7Controls, CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, startX, 92, 240, 26, hwnd, (HMENU)(UINT_PTR)ID_EDIT_SVC_SEARCH, m_hInstance, nullptr));
    SendMessageW(m_editSvcSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Pesquisar servico por nome ou descricao...");

    m_btnRefreshSvcList = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Atualizar Lista", WS_CHILD | BS_PUSHBUTTON, startX + 250, 91, 120, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_REFRESH_SVC_LIST, m_hInstance, nullptr));
    m_btnConfigSvcTriggers = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Configurar Gatilhos...", WS_CHILD | BS_PUSHBUTTON, startX + 380, 91, 150, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_CONFIG_SVC_TRIGGERS, m_hInstance, nullptr), m_hFontBold);

    m_listServices = regControl(m_tab7Controls, CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, startX, 125, 680, 300, hwnd, nullptr, m_hInstance, nullptr));
    ListView_SetExtendedListViewStyle(m_listServices, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvcSvc{};
    lvcSvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvcSvc.cx = 140; lvcSvc.pszText = (LPWSTR)L"Servico (ID)"; ListView_InsertColumn(m_listServices, 0, &lvcSvc);
    lvcSvc.cx = 240; lvcSvc.pszText = (LPWSTR)L"Nome de Exibicao"; ListView_InsertColumn(m_listServices, 1, &lvcSvc);
    lvcSvc.cx = 100; lvcSvc.pszText = (LPWSTR)L"Status Atual"; ListView_InsertColumn(m_listServices, 2, &lvcSvc);
    lvcSvc.cx = 160; lvcSvc.pszText = (LPWSTR)L"Regra Aplicada"; ListView_InsertColumn(m_listServices, 3, &lvcSvc);
    lvcSvc.cx = 130; lvcSvc.pszText = (LPWSTR)L"Nivel Seguranca"; ListView_InsertColumn(m_listServices, 4, &lvcSvc);

    m_lblSvcRulePrompt = regControl(m_tab7Controls, CreateWindowW(L"STATIC", L"Regra para o Servico:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 435, 140, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_comboSvcRule = regControl(m_tab7Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, startX + 145, 432, 210, 160, hwnd, (HMENU)(UINT_PTR)ID_COMBO_SVC_RULE, m_hInstance, nullptr));
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Padrao do Sistema (Automatico)");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Inteligente Opit-Core (Auto IA)");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Pausar em Modo Jogo");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Pausar em Modo Eco");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Pausar Sob Alta CPU (>80%)");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Sempre Pausado (Desativado)");
    SendMessageW(m_comboSvcRule, CB_ADDSTRING, 0, (LPARAM)L"Nunca Pausar (Protegido)");
    SendMessageW(m_comboSvcRule, CB_SETCURSEL, 0, 0);

    m_btnApplySvcRule = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Aplicar Regra", WS_CHILD | BS_PUSHBUTTON, startX + 365, 430, 105, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_APPLY_SVC_RULE, m_hInstance, nullptr));
    m_btnApplyAllSvcRules = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Aplicar em Todos", WS_CHILD | BS_PUSHBUTTON, startX + 475, 430, 115, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_APPLY_ALL_SVC_RULE, m_hInstance, nullptr));
    m_btnPauseSvcNow = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Pausar Agora", WS_CHILD | BS_PUSHBUTTON, startX + 595, 430, 100, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_PAUSE_SVC_NOW, m_hInstance, nullptr));
    m_btnResumeSvcNow = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Retomar Agora", WS_CHILD | BS_PUSHBUTTON, startX + 700, 430, 105, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_RESUME_SVC_NOW, m_hInstance, nullptr));
    m_btnRestoreAllSvcs = regControl(m_tab7Controls, CreateWindowW(L"BUTTON", L"Restaurar Todos Padrao", WS_CHILD | BS_PUSHBUTTON, startX + 810, 430, 150, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_RESTORE_ALL_SVCS, m_hInstance, nullptr));

    m_lblSvcStatus = regControl(m_tab7Controls, CreateWindowW(L"STATIC", L"Servicos vitais do Windows e anticheats sao protegidos automaticamente contra interrupcoes indevidas.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 470, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 8: JOGOS
    // ==========================================
    regControl(m_tab8Controls, CreateWindowW(L"STATIC", L"=== DETECCAO E PERFIS AUTOMATICOS DE JOGOS ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_chkAutoGame = regControl(m_tab8Controls, CreateWindowW(L"BUTTON", L"Ativar Modo Jogo Automatico (Alterna para Alto Desempenho ao iniciar jogo e restaura ao fechar)", WS_CHILD | BS_AUTOCHECKBOX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkAutoGame, BM_SETCHECK, Config::Instance().IsAutoGameModeEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_listGames = regControl(m_tab8Controls, CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, startX, 130, 500, 300, hwnd, nullptr, m_hInstance, nullptr));
    ListView_SetExtendedListViewStyle(m_listGames, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvcGame{};
    lvcGame.mask = LVCF_TEXT | LVCF_WIDTH;
    lvcGame.cx = 320; lvcGame.pszText = (LPWSTR)L"Executavel do Jogo (.exe)"; ListView_InsertColumn(m_listGames, 0, &lvcGame);
    lvcGame.cx = 160; lvcGame.pszText = (LPWSTR)L"Perfil Acionado"; ListView_InsertColumn(m_listGames, 1, &lvcGame);

    regControl(m_tab8Controls, CreateWindowW(L"STATIC", L"Gerenciar Perfis de Jogos:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 520, 135, 200, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_editGameExe = regControl(m_tab8Controls, CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER, startX + 520, 160, 200, 26, hwnd, nullptr, m_hInstance, nullptr));
    m_btnAddGame = regControl(m_tab8Controls, CreateWindowW(L"BUTTON", L"Adicionar / Criar Perfil...", WS_CHILD | BS_PUSHBUTTON, startX + 520, 195, 180, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_ADD_GAME, m_hInstance, nullptr));
    m_btnEditGame = regControl(m_tab8Controls, CreateWindowW(L"BUTTON", L"Editar Perfil do Jogo...", WS_CHILD | BS_PUSHBUTTON, startX + 520, 235, 180, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_EDIT_GAME, m_hInstance, nullptr));
    m_btnRemoveGame = regControl(m_tab8Controls, CreateWindowW(L"BUTTON", L"Remover Selecionado", WS_CHILD | BS_PUSHBUTTON, startX + 520, 275, 180, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_REMOVE_GAME, m_hInstance, nullptr));
    m_btnToggleGameMode = regControl(m_tab8Controls, CreateWindowW(L"BUTTON", L"Alternar Modo Jogo Manual", WS_CHILD | BS_PUSHBUTTON, startX + 520, 320, 180, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_TOGGLE_GAMEMODE, m_hInstance, nullptr));
    m_lblGameStatus = regControl(m_tab8Controls, CreateWindowW(L"STATIC", L"Status: Pronto", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 445, 600, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 9: MONITORAMENTO (GRAFICOS E HISTORICO)
    // ==========================================
    regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"=== MONITORAMENTO CONTINUO, TENDENCIAS E HISTORICO ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 650, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblMonRealtime = regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"Taxa de Amostragem: 1000 ms | Amostras Coletadas: 0", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr));
    m_lblMonCpuStats = regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"CPU (Sessao): Min: 0.0% | Max: 0.0% | Media: 0.0%", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 120, 330, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_lblMonRamStats = regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"RAM (Sessao): Min: 0.0% | Max: 0.0% | Media: 0.0%", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX + 350, 120, 330, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);

    regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"Grafico de Tendencia Recente de Uso da CPU (Ultimas 40 Amostras):", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 150, 600, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_lblMonTrend = regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"[........................................]", WS_CHILD | WS_BORDER | SS_LEFT | SS_NOPREFIX, startX, 175, 680, 28, hwnd, nullptr, m_hInstance, nullptr), m_hFontMono);

    regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"Historico de Gravacao no Banco SQLite (Tabela metrics_history):", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 215, 600, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_listMonHistory = regControl(m_tab9Controls, CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | WS_BORDER, startX, 240, 680, 190, hwnd, nullptr, m_hInstance, nullptr));
    ListView_SetExtendedListViewStyle(m_listMonHistory, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvcMon{};
    lvcMon.mask = LVCF_TEXT | LVCF_WIDTH;
    lvcMon.cx = 70; lvcMon.pszText = (LPWSTR)L"ID"; ListView_InsertColumn(m_listMonHistory, 0, &lvcMon);
    lvcMon.cx = 240; lvcMon.pszText = (LPWSTR)L"Data e Hora (ISO)"; ListView_InsertColumn(m_listMonHistory, 1, &lvcMon);
    lvcMon.cx = 180; lvcMon.pszText = (LPWSTR)L"CPU Usage (%)"; ListView_InsertColumn(m_listMonHistory, 2, &lvcMon);
    lvcMon.cx = 180; lvcMon.pszText = (LPWSTR)L"RAM Usage (%)"; ListView_InsertColumn(m_listMonHistory, 3, &lvcMon);
    lvcMon.cx = 180; lvcMon.pszText = (LPWSTR)L"Temperatura (°C)"; ListView_InsertColumn(m_listMonHistory, 4, &lvcMon);

    m_btnRefreshMonHistory = regControl(m_tab9Controls, CreateWindowW(L"BUTTON", L"Atualizar Historico", WS_CHILD | BS_PUSHBUTTON, startX, 440, 150, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_REFRESH_MON, m_hInstance, nullptr));
    m_btnClearMonHistory = regControl(m_tab9Controls, CreateWindowW(L"BUTTON", L"Limpar Historico", WS_CHILD | BS_PUSHBUTTON, startX + 160, 440, 140, 28, hwnd, (HMENU)(UINT_PTR)ID_BTN_CLEAR_MON, m_hInstance, nullptr));
    m_lblMonStatus = regControl(m_tab9Controls, CreateWindowW(L"STATIC", L"Metricas salvas periodicamente a cada 10 amostras no SQLite nativo.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 310, 445, 450, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 10: TÉRMICA
    // ==========================================
    regControl(m_tab10Controls, CreateWindowW(L"STATIC", L"=== PROTECAO TERMICA E SEGURANCA DO HARDWARE ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblThermalCurrent = regControl(m_tab10Controls, CreateWindowW(L"STATIC", L"Temperatura Atual: Lendo sensor...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 680, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
    m_lblThermalSensorStatus = regControl(m_tab10Controls, CreateWindowW(L"STATIC", L"Estado do Sensor: ...", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 125, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    m_chkThermalEnabled = regControl(m_tab10Controls, CreateWindowW(L"BUTTON", L"Ativar Protecao Termica Automatica (Throttling Protetor com Histerese)", WS_CHILD | BS_AUTOCHECKBOX, startX, 165, 650, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkThermalEnabled, BM_SETCHECK, Config::Instance().IsThermalProtectionEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    regControl(m_tab10Controls, CreateWindowW(L"STATIC", L"Limite Termico de Seguranca (°C):", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 205, 240, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_editThermalLimit = regControl(m_tab10Controls, CreateWindowW(L"EDIT", L"80", WS_CHILD | WS_BORDER | ES_NUMBER, startX + 250, 203, 80, 26, hwnd, nullptr, m_hInstance, nullptr));
    m_btnApplyThermal = regControl(m_tab10Controls, CreateWindowW(L"BUTTON", L"Salvar Configuracao Termica", WS_CHILD | BS_PUSHBUTTON, startX + 345, 201, 200, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_APPLY_THERMAL, m_hInstance, nullptr));

    m_lblThermalActionStatus = regControl(m_tab10Controls, CreateWindowW(L"STATIC", L"Mecanismo: Ao atingir o limite, reduz a CPU para 70% e restaura por histerese (-5°C).", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 245, 680, 40, hwnd, nullptr, m_hInstance, nullptr));

    m_btnOpenFansGraph = regControl(m_tab10Controls, CreateWindowW(L"BUTTON", L"📈 Configurar Curva no Grafico Interativo (Arrastar Pontos)...", WS_CHILD | BS_PUSHBUTTON, startX, 295, 520, 34, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_FANS_GRAPH, m_hInstance, nullptr), m_hFontBold);

    m_btnOpenFansConfig = regControl(m_tab10Controls, CreateWindowW(L"BUTTON", L"⚙️ Configuracoes Avancadas de Ventoinha e Sensores...", WS_CHILD | BS_PUSHBUTTON, startX, 335, 520, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_FANS, m_hInstance, nullptr));

    // ==========================================
    // ABA 11: CONFIGURAÇÕES GERAIS
    // ==========================================
    regControl(m_tab11Controls, CreateWindowW(L"STATIC", L"=== PREFERENCIAS DO APLICATIVO E PERSISTENCIA ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);

    m_chkStartWindows = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Iniciar com o Windows (Registro HKCU\\Run)", WS_CHILD | BS_AUTOCHECKBOX, startX, 95, 520, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkStartWindows, BM_SETCHECK, ElevationHelper::IsRunAtStartup() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_chkStartWindowsAdmin = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Iniciar com o Windows como Administrador (Agendador de Tarefas / Sem UAC)", WS_CHILD | BS_AUTOCHECKBOX, startX, 120, 580, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkStartWindowsAdmin, BM_SETCHECK, ElevationHelper::IsRunAtStartupAdmin() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_chkStartMinimized = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Iniciar minimizado na bandeja do sistema", WS_CHILD | BS_AUTOCHECKBOX, startX, 145, 500, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkStartMinimized, BM_SETCHECK, Config::Instance().GetStartMinimized() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_chkCloseToTray = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Fechar para a bandeja (manter o orquestrador em execucao)", WS_CHILD | BS_AUTOCHECKBOX, startX, 170, 500, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkCloseToTray, BM_SETCHECK, Config::Instance().GetCloseToTray() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_chkDisableTelemetry = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Desativar Telemetria da Microsoft (Privacidade & Fundo)", WS_CHILD | BS_AUTOCHECKBOX, startX, 195, 500, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkDisableTelemetry, BM_SETCHECK, UnifiedSettingsModel::Instance().disableMicrosoftTelemetry ? BST_CHECKED : BST_UNCHECKED, 0);

    m_chkSmartGamingServices = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Otimizacao Inteligente de Servicos (Pausar bloatware e updaters em jogos)", WS_CHILD | BS_AUTOCHECKBOX, startX, 220, 600, 22, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_chkSmartGamingServices, BM_SETCHECK, UnifiedSettingsModel::Instance().smartGamingServiceOptimization ? BST_CHECKED : BST_UNCHECKED, 0);

    regControl(m_tab11Controls, CreateWindowW(L"STATIC", L"Intervalo de Amostragem do Monitoramento:", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 255, 320, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_comboInterval = regControl(m_tab11Controls, CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, startX + 330, 252, 180, 100, hwnd, nullptr, m_hInstance, nullptr));
    SendMessageW(m_comboInterval, CB_ADDSTRING, 0, (LPARAM)L"Rapido (500 ms)");
    SendMessageW(m_comboInterval, CB_ADDSTRING, 0, (LPARAM)L"Normal (1000 ms)");
    SendMessageW(m_comboInterval, CB_ADDSTRING, 0, (LPARAM)L"Economico (2000 ms)");
    int curInt = Config::Instance().GetSampleIntervalMs();
    if (curInt <= 500) SendMessageW(m_comboInterval, CB_SETCURSEL, 0, 0);
    else if (curInt >= 2000) SendMessageW(m_comboInterval, CB_SETCURSEL, 2, 0);
    else SendMessageW(m_comboInterval, CB_SETCURSEL, 1, 0);

    m_btnSaveConfig = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Salvar Preferencias", WS_CHILD | BS_PUSHBUTTON, startX, 305, 170, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_SAVE_CONFIG, m_hInstance, nullptr));
    m_btnResetDefaults = regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Restaurar Padroes", WS_CHILD | BS_PUSHBUTTON, startX + 180, 305, 170, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_RESET_DEFAULTS, m_hInstance, nullptr));
    regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Abrir Painel Unificado de Configuracoes (Tela Centralizada)", WS_CHILD | BS_PUSHBUTTON, startX + 360, 305, 380, 32, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_UNIFIED, m_hInstance, nullptr), m_hFontBold);
    regControl(m_tab11Controls, CreateWindowW(L"BUTTON", L"Configurar Servicos Windows (Gatilhos, IA e Excecoes)...", WS_CHILD | BS_PUSHBUTTON, startX + 360, 345, 380, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_SERVICES_CFG, m_hInstance, nullptr));
    m_lblConfigStatus = regControl(m_tab11Controls, CreateWindowW(L"STATIC", L"Configuracoes salvas automaticamente em opitcore.json e no banco SQLite.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 385, 680, 22, hwnd, nullptr, m_hInstance, nullptr));

    // ==========================================
    // ABA 12: DIAGNÓSTICO E LOGS
    // ==========================================
    regControl(m_tab12Controls, CreateWindowW(L"STATIC", L"=== DIAGNOSTICO DO SISTEMA E AUDITORIA OPERACIONAL ===", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 600, 22, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);
    m_lblDiagElevated = regControl(m_tab12Controls, CreateWindowW(L"STATIC", L"Privilegios: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX, 95, 340, 20, hwnd, nullptr, m_hInstance, nullptr));
    m_lblDiagDb = regControl(m_tab12Controls, CreateWindowW(L"STATIC", L"Banco SQLite: ...", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, startX + 350, 95, 340, 20, hwnd, nullptr, m_hInstance, nullptr));

    m_listLogs = regControl(m_tab12Controls, CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, startX, 125, 680, 340, hwnd, nullptr, m_hInstance, nullptr), m_hFontMono);
    m_btnRefreshLogs = regControl(m_tab12Controls, CreateWindowW(L"BUTTON", L"Atualizar Logs", WS_CHILD | BS_PUSHBUTTON, startX, 480, 140, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_REFRESH_LOGS, m_hInstance, nullptr));
    m_btnCopyDiag = regControl(m_tab12Controls, CreateWindowW(L"BUTTON", L"Copiar Diagnostico", WS_CHILD | BS_PUSHBUTTON, startX + 150, 480, 160, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_COPY_DIAG, m_hInstance, nullptr));
    regControl(m_tab12Controls, CreateWindowW(L"BUTTON", L"IA Copilot / Diagnostico...", WS_CHILD | BS_PUSHBUTTON, startX + 320, 480, 220, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_OPEN_AI, m_hInstance, nullptr), m_hFontBold);
    m_lblDiagStatus = regControl(m_tab12Controls, CreateWindowW(L"STATIC", L"Registros operacionais em tempo real.", WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 550, 485, 250, 22, hwnd, nullptr, m_hInstance, nullptr));

    // Tab 13: Sobre & Licença
    {
        m_lblAboutTitle = regControl(m_tab13Controls, CreateWindowW(L"STATIC", L"Sobre o Opit-Core — gratuito para uso, código disponível e transparente.",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 60, 780, 26, hwnd, nullptr, m_hInstance, nullptr), m_hFontTitle);

        struct AboutLine { const wchar_t* text; bool strong; };
        static const AboutLine kAboutLines[] = {
            { L"Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com). Começou como um jeito de domar a velocidade", false },
            { L"do processador e virou um gerenciador completo de CPU, GPU, ventoinhas, memória, serviços e energia.", false },
            { L"", false },
            { L"LICENÇA (POLYFORM STRICT 1.0.0)", true },
            { L"O Opit-Core é donationware com código-fonte disponível sob a PolyForm Strict License 1.0.0.", false },
            { L"Uso gratuito apenas para fins pessoais e não comerciais (ou instituições sem fins lucrativos).", false },
            { L"Proibido uso comercial, redistribuição ou derivados sem autorização. Software SEM GARANTIA.", false },
            { L"", false },
            { L"COMO USAR, SEM PEGADINHA", true },
            { L"Sem ativação, sem chave, sem cadastro, sem servidor: instalar e usar. Funciona 100% offline.", false },
            { L"Nada do seu PC sai do seu PC — não há telemetria nem canal de envio; a auditoria fica no log local.", false },
            { L"Tudo que ele faz é reversível: lê antes de escrever, e cada mudança tem diário de recuperação.", false },
            { L"", false },
            { L"ONDE RODA", true },
            { L"Windows 10 e Windows 11 (x64 e x86).", false },
        };
        for (const auto& line : kAboutLines) {
            HWND h = CreateWindowW(L"STATIC", line.text, WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 96, 780, 20,
                                   hwnd, nullptr, m_hInstance, nullptr);
            regControl(m_tab13Controls, h, line.strong ? m_hFontBold : nullptr);
        }

        m_lblPixHead = regControl(m_tab13Controls, CreateWindowW(L"STATIC",
            L"Nenhum pagamento recorrente, nenhuma assinatura. Se o programa te devolveu tempo, silêncio ou energia,",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 430, 780, 20, hwnd, nullptr, m_hInstance, nullptr));
        regControl(m_tab13Controls, CreateWindowW(L"STATIC",
            L"uma contribuição em qualquer valor mantém os próximos projetos de pé. É opcional, e nada muda por causa dela.",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 452, 780, 20, hwnd, nullptr, m_hInstance, nullptr), m_hFontBold);
        m_lblPixKey = regControl(m_tab13Controls, CreateWindowW(L"STATIC", L"Pix:  alexcstos@gmail.com",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, startX, 486, 330, 26, hwnd, nullptr, m_hInstance, nullptr), m_hFontMono);
        m_btnCopyPix = regControl(m_tab13Controls, CreateWindowW(L"BUTTON", L"Copiar chave Pix",
            WS_CHILD | BS_PUSHBUTTON, startX + 346, 484, 150, 30, hwnd, (HMENU)(UINT_PTR)ID_BTN_COPY_PIX, m_hInstance, nullptr));
        m_lblAboutStatus = regControl(m_tab13Controls, CreateWindowW(L"STATIC",
            L"Obrigado por usar algo que não esconde nada de você.",
            WS_CHILD | SS_LEFT | SS_NOPREFIX, startX + 512, 489, 320, 22, hwnd, nullptr, m_hInstance, nullptr));
    }

    auto applyDarkList = [](HWND hList) {
        if (!hList) return;
        ui::Theme::ApplyDarkExplorerTheme(hList);
        ListView_SetBkColor(hList, ui::Theme::BgInput);
        ListView_SetTextColor(hList, ui::Theme::TextPrimary);
        ListView_SetTextBkColor(hList, ui::Theme::BgInput);
    };
    applyDarkList(m_listGpu);
    applyDarkList(m_listProcesses);
    applyDarkList(m_listServices);
    applyDarkList(m_listGames);
    applyDarkList(m_listMonHistory);

    // Inicializar na aba 0
    SwitchTab(0);
}

void MainWindow::SwitchTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)m_allTabControls.size()) return;

    for (size_t t = 0; t < m_allTabControls.size(); ++t) {
        int showCmd = (t == (size_t)tabIndex) ? SW_SHOW : SW_HIDE;
        for (HWND h : *m_allTabControls[t]) {
            ShowWindow(h, showCmd);
        }
    }

    m_currentTab = tabIndex;
    TabCtrl_SetCurSel(m_tab, tabIndex);

    RECT rcMain{};
    GetClientRect(m_hwnd, &rcMain);
    LayoutControls(rcMain.right, rcMain.bottom);

    if (m_sidebarHwnd) {
        InvalidateRect(m_sidebarHwnd, nullptr, FALSE);
    }

    // Evitar ghosts visuais repintando completamente
    InvalidateRect(m_hwnd, nullptr, TRUE);
    UpdateWindow(m_hwnd);

    // Atualizacoes imediatas ao focar abas
    if (tabIndex == 6) {
        RefreshProcessList();
    } else if (tabIndex == 7) {
        RefreshServicesList();
    } else if (tabIndex == 9) {
        RefreshMonitoringHistory();
    } else if (tabIndex == 12) {
        RefreshLogs();
    }
}

void MainWindow::PopulateHardwareInfo() {
    auto hw = CoreRuntime::Instance().GetHardwareInfo();

    std::wstring cpuBrand = StringUtils::ToWide(hw.cpu.brand);
    std::wstring cpuArch = StringUtils::ToWide(hw.cpu.architecture);
    std::wstring cpuText = L"Processador: " + cpuBrand;
    if (!cpuArch.empty() && cpuBrand.find(cpuArch) == std::wstring::npos) {
        cpuText += L" (" + cpuArch + L")";
    }
    SetWindowTextW(m_lblDashCpu, cpuText.c_str());
    SetWindowTextW(m_lblCpuInfo, cpuText.c_str());

    std::wstringstream wsCores;
    wsCores << L"Nucleos Fisicos: " << hw.cpu.physicalCores << L" | Threads Logicas: " << hw.cpu.logicalThreads;
    SetWindowTextW(m_lblCpuCores, wsCores.str().c_str());

    std::wstring archText = L"Origem dos dados: " + StringUtils::ToWide(DataOriginToString(hw.cpu.origin)) +
                            L" (Confiabilidade: " + StringUtils::ToWide(DataConfidenceToString(hw.cpu.confidence)) + L")";
    SetWindowTextW(m_lblCpuArch, archText.c_str());

    // Especificações Documentadas na Base de Conhecimento SQLite
    std::wstring docSpecStr;
    if (hw.documentedCpu.found) {
        docSpecStr = L"Especificacoes Documentadas (Fabricante): " + StringUtils::ToWide(hw.documentedCpu.model) +
                     L" | Base: " + StringUtils::ToWide(hw.documentedCpu.clockBase) +
                     L" | Boost: " + StringUtils::ToWide(hw.documentedCpu.clockBoost) +
                     L" | TDP: " + StringUtils::ToWide(hw.documentedCpu.tdp) +
                     L" | Socket: " + StringUtils::ToWide(hw.documentedCpu.socketOrBus);
        SetWindowTextW(m_lblDashDocCpu, (L"Base Oficial: " + StringUtils::ToWide(hw.documentedCpu.model) + L" - TDP: " + StringUtils::ToWide(hw.documentedCpu.tdp)).c_str());
    } else {
        docSpecStr = L"Especificacoes Documentadas: Modelo nao indexado na base local; operando via CPUID em tempo real.";
        SetWindowTextW(m_lblDashDocCpu, L"Base de Conhecimento: Modelo detectado via instrucao CPUID nativa.");
    }
    SetWindowTextW(m_lblCpuDocSpecs, docSpecStr.c_str());

    // RAM Inicial
    std::wstring ramText = L"Memoria RAM Total: " + StringUtils::ToWide(StringUtils::FormatBytes(hw.memory.totalPhysicalBytes));
    if (!hw.memorySticks.empty()) {
        ramText += L" (" + std::to_wstring(hw.memorySticks.size()) + L" modulo(s) identificados no SMBIOS)";
    }
    SetWindowTextW(m_lblRamTotal, ramText.c_str());

    // Placa-mae, BIOS e pentes de memoria (SMBIOS do firmware; numeros de serie nunca exibidos)
    if (!hw.motherboard.manufacturer.empty() || !hw.motherboard.product.empty()) {
        std::wstringstream wsBoard;
        wsBoard << L"Placa-Mae: " << StringUtils::ToWide(hw.motherboard.manufacturer) << L" "
                << StringUtils::ToWide(hw.motherboard.product);
        if (!hw.motherboard.version.empty()) {
            wsBoard << L" (versao " << StringUtils::ToWide(hw.motherboard.version) << L")";
        }
        if (!hw.motherboard.chassisType.empty()) {
            wsBoard << L"\r\nChassi: " << StringUtils::ToWide(hw.motherboard.chassisType);
        }
        wsBoard << L"\r\nOrigem dos dados: SMBIOS do firmware (informacao de fabrica).";
        SetWindowTextW(m_lblBoardInfo, wsBoard.str().c_str());
    } else {
        SetWindowTextW(m_lblBoardInfo, L"Placa-Mae: Nao exposta pelo firmware desta maquina (SMBIOS indisponivel); a operacao do programa nao e afetada.");
    }

    if (!hw.bios.vendor.empty()) {
        std::wstring biosStr = L"BIOS: " + StringUtils::ToWide(hw.bios.vendor) + L" " + StringUtils::ToWide(hw.bios.version);
        if (!hw.bios.releaseDate.empty()) {
            biosStr += L" (lancamento " + StringUtils::ToWide(hw.bios.releaseDate) + L")";
        }
        SetWindowTextW(m_lblBiosInfo, biosStr.c_str());
    } else {
        SetWindowTextW(m_lblBiosInfo, L"BIOS: Nao exposta pelo firmware (SMBIOS indisponivel nesta maquina).");
    }

    if (!hw.memorySticks.empty()) {
        std::wstringstream wsSticks;
        wsSticks << L"Pentes de Memoria (" << hw.memorySticks.size() << L" modulo(s), SMBIOS):\r\n";
        for (const auto& s : hw.memorySticks) {
            wsSticks << L"  - " << StringUtils::ToWide(s.locator) << L": ";
            if (s.capacityMB > 0) {
                wsSticks << (s.capacityMB / 1024) << L" GB";
            } else {
                wsSticks << L"capacidade nao informada";
            }
            if (!s.memoryType.empty()) wsSticks << L" " << StringUtils::ToWide(s.memoryType);
            if (s.configuredSpeedMHz > 0) wsSticks << L" @ " << s.configuredSpeedMHz << L" MHz";
            if (!s.manufacturer.empty()) wsSticks << L" - " << StringUtils::ToWide(s.manufacturer);
            if (!s.partNumber.empty()) wsSticks << L" (" << StringUtils::ToWide(s.partNumber) << L")";
            wsSticks << L"\r\n";
        }
        SetWindowTextW(m_lblRamSticks, wsSticks.str().c_str());
    } else {
        SetWindowTextW(m_lblRamSticks, L"Pentes de Memoria: Nao expostos pelo firmware (SMBIOS indisponivel nesta maquina); a capacidade total continua sendo medida em tempo real.");
    }

    // Preencher GPUs na ListView
    ListView_DeleteAllItems(m_listGpu);
    if (hw.gpus.empty()) {
        SetWindowTextW(m_lblDashGpu, L"Placa de Video: Adaptador basico de video");
        SetWindowTextW(m_lblGpuDocSpecs, L"Nenhuma GPU dedicada detectada.");
    } else {
        std::wstring primaryGpu = StringUtils::ToWide(hw.gpus[0].description) + L" (" + 
                                  std::to_wstring(hw.gpus[0].dedicatedVideoMemoryMB) + L" MB VRAM)";
        SetWindowTextW(m_lblDashGpu, (L"Placa de Video: " + primaryGpu).c_str());

        for (int i = 0; i < (int)hw.gpus.size(); ++i) {
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            std::wstring desc = StringUtils::ToWide(hw.gpus[i].description);
            lvi.pszText = (LPWSTR)desc.c_str();
            ListView_InsertItem(m_listGpu, &lvi);

            std::wstring vram = std::to_wstring(hw.gpus[i].dedicatedVideoMemoryMB);
            ListView_SetItemText(m_listGpu, i, 1, (LPWSTR)vram.c_str());

            std::wstring shared = std::to_wstring(hw.gpus[i].sharedSystemMemoryMB);
            ListView_SetItemText(m_listGpu, i, 2, (LPWSTR)shared.c_str());

            std::wstring tipo = hw.gpus[i].isDedicated ? L"Dedicada (dGPU)" : L"Integrada (iGPU)";
            ListView_SetItemText(m_listGpu, i, 3, (LPWSTR)tipo.c_str());
        }

        if (hw.documentedGpu.found) {
            std::wstring docGpuStr = L"Documentacao Oficial: " + StringUtils::ToWide(hw.documentedGpu.model) +
                                     L" | " + StringUtils::ToWide(hw.documentedGpu.coresThreads) +
                                     L" | Barramento: " + StringUtils::ToWide(hw.documentedGpu.socketOrBus) +
                                     L" | " + StringUtils::ToWide(hw.documentedGpu.details);
            SetWindowTextW(m_lblGpuDocSpecs, docGpuStr.c_str());
        } else {
            SetWindowTextW(m_lblGpuDocSpecs, L"Adaptador verificado via DXGI (DirectX Graphics Infrastructure).");
        }
    }

    // Diagnóstico
    bool elevated = ElevationHelper::IsElevated();
    std::wstring elevStr = elevated ? L"Privilegios: Administrador (Elevado)" : L"Privilegios: Usuario Padrao (Nao-elevado)";
    SetWindowTextW(m_lblDiagElevated, elevStr.c_str());

    std::wstring dbStr = DatabaseManager::Instance().IsOpen() ? 
                         L"Banco SQLite: Ativo (Schema v" + std::to_wstring(DatabaseManager::Instance().GetSchemaVersion()) + L")" :
                         L"Banco SQLite: Inativo";
    SetWindowTextW(m_lblDiagDb, dbStr.c_str());

    // Configurar campos de edicao
    std::wstringstream wsThrottle;
    wsThrottle << Config::Instance().GetProcessorThrottleCap();
    SetWindowTextW(m_editCpuThrottle, wsThrottle.str().c_str());

    std::wstringstream wsTerm;
    wsTerm << (int)Config::Instance().GetThermalLimitC();
    SetWindowTextW(m_editThermalLimit, wsTerm.str().c_str());
}

void MainWindow::PopulatePowerSchemes() {
    auto schemes = CoreRuntime::Instance().GetPowerManager().ListSchemes();
    SendMessageW(m_comboPowerSchemes, CB_RESETCONTENT, 0, 0);

    int activeIdx = 0;
    for (int i = 0; i < (int)schemes.size(); ++i) {
        // So o nome amigavel vai para o publico; o GUID (registro do Windows) fica interno.
        std::wstring item = StringUtils::ToWide(schemes[i].name);
        SendMessageW(m_comboPowerSchemes, CB_ADDSTRING, 0, (LPARAM)item.c_str());
        if (schemes[i].isActive) {
            activeIdx = i;
        }
    }
    SendMessageW(m_comboPowerSchemes, CB_SETCURSEL, activeIdx, 0);

    PowerScheme active = CoreRuntime::Instance().GetPowerManager().GetActiveScheme();
    std::wstring activeStr = L"Plano Ativo: " + StringUtils::ToWide(active.name);
    SetWindowTextW(m_lblActivePowerScheme, activeStr.c_str());
    SetWindowTextW(m_lblDashPower, activeStr.c_str());
}

void MainWindow::RefreshProcessList() {
    auto processes = ProcessManager::EnumerateProcesses();
    ListView_DeleteAllItems(m_listProcesses);

    int row = 0;
    for (const auto& p : processes) {
        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        std::wstring pidStr = std::to_wstring(p.pid);
        lvi.pszText = (LPWSTR)pidStr.c_str();
        ListView_InsertItem(m_listProcesses, &lvi);

        std::wstring nameStr = StringUtils::ToWide(p.name);
        ListView_SetItemText(m_listProcesses, row, 1, (LPWSTR)nameStr.c_str());

        double mb = (double)p.workingSetBytes / (1024.0 * 1024.0);
        std::wstringstream wsMem;
        wsMem << std::fixed << std::setprecision(1) << mb << L" MB";
        ListView_SetItemText(m_listProcesses, row, 2, (LPWSTR)wsMem.str().c_str());

        std::wstring prioStr = StringUtils::ToWide(p.priorityName);
        ListView_SetItemText(m_listProcesses, row, 3, (LPWSTR)prioStr.c_str());

        std::wstring critStr = p.isSystemCritical ? L"Sistema (Vital)" : L"Usuario / Aplicacao";
        // O que o auto de processos fez neste PID agora (nivel aplicado e motivo medido), quando fez.
        const std::string managed = ProcessGovernor::Instance().ManagementLabelFor(p.pid);
        if (!managed.empty()) critStr += L"  ·  " + StringUtils::ToWide(managed);
        ListView_SetItemText(m_listProcesses, row, 4, (LPWSTR)critStr.c_str());

        row++;
    }

    SetWindowTextW(m_lblProcStatus, (L"Total de processos listados: " + std::to_wstring(processes.size())).c_str());
}

void MainWindow::RefreshServicesList(const std::string& filter) {
    if (!m_listServices) return;

    std::string effectiveFilter = filter;
    if (effectiveFilter.empty() && m_editSvcSearch) {
        WCHAR buf[256] = {0};
        GetWindowTextW(m_editSvcSearch, buf, 256);
        effectiveFilter = StringUtils::ToUtf8(buf);
    }

    auto services = ServiceManager::Instance().EnumerateAllServices(effectiveFilter);
    ListView_DeleteAllItems(m_listServices);

    int row = 0;
    for (const auto& s : services) {
        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        std::wstring nameStr = StringUtils::ToWide(s.name);
        lvi.pszText = (LPWSTR)nameStr.c_str();
        ListView_InsertItem(m_listServices, &lvi);

        std::wstring dispStr = StringUtils::ToWide(s.displayName.empty() ? s.name : s.displayName);
        ListView_SetItemText(m_listServices, row, 1, (LPWSTR)dispStr.c_str());

        std::wstring statusStr = s.isPausedByOpit ? L"Pausado (Opit-Core)" : (s.isRunning ? L"Em Execucao" : L"Parado");
        if (s.isPausedByOpit && s.gainMB > 0) statusStr += L" · " + std::to_wstring(s.gainMB) + L" MB fora da RAM";
        ListView_SetItemText(m_listServices, row, 2, (LPWSTR)statusStr.c_str());

        std::wstring ruleStr = L"Padrao do Sistema";
        if (s.rule == "auto") ruleStr = L"Inteligente (Auto IA)";
        else if (s.rule == "pause_on_game") ruleStr = L"Pausar em Modo Jogo";
        else if (s.rule == "pause_on_eco") ruleStr = L"Pausar em Modo Eco";
        else if (s.rule == "pause_on_high_cpu") ruleStr = L"Pausar Sob Alta CPU";
        else if (s.rule == "always_paused") ruleStr = L"Sempre Pausado";
        else if (s.rule == "never_pause") ruleStr = L"Nunca Pausar (Protegido)";
        ListView_SetItemText(m_listServices, row, 3, (LPWSTR)ruleStr.c_str());

        std::wstring secStr = s.isCritical ? L"Critico (Protegido)" : (!s.isAllowed ? L"Restrito" : L"Otimizavel");
        // O que a automacao sabe deste nome agora: como ele volta, o ganho medido ou porque nao encosta.
        if (s.isPausedByOpit && !s.wake.empty()) secStr += L"  ·  " + StringUtils::ToWide(s.wake);
        else if (!s.blockReason.empty()) secStr += L"  ·  " + StringUtils::ToWide(s.blockReason);
        else if (s.gainMB > 0) secStr += L"  ·  " + std::to_wstring(s.gainMB) + L" MB no hospedeiro";
        ListView_SetItemText(m_listServices, row, 4, (LPWSTR)secStr.c_str());

        row++;
    }

    std::wstring countStr = L"Total de servicos listados: " + std::to_wstring(services.size());
    if (!effectiveFilter.empty()) {
        countStr += L" (filtrado por '" + StringUtils::ToWide(effectiveFilter) + L"')";
    }
    SetWindowTextW(m_lblSvcStatus, countStr.c_str());
}

void MainWindow::RefreshGameList() {
    auto profiles = DatabaseManager::Instance().GetAllGameProfiles();
    ListView_DeleteAllItems(m_listGames);

    auto& gd = CoreRuntime::Instance().GetGameDetector();
    gd.Clear();

    for (int i = 0; i < (int)profiles.size(); ++i) {
        gd.AddGame(profiles[i].exeName);

        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        std::wstring g = StringUtils::ToWide(profiles[i].exeName);
        lvi.pszText = (LPWSTR)g.c_str();
        ListView_InsertItem(m_listGames, &lvi);

        std::wstring desc = StringUtils::ToWide(profiles[i].gameName.empty() ? profiles[i].cpuProfile : (profiles[i].gameName + " [" + profiles[i].cpuProfile + " / " + profiles[i].gpuProfile + "]"));
        ListView_SetItemText(m_listGames, i, 1, (LPWSTR)desc.c_str());
    }
}

void MainWindow::RefreshLogs() {
    auto logs = Logger::Instance().GetRecentLogs(200);
    SendMessageW(m_listLogs, LB_RESETCONTENT, 0, 0);
    for (const auto& line : logs) {
        std::wstring ws = StringUtils::ToWide(line);
        SendMessageW(m_listLogs, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
    }
    int count = (int)SendMessageW(m_listLogs, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessageW(m_listLogs, LB_SETTOPINDEX, count - 1, 0);
    }
}

void MainWindow::RefreshMonitoringHistory() {
    auto history = DatabaseManager::Instance().GetMetricsHistory(40);
    ListView_DeleteAllItems(m_listMonHistory);

    int row = 0;
    for (const auto& r : history) {
        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        std::wstring idStr = std::to_wstring(r.id);
        lvi.pszText = (LPWSTR)idStr.c_str();
        ListView_InsertItem(m_listMonHistory, &lvi);

        std::wstring ts = StringUtils::ToWide(r.timestamp);
        ListView_SetItemText(m_listMonHistory, row, 1, (LPWSTR)ts.c_str());

        std::wstringstream wsCpu;
        wsCpu << std::fixed << std::setprecision(1) << r.cpuUsage << L"%";
        ListView_SetItemText(m_listMonHistory, row, 2, (LPWSTR)wsCpu.str().c_str());

        std::wstringstream wsRam;
        wsRam << std::fixed << std::setprecision(1) << r.ramUsage << L"%";
        ListView_SetItemText(m_listMonHistory, row, 3, (LPWSTR)wsRam.str().c_str());

        std::wstringstream wsTemp;
        if (r.tempC > 0.0) {
            wsTemp << std::fixed << std::setprecision(1) << r.tempC << L" °C";
        } else {
            wsTemp << L"N/D";
        }
        ListView_SetItemText(m_listMonHistory, row, 4, (LPWSTR)wsTemp.str().c_str());

        row++;
    }

    SetWindowTextW(m_lblMonStatus, (L"Exibindo os ultimos " + std::to_wstring(history.size()) + L" registros do SQLite.").c_str());
}

void MainWindow::UpdatePeriodicData() {
    SystemMetricsSnapshot snap = CoreRuntime::Instance().GetMetricsSnapshot();

    // 1. Atualizar histórico e estatísticas em memória
    m_cpuHistory.push_back(snap.cpuUsagePercent);
    if (m_cpuHistory.size() > 40) {
        m_cpuHistory.erase(m_cpuHistory.begin());
    }

    if (snap.cpuUsagePercent < m_minCpu) m_minCpu = snap.cpuUsagePercent;
    if (snap.cpuUsagePercent > m_maxCpu) m_maxCpu = snap.cpuUsagePercent;
    m_sumCpu += snap.cpuUsagePercent;
    m_countCpu++;

    if (snap.ramUsagePercent < m_minRam) m_minRam = snap.ramUsagePercent;
    if (snap.ramUsagePercent > m_maxRam) m_maxRam = snap.ramUsagePercent;
    m_sumRam += snap.ramUsagePercent;

    // 2. Dashboard & CPU
    std::wstringstream wsCpu;
    wsCpu << L"Uso em Tempo Real: " << std::fixed << std::setprecision(1) << snap.cpuUsagePercent << L"%";
    SetWindowTextW(m_lblCpuUsage, wsCpu.str().c_str());

    std::wstringstream wsDashCpu;
    wsDashCpu << L"Uso de CPU: " << std::fixed << std::setprecision(1) << snap.cpuUsagePercent << L"% | Perfil: "
              << StringUtils::ToWide(snap.activeProfileName);
    SetWindowTextW(m_lblDashProfile, wsDashCpu.str().c_str());

    SendMessageW(m_progDashCpu, PBM_SETPOS, (WPARAM)(int)snap.cpuUsagePercent, 0);
    SendMessageW(m_progCpuUsage, PBM_SETPOS, (WPARAM)(int)snap.cpuUsagePercent, 0);

    // 3. RAM
    std::wstringstream wsRam;
    wsRam << L"Memoria RAM: " << snap.ramUsedMB << L" MB em uso / " << snap.ramTotalMB << L" MB total ("
          << std::fixed << std::setprecision(1) << snap.ramUsagePercent << L"%)";
    SetWindowTextW(m_lblDashRam, wsRam.str().c_str());
    SetWindowTextW(m_lblRamUsed, (L"Em Uso: " + std::to_wstring(snap.ramUsedMB) + L" MB").c_str());
    SetWindowTextW(m_lblRamFree, (L"Disponivel: " + std::to_wstring(snap.ramTotalMB - snap.ramUsedMB) + L" MB").c_str());
    SetWindowTextW(m_lblRamLoad, (L"Carga de Memoria: " + std::to_wstring((int)snap.ramUsagePercent) + L"%").c_str());

    SendMessageW(m_progDashRam, PBM_SETPOS, (WPARAM)(int)snap.ramUsagePercent, 0);
    SendMessageW(m_progRamLoad, PBM_SETPOS, (WPARAM)(int)snap.ramUsagePercent, 0);

    // 4. Térmica
    std::wstringstream wsTemp;
    if (snap.hasTempSensor) {
        wsTemp << L"Temperatura Hardware: " << std::fixed << std::setprecision(1) << snap.cpuTemperatureC << L" °C";
        if (snap.hasFanRpm) {
            wsTemp << L" | Ventoinha: " << snap.fanRpm << L" RPM";
        }
        if (snap.isThermalThrottling) {
            wsTemp << L" [THROTTLING ATIVADO - CPU CAP APLICADO]";
        }
        SetWindowTextW(m_lblDashTemp, wsTemp.str().c_str());
        SetWindowTextW(m_lblThermalCurrent, wsTemp.str().c_str());
        std::wstring srcTxt = L"Sensor de Hardware Ativo: " +
                              (snap.tempSensorSource.empty() ? std::wstring(L"Operacional e respondendo")
                                                             : StringUtils::ToWide(snap.tempSensorSource));
        SetWindowTextW(m_lblThermalSensorStatus, srcTxt.c_str());
    } else if (snap.hasFanRpm) {
        wsTemp << L"Ventoinha Hardware: " << snap.fanRpm << L" RPM | Temp: N/D";
        SetWindowTextW(m_lblDashTemp, wsTemp.str().c_str());
        SetWindowTextW(m_lblThermalCurrent, wsTemp.str().c_str());
        SetWindowTextW(m_lblThermalSensorStatus, L"Tacometro de ventoinha ativo e respondendo.");
    } else {
        SetWindowTextW(m_lblDashTemp, L"Temperatura CPU: Sensor ACPI nao reportado pelo firmware");
        SetWindowTextW(m_lblThermalCurrent, L"Temperatura: Nao disponivel no hardware atual");
        SetWindowTextW(m_lblThermalSensorStatus, L"Sensor ACPI nao exposto pelo fabricante ou sem suporte do driver.");
    }

    // 4.1. Telemetria da funcao-objetivo anexada a linha de hardware do dashboard: so aparece
    // quando o sensor existe; ausencia nao inventa valor.
    if (m_lblDashTemp && (snap.hasFrameTime || snap.hasGpuPowerW)) {
        std::wstringstream wsFt;
        bool hasSeg = false;
        if (snap.hasFrameTime && snap.frameTimeMs > 0.0) {
            wsFt << L"Quadro: " << std::fixed << std::setprecision(1) << snap.frameTimeMs << L" ms ("
                 << std::setprecision(0) << (1000.0 / snap.frameTimeMs) << L" FPS)";
            hasSeg = true;
        }
        if (snap.hasGpuPowerW) {
            if (hasSeg) wsFt << L" | ";
            wsFt << L"GPU: " << std::fixed << std::setprecision(1) << snap.gpuPowerWatts << L" W";
            hasSeg = true;
        }
        if (hasSeg) {
            wchar_t cur[512]{};
            int len = GetWindowTextW(m_lblDashTemp, cur, 512);
            std::wstring merged(cur, cur + (len > 0 ? len : 0));
            if (!merged.empty()) merged += L" | ";
            SetWindowTextW(m_lblDashTemp, (merged + wsFt.str()).c_str());
        }
    }

    // 5. Jogo
    if (snap.isGameRunning) {
        std::wstring gameTxt = L"Modo Jogo: ATIVO (" + StringUtils::ToWide(snap.runningGameName) + L")";
        SetWindowTextW(m_lblDashGame, gameTxt.c_str());
    } else {
        SetWindowTextW(m_lblDashGame, L"Modo Jogo: Nenhum jogo em execucao");
    }

    // 5.1. Coordenador do Sistema
    if (m_lblDashCoordinator) {
        std::wstring coordDesc = L"Coordenador do Sistema: " + StringUtils::ToWide(ModeCoordinator::Instance().GetCurrentModeDescription());
        SetWindowTextW(m_lblDashCoordinator, coordDesc.c_str());
    }

    // 5.2. Atualizacao visual dinamica do icone na bandeja (Systray) de acordo com o modo
    TrayVisualMode targetVisual = TrayVisualMode::Balanced;
    std::wstring tip = L"Opit-Core — Modo Balanceado";

    if (UnifiedSettingsModel::Instance().smartOrchestrator) {
        targetVisual = TrayVisualMode::AI;
        tip = L"Opit-Core — Modo Inteligencia IA (Auto-Otimizando)";
    } else if (ModeCoordinator::Instance().IsGameModeActive() || Config::Instance().GetProfile() == Config::Profile::HighPerformance) {
        targetVisual = TrayVisualMode::Performance;
        tip = ModeCoordinator::Instance().IsGameModeActive() ?
              (L"Opit-Core — Modo Jogo Ativo (" + StringUtils::ToWide(snap.runningGameName) + L")") :
              L"Opit-Core — Alto Desempenho (100%)";
    } else if (ModeCoordinator::Instance().IsDeepEcoModeActive() || Config::Instance().GetProfile() == Config::Profile::Eco) {
        targetVisual = TrayVisualMode::Eco;
        tip = ModeCoordinator::Instance().IsDeepEcoModeActive() ?
              L"Opit-Core — Economia Maxima (Deep Eco)" :
              L"Opit-Core — Modo Economico (75%)";
    } else {
        targetVisual = TrayVisualMode::Balanced;
        tip = L"Opit-Core — Modo Balanceado";
    }

    if (targetVisual != m_currentTrayMode) {
        UpdateTrayVisual(targetVisual, tip);
    }

    // 6. Bateria
    BatteryStatusInfo bat = CoreRuntime::Instance().GetPowerManager().GetBatteryStatus();
    std::wstring batTxt;
    if (bat.hasBattery) {
        batTxt = L"Alimentacao: Bateria em " + std::to_wstring(bat.batteryPercent) + L"%" + 
                 (bat.isCharging ? L" (Carregando)" : (bat.isAcOnline ? L" (Na tomada)" : L" (Na bateria)"));
    } else {
        batTxt = L"Alimentacao: Conectado a rede eletrica (Desktop / AC)";
    }
    SetWindowTextW(m_lblBatteryStatus, batTxt.c_str());

    // 7. Monitoramento Tab: Atualizar Estatísticas e Gráfico Sparkline de Tendência
    if (m_currentTab == 8) {
        std::wstringstream wsMonRate;
        wsMonRate << L"Intervalo: " << Config::Instance().GetSampleIntervalMs() << L" ms | Total de Amostras na Sessao: " << m_countCpu;
        SetWindowTextW(m_lblMonRealtime, wsMonRate.str().c_str());

        double avgCpu = (m_countCpu > 0) ? (m_sumCpu / m_countCpu) : 0.0;
        std::wstringstream wsCpuStat;
        wsCpuStat << L"CPU (Sessao): Min: " << std::fixed << std::setprecision(1) << m_minCpu << L"% | Max: " << m_maxCpu << L"% | Media: " << avgCpu << L"%";
        SetWindowTextW(m_lblMonCpuStats, wsCpuStat.str().c_str());

        double avgRam = (m_countCpu > 0) ? (m_sumRam / m_countCpu) : 0.0;
        std::wstringstream wsRamStat;
        wsRamStat << L"RAM (Sessao): Min: " << std::fixed << std::setprecision(1) << m_minRam << L"% | Max: " << m_maxRam << L"% | Media: " << avgRam << L"%";
        SetWindowTextW(m_lblMonRamStats, wsRamStat.str().c_str());

        // Gráfico de sparklines de tendência usando caracteres de bloco consistentes
        std::wstring spark;
        spark += L"[ ";
        const wchar_t sparkChars[] = { L'_', L'.', L'-', L'=', L'o', L'O', L'#', L'@' };
        for (double val : m_cpuHistory) {
            int idx = (int)(val / 12.5);
            if (idx < 0) idx = 0;
            if (idx > 7) idx = 7;
            spark += sparkChars[idx];
            spark += L' ';
        }
        spark += L"] Ultimo: " + std::to_wstring((int)snap.cpuUsagePercent) + L"%";
        SetWindowTextW(m_lblMonTrend, spark.c_str());
    }

    // 8. Diagnostico Tab: Status de Gargalo em Tempo Real
    if (m_currentTab == 12 && m_lblDiagStatus) {
        auto diag = AdaptiveEngine::Instance().GetLatestDiagnosis();
        if (diag.primaryType != BottleneckType::None) {
            std::wstring diagTxt = L"⚠️ Gargalo: " + StringUtils::ToWide(BottleneckTypeToString(diag.primaryType)) +
                                   L" (" + StringUtils::ToWide(diag.justification) + L")";
            SetWindowTextW(m_lblDiagStatus, diagTxt.c_str());
        } else {
            SetWindowTextW(m_lblDiagStatus, L"Hardware operando em fluxo ideal (sem gargalos detectados).");
        }
    }
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Explorer reiniciou: o Windows derruba todo icone de bandeja e anuncia "TaskbarCreated".
    // Sem este re-add o icone some para sempre (ate reiniciar o Opit-Core), com tooltip e modo
    // atuais preservados porque m_nid ainda guarda o ultimo estado.
    if (s_msgTaskbarCreated != 0 && msg == s_msgTaskbarCreated && m_nid.hWnd == hwnd) {
        Shell_NotifyIconW(NIM_ADD, &m_nid);
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        CreateTabControls(hwnd);
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (w > 0 && h > 0) {
            LayoutControls(w, h);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* pMM = reinterpret_cast<MINMAXINFO*>(lParam);
        pMM->ptMinTrackSize.x = 880;
        pMM->ptMinTrackSize.y = 660;
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG: {
        HDC hdcStatic = (HDC)wParam;
        return ui::Theme::HandleCtlColorStatic(hdcStatic, (HWND)lParam, ui::Theme::TextPrimary, ui::Theme::BgDark);
    }

    case WM_CTLCOLOREDIT:
        return ui::Theme::HandleCtlColorEdit((HDC)wParam, (HWND)lParam);

    case WM_CTLCOLORLISTBOX:
        return ui::Theme::HandleCtlColorListBox((HDC)wParam, (HWND)lParam);

    case WM_CTLCOLORBTN:
        return ui::Theme::HandleCtlColorBtn((HDC)wParam, (HWND)lParam);

    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE) {
            UpdatePeriodicData();
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR* pnm = reinterpret_cast<NMHDR*>(lParam);
        if (pnm->hwndFrom == m_tab && pnm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(m_tab);
            SwitchTab(sel);
            return 0;
        }
        if (pnm->hwndFrom == m_listServices && pnm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* pnmv = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                int selItem = pnmv->iItem;
                if (selItem >= 0 && m_comboSvcRule) {
                    WCHAR svcNameBuf[128] = {0};
                    ListView_GetItemText(m_listServices, selItem, 0, svcNameBuf, 128);
                    std::string svcName = StringUtils::ToUtf8(svcNameBuf);
                    std::string rule = ServiceManager::Instance().GetServiceRule(svcName);

                    int ruleIdx = 0; // default
                    if (rule == "auto") ruleIdx = 1;
                    else if (rule == "pause_on_game") ruleIdx = 2;
                    else if (rule == "pause_on_eco") ruleIdx = 3;
                    else if (rule == "pause_on_high_cpu") ruleIdx = 4;
                    else if (rule == "always_paused") ruleIdx = 5;
                    else if (rule == "never_pause") ruleIdx = 6;
                    SendMessageW(m_comboSvcRule, CB_SETCURSEL, ruleIdx, 0);

                    bool isCrit = ServiceManager::Instance().IsCriticalService(svcName);
                    if (m_btnPauseSvcNow) EnableWindow(m_btnPauseSvcNow, !isCrit);
                    if (m_btnApplySvcRule) EnableWindow(m_btnApplySvcRule, !isCrit);
                }
            }
            return 0;
        }
        break;
    }

    case WM_OPIT_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) {
            Show(!m_visible);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Abrir Opit-Core");
            AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE, m_visible ? L"Ocultar janela" : L"Mostrar janela");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            // O teto do Eco e configuravel (bandas Min/Max): o rotulo le o valor salvo em vez de
            // mentir um numero fixo que a usuaria pode ter mudado na janela avancada.
            {
                const uint32_t ecoTop = ModuleAdvancedSettings::Instance().cpuMinMax.ecoPlan.maxPercentAC;
                std::wstring ecoTip = L"Perfil: Eco (" + std::to_wstring(ecoTop) + L"%)";
                AppendMenuW(menu, MF_STRING, ID_TRAY_PROFILE_ECO, ecoTip.c_str());
            }
            {
                const uint32_t balTop = ModuleAdvancedSettings::Instance().cpuMinMax.balancedPlan.maxPercentAC;
                std::wstring balTip = L"Perfil: Balanceado (" + std::to_wstring(balTop) + L"%)";
                AppendMenuW(menu, MF_STRING, ID_TRAY_PROFILE_BAL, balTip.c_str());
            }
            {
                const uint32_t highTop = ModuleAdvancedSettings::Instance().cpuMinMax.highPerfPlan.maxPercentAC;
                std::wstring highTip = L"Perfil: Alto Desempenho (" + std::to_wstring(highTop) + L"%)";
                AppendMenuW(menu, MF_STRING, ID_TRAY_PROFILE_HIGH, highTip.c_str());
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE_AI, UnifiedSettingsModel::Instance().smartOrchestrator ? L"Inteligencia IA: [LIGADA]" : L"Inteligencia IA: [DESLIGADA]");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_UNIFIED, L"Painel Unificado de Configuracoes...");
            AppendMenuW(menu, MF_STRING, ID_BTN_OPEN_FANS_GRAPH, L"📈 Editor Grafico de Curvas de Ventoinha...");
            AppendMenuW(menu, MF_STRING, ID_BTN_OPEN_FANS, L"⚙️ Modulo de Ventoinhas e Sensores...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Sair");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN:
            Show(true);
            return 0;
        case ID_TRAY_TOGGLE:
            Show(!m_visible);
            return 0;
        case ID_TRAY_PROFILE_ECO:
            CoreRuntime::Instance().ApplyProfile(Config::Profile::Eco);
            SendMessageW(m_comboProfiles, CB_SETCURSEL, 0, 0);
            UpdatePeriodicData();
            return 0;
        case ID_TRAY_PROFILE_BAL:
            CoreRuntime::Instance().ApplyProfile(Config::Profile::Balanced);
            SendMessageW(m_comboProfiles, CB_SETCURSEL, 1, 0);
            UpdatePeriodicData();
            return 0;
        case ID_TRAY_PROFILE_HIGH:
            CoreRuntime::Instance().ApplyProfile(Config::Profile::HighPerformance);
            SendMessageW(m_comboProfiles, CB_SETCURSEL, 2, 0);
            UpdatePeriodicData();
            return 0;
        case ID_TRAY_TOGGLE_AI: {
            bool next = !UnifiedSettingsModel::Instance().smartOrchestrator;
            UnifiedSettingsModel::Instance().smartOrchestrator = next;
            UnifiedSettingsModel::Instance().Save();
            UpdatePeriodicData();
            return 0;
        }
        case ID_TRAY_UNIFIED:
        case ID_BTN_OPEN_UNIFIED:
            UnifiedSettingsWindow::Instance().ShowModal(hwnd);
            return 0;
        case ID_BTN_OPEN_AI:
            AdvancedModuleDialogs::ShowAiAssistantDialog(hwnd);
            return 0;
        case ID_BTN_OPEN_SERVICES_CFG:
            AdvancedModuleDialogs::ShowServicesDialog(hwnd);
            RefreshServicesList();
            return 0;
        case ID_BTN_OPEN_FANS_GRAPH:
            FanCurveEditorDialog::Show(hwnd);
            return 0;
        case ID_BTN_OPEN_FANS:
            AdvancedModuleDialogs::ShowFansDialog(hwnd);
            return 0;

        case ID_EDIT_SVC_SEARCH:
            if (HIWORD(wParam) == EN_CHANGE) {
                WCHAR searchBuf[256] = {0};
                GetWindowTextW(m_editSvcSearch, searchBuf, 256);
                RefreshServicesList(StringUtils::ToUtf8(searchBuf));
            }
            return 0;

        case ID_BTN_REFRESH_SVC_LIST: {
            WCHAR searchBuf[256] = {0};
            if (m_editSvcSearch) GetWindowTextW(m_editSvcSearch, searchBuf, 256);
            RefreshServicesList(StringUtils::ToUtf8(searchBuf));
            return 0;
        }

        case ID_BTN_CONFIG_SVC_TRIGGERS:
            AdvancedModuleDialogs::ShowServicesDialog(hwnd);
            RefreshServicesList();
            return 0;

        case ID_BTN_APPLY_SVC_RULE: {
            int selItem = ListView_GetNextItem(m_listServices, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR svcNameBuf[128] = {0};
                ListView_GetItemText(m_listServices, selItem, 0, svcNameBuf, 128);
                std::string svcName = StringUtils::ToUtf8(svcNameBuf);

                if (ServiceManager::Instance().IsCriticalService(svcName)) {
                    MessageBoxW(hwnd, L"Este servico e critico para a integridade do sistema operacional e nao pode ter suas regras alteradas.", L"Opit-Core - Protecao de Sistema", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                int selRule = (int)SendMessageW(m_comboSvcRule, CB_GETCURSEL, 0, 0);
                std::string ruleKey = "default";
                if (selRule == 1) ruleKey = "auto";
                else if (selRule == 2) ruleKey = "pause_on_game";
                else if (selRule == 3) ruleKey = "pause_on_eco";
                else if (selRule == 4) ruleKey = "pause_on_high_cpu";
                else if (selRule == 5) ruleKey = "always_paused";
                else if (selRule == 6) ruleKey = "never_pause";

                ServiceManager::Instance().SetServiceRule(svcName, ruleKey);
                RefreshServicesList();
                SetWindowTextW(m_lblSvcStatus, (L"Regra '" + StringUtils::ToWide(ruleKey) + L"' salva com sucesso para o servico " + svcNameBuf).c_str());
                MessageBoxW(hwnd, (L"Regra de servico atualizada com sucesso para: " + std::wstring(svcNameBuf)).c_str(), L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"Selecione um servico na lista primeiro.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_APPLY_ALL_SVC_RULE: {
            int selRule = (int)SendMessageW(m_comboSvcRule, CB_GETCURSEL, 0, 0);
            if (selRule < 0 || selRule > 6) selRule = 0;

            std::string ruleKey = "default";
            std::wstring ruleName = L"Padrao do Sistema (Automatico)";
            if (selRule == 1) { ruleKey = "auto"; ruleName = L"Inteligente Opit-Core (Auto IA)"; }
            else if (selRule == 2) { ruleKey = "pause_on_game"; ruleName = L"Pausar em Modo Jogo"; }
            else if (selRule == 3) { ruleKey = "pause_on_eco"; ruleName = L"Pausar em Modo Eco"; }
            else if (selRule == 4) { ruleKey = "pause_on_high_cpu"; ruleName = L"Pausar Sob Alta CPU (>80%)"; }
            else if (selRule == 5) { ruleKey = "always_paused"; ruleName = L"Sempre Pausado (Desativado)"; }
            else if (selRule == 6) { ruleKey = "never_pause"; ruleName = L"Nunca Pausar (Protegido)"; }

            std::wstring confirmMsg = L"Deseja aplicar a regra:\r\n\r\n\"" + ruleName +
                                      L"\"\r\n\r\npara TODOS os servicos otimizaveis do sistema?\r\n\r\n"
                                      L"(Servicos vitais e criticos do Windows continuarao protegidos automaticamente).";

            if (MessageBoxW(hwnd, confirmMsg.c_str(), L"Opit-Core — Aplicar Regra em Lote", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return 0;
            }

            auto allServices = ServiceManager::Instance().EnumerateAllServices("");
            int updatedCount = 0;
            int protectedCount = 0;

            for (const auto& s : allServices) {
                if (s.isCritical || ServiceManager::Instance().IsCriticalService(s.name)) {
                    protectedCount++;
                    continue;
                }
                ServiceManager::Instance().SetServiceRule(s.name, ruleKey);
                updatedCount++;
            }

            RefreshServicesList();

            std::wstring resultMsg = L"Regra em lote aplicada com sucesso!\r\n\r\n"
                                     L"• Servicos otimizaveis atualizados: " + std::to_wstring(updatedCount) + L"\r\n"
                                     L"• Servicos vitais mantidos protegidos: " + std::to_wstring(protectedCount);

            SetWindowTextW(m_lblSvcStatus, (L"Regra em lote '" + StringUtils::ToWide(ruleKey) + L"' aplicada a " +
                           std::to_wstring(updatedCount) + L" servicos.").c_str());

            MessageBoxW(hwnd, resultMsg.c_str(), L"Opit-Core — Servicos Atualizados", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        case ID_BTN_PAUSE_SVC_NOW: {
            int selItem = ListView_GetNextItem(m_listServices, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR svcNameBuf[128] = {0};
                ListView_GetItemText(m_listServices, selItem, 0, svcNameBuf, 128);
                std::string svcName = StringUtils::ToUtf8(svcNameBuf);

                if (ServiceManager::Instance().IsCriticalService(svcName)) {
                    MessageBoxW(hwnd, L"Este servico e critico e nao pode ser pausado.", L"Opit-Core", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                bool ok = ServiceManager::Instance().PauseService(svcName);
                RefreshServicesList();
                if (ok) {
                    MessageBoxW(hwnd, (L"Servico " + std::wstring(svcNameBuf) + L" pausado com sucesso!").c_str(), L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, (L"Falha ao pausar servico " + std::wstring(svcNameBuf) + L" (pode requerer privilegios administrativos adicionais ou estar em uso).").c_str(), L"Opit-Core", MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, L"Selecione um servico na lista primeiro.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_RESUME_SVC_NOW: {
            int selItem = ListView_GetNextItem(m_listServices, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR svcNameBuf[128] = {0};
                ListView_GetItemText(m_listServices, selItem, 0, svcNameBuf, 128);
                std::string svcName = StringUtils::ToUtf8(svcNameBuf);

                bool ok = ServiceManager::Instance().ResumeService(svcName);
                RefreshServicesList();
                if (ok) {
                    MessageBoxW(hwnd, (L"Servico " + std::wstring(svcNameBuf) + L" retomado com sucesso!").c_str(), L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, (L"Falha ao retomar servico " + std::wstring(svcNameBuf) + L".").c_str(), L"Opit-Core", MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, L"Selecione um servico na lista primeiro.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_RESTORE_ALL_SVCS: {
            if (MessageBoxW(hwnd, L"Deseja restaurar todos os servicos pausados para a execucao normal agora?", L"Restaurar Servicos", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ServiceManager::Instance().RestoreAllServicesNow();
                RefreshServicesList();
                MessageBoxW(hwnd, L"Todos os servicos gerenciados pelo Opit-Core foram restaurados com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        case ID_BTN_DEEP_ECO: {
            bool current = ModeCoordinator::Instance().IsDeepEcoModeActive();
            std::string msg;
            ModeCoordinator::Instance().SetDeepEcoMode(!current, msg);
            UpdatePeriodicData();
            MessageBoxW(hwnd, StringUtils::ToWide(msg).c_str(), L"Opit-Core - Economia Maxima", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case ID_BTN_TOGGLE_GAMEMODE: {
            bool current = ModeCoordinator::Instance().IsGameModeActive();
            std::string msg;
            ModeCoordinator::Instance().SetManualGameMode(!current, msg);
            UpdatePeriodicData();
            MessageBoxW(hwnd, StringUtils::ToWide(msg).c_str(), L"Opit-Core - Modo Jogo", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case ID_BTN_CPU_ADVANCED: {
            AdvancedModuleDialogs::ShowDialog(1, hwnd);
            return 0;
        }
        case ID_BTN_GPU_ADVANCED: {
            AdvancedModuleDialogs::ShowDialog(4, hwnd);
            return 0;
        }
        case ID_TRAY_EXIT:
            Shell_NotifyIconW(NIM_DELETE, &m_nid);
            m_quitRequested = true;
            PostQuitMessage(0);
            return 0;

        case ID_BTN_APPLY_PROFILE: {
            int sel = (int)SendMessageW(m_comboProfiles, CB_GETCURSEL, 0, 0);
            Config::Profile p = Config::Profile::Balanced;
            if (sel == 0) p = Config::Profile::Eco;
            else if (sel == 2) p = Config::Profile::HighPerformance;

            CoreRuntime::Instance().ApplyProfile(p);
            SetWindowTextW(m_lblProfileStatus, L"Status: Perfil aplicado com sucesso!");
            PopulatePowerSchemes();
            UpdatePeriodicData();
            MessageBoxW(hwnd, L"Perfil de desempenho aplicado com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        case ID_BTN_APPLY_THROTTLE: {
            WCHAR buf[32] = {0};
            GetWindowTextW(m_editCpuThrottle, buf, 32);
            int val = _wtoi(buf);
            if (val >= 20 && val <= 100) {
                CoreRuntime::Instance().GetPowerManager().SetProcessorThrottleLimit((uint32_t)val);
                Config::Instance().SetProcessorThrottleCap(val);

                // Sincroniza e protege nas configuracoes avancadas para que nenhum plano reverta
                auto& adv = ModuleAdvancedSettings::Instance();
                adv.cpuMinMax.maxPercentAC = (uint32_t)val;
                adv.Save();

                // Propaga para todos os esquemas do Windows
                auto schemes = CoreRuntime::Instance().GetPowerManager().ListSchemes();
                for (const auto& sch : schemes) {
                    CoreRuntime::Instance().GetPowerManager().WriteSchemeProcessorThrottle(
                        sch.guid, adv.cpuMinMax.minPercentAC, adv.cpuMinMax.maxPercentAC,
                        adv.cpuMinMax.minPercentDC, adv.cpuMinMax.maxPercentDC);
                }

                SetWindowTextW(m_lblThrottleStatus, (L"Limite de processador ajustado para: " + std::to_wstring(val) + L"%").c_str());
                MessageBoxW(hwnd, L"Limite maximo de frequencia da CPU aplicado e fixado em todos os planos do Windows!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"Por favor informe um valor entre 20 e 100.", L"Opit-Core", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        case ID_BTN_OPTIMIZE_RAM: {
            bool ok = ProcessManager::OptimizeMemory(0);
            if (ok) {
                SetWindowTextW(m_lblRamOptStatus, L"Memoria limpa com sucesso. Paginas inativas liberadas dos processos.");
                UpdatePeriodicData();
                MessageBoxW(hwnd, L"Otimizacao de memoria concluida com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            } else {
                SetWindowTextW(m_lblRamOptStatus, L"Falha ao solicitar otimizacao de memoria.");
            }
            return 0;
        }

        case ID_BTN_SET_POWER_SCHEME: {
            int sel = (int)SendMessageW(m_comboPowerSchemes, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                auto schemes = CoreRuntime::Instance().GetPowerManager().ListSchemes();
                if (sel >= 0 && sel < (int)schemes.size()) {
                    CoreRuntime::Instance().GetPowerManager().SetActiveScheme(schemes[sel].guid);
                    PopulatePowerSchemes();
                    SetWindowTextW(m_lblPowerStatus, L"Status: Plano de energia do Windows alterado com sucesso!");
                    MessageBoxW(hwnd, L"Plano de energia ativado!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }

        case ID_BTN_REFRESH_PROC:
            RefreshProcessList();
            return 0;

        case ID_BTN_SET_PRIORITY: {
            int selItem = ListView_GetNextItem(m_listProcesses, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR pidBuf[32] = {0};
                ListView_GetItemText(m_listProcesses, selItem, 0, pidBuf, 32);
                uint32_t pid = (uint32_t)_wtoi(pidBuf);

                int prioSel = (int)SendMessageW(m_comboPriority, CB_GETCURSEL, 0, 0);
                uint32_t prioClass = NORMAL_PRIORITY_CLASS;
                if (prioSel == 0) prioClass = IDLE_PRIORITY_CLASS;
                else if (prioSel == 1) prioClass = BELOW_NORMAL_PRIORITY_CLASS;
                else if (prioSel == 2) prioClass = NORMAL_PRIORITY_CLASS;
                else if (prioSel == 3) prioClass = ABOVE_NORMAL_PRIORITY_CLASS;
                else if (prioSel == 4) prioClass = HIGH_PRIORITY_CLASS;

                bool ok = ProcessManager::SetPriority(pid, prioClass);
                if (ok) {
                    RefreshProcessList();
                    MessageBoxW(hwnd, L"Prioridade do processo alterada com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Nao foi possivel alterar a prioridade (processo protegido ou acesso negado).", L"Opit-Core", MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, L"Selecione um processo na lista primeiro.", L"Opit-Core", MB_OK);
            }
            return 0;
        }

        case ID_BTN_SET_AFFINITY: {
            int selItem = ListView_GetNextItem(m_listProcesses, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR pidBuf[32] = {0};
                ListView_GetItemText(m_listProcesses, selItem, 0, pidBuf, 32);
                uint32_t pid = (uint32_t)_wtoi(pidBuf);

                int affSel = (int)SendMessageW(m_comboAffinity, CB_GETCURSEL, 0, 0);
                uint64_t mask = 0xFFFFFFFFFFFFFFFFULL;
                if (affSel == 1) mask = 0x0F;       // Núcleos 0 a 3
                else if (affSel == 2) mask = 0x5555555555555555ULL; // Núcleos Pares (0, 2, 4...)
                else if (affSel == 3) mask = 0x01;  // Núcleo 0 apenas

                bool ok = ProcessManager::SetAffinity(pid, mask);
                if (ok) {
                    RefreshProcessList();
                    MessageBoxW(hwnd, L"Mascara de afinidade de nucleos aplicada com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Falha ao ajustar afinidade (processo protegido ou permissao insuficiente).", L"Opit-Core", MB_OK | MB_ICONWARNING);
                }
            } else {
                MessageBoxW(hwnd, L"Selecione um processo na lista primeiro.", L"Opit-Core", MB_OK);
            }
            return 0;
        }

        case ID_BTN_KILL_PROC: {
            int selItem = ListView_GetNextItem(m_listProcesses, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR pidBuf[32] = {0};
                ListView_GetItemText(m_listProcesses, selItem, 0, pidBuf, 32);
                uint32_t pid = (uint32_t)_wtoi(pidBuf);

                if (MessageBoxW(hwnd, L"Tem certeza que deseja finalizar este processo?", L"Confirmacao", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    bool ok = ProcessManager::Terminate(pid);
                    if (ok) {
                        RefreshProcessList();
                        MessageBoxW(hwnd, L"Processo finalizado com sucesso.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd, L"Nao foi possivel finalizar o processo (processo vital ou acesso negado).", L"Opit-Core", MB_OK | MB_ICONERROR);
                    }
                }
            }
            return 0;
        }

        case ID_BTN_ADD_GAME: {
            WCHAR buf[260] = {0};
            GetWindowTextW(m_editGameExe, buf, 260);
            std::string exe = StringUtils::ToUtf8(buf);
            exe = StringUtils::Trim(exe);
            AdvancedModuleDialogs::ShowGameProfileDialog(hwnd, exe);
            SetWindowTextW(m_editGameExe, L"");
            RefreshGameList();
            return 0;
        }

        case ID_BTN_EDIT_GAME: {
            int selItem = ListView_GetNextItem(m_listGames, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR buf[260] = {0};
                ListView_GetItemText(m_listGames, selItem, 0, buf, 260);
                std::string exe = StringUtils::ToUtf8(buf);
                AdvancedModuleDialogs::ShowGameProfileDialog(hwnd, exe);
                RefreshGameList();
            } else {
                MessageBoxW(hwnd, L"Selecione um jogo na lista para editar seu perfil de CPU/GPU.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_REMOVE_GAME: {
            int selItem = ListView_GetNextItem(m_listGames, -1, LVNI_SELECTED);
            if (selItem != -1) {
                WCHAR buf[260] = {0};
                ListView_GetItemText(m_listGames, selItem, 0, buf, 260);
                std::string exe = StringUtils::ToUtf8(buf);
                CoreRuntime::Instance().GetGameDetector().RemoveGame(exe);
                DatabaseManager::Instance().RemoveGameRule(exe);
                Config::Instance().RemoveCustomGame(exe);
                RefreshGameList();
                MessageBoxW(hwnd, L"Jogo removido da lista.", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_REFRESH_MON:
            RefreshMonitoringHistory();
            return 0;

        case ID_BTN_CLEAR_MON:
            if (MessageBoxW(hwnd, L"Deseja limpar todos os registros do historico no SQLite?", L"Confirmar", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DatabaseManager::Instance().ClearMetricsHistory();
                RefreshMonitoringHistory();
            }
            return 0;

        case ID_BTN_APPLY_THERMAL: {
            WCHAR buf[32] = {0};
            GetWindowTextW(m_editThermalLimit, buf, 32);
            double limit = (double)_wtoi(buf);
            if (limit >= 50.0 && limit <= 100.0) {
                bool enabled = (SendMessageW(m_chkThermalEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
                Config::Instance().SetThermalLimitC(limit);
                Config::Instance().SetThermalProtectionEnabled(enabled);
                CoreRuntime::Instance().GetThermalProtection().SetEnabled(enabled);
                SetWindowTextW(m_lblThermalActionStatus, (L"Limite configurado para: " + std::to_wstring((int)limit) + L" °C. Status: " + (enabled ? L"Ativo" : L"Desativado")).c_str());
                MessageBoxW(hwnd, L"Configuracoes termicas atualizadas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"Limite termico deve estar entre 50°C e 100°C.", L"Opit-Core", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        case ID_BTN_SAVE_CONFIG: {
            bool startWin = (SendMessageW(m_chkStartWindows, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool startWinAdmin = (SendMessageW(m_chkStartWindowsAdmin, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool startMin = (SendMessageW(m_chkStartMinimized, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool closeTray = (SendMessageW(m_chkCloseToTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool disTele = (SendMessageW(m_chkDisableTelemetry, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool smartSvc = (SendMessageW(m_chkSmartGamingServices, BM_GETCHECK, 0, 0) == BST_CHECKED);

            int intSel = (int)SendMessageW(m_comboInterval, CB_GETCURSEL, 0, 0);
            int intervalMs = 1000;
            if (intSel == 0) intervalMs = 500;
            else if (intSel == 2) intervalMs = 2000;

            Config::Instance().SetStartWithWindows(startWin || startWinAdmin);
            Config::Instance().SetStartMinimized(startMin);
            Config::Instance().SetCloseToTray(closeTray);
            Config::Instance().SetSampleIntervalMs(intervalMs);

            auto& m = UnifiedSettingsModel::Instance();
            m.disableMicrosoftTelemetry = disTele;
            m.smartGamingServiceOptimization = smartSvc;
            m.Save();
            ServiceManager::Instance().SetMicrosoftTelemetryDisabled(disTele);

            // Gerenciar inicialização com o Windows (Agendador de Tarefas com Elevação vs HKCU Run)
            if (startWinAdmin) {
                if (!ElevationHelper::IsElevated()) {
                    MessageBoxW(hwnd,
                        L"Para configurar a inicializacao automatica como Administrador (sem avisos do UAC),\n"
                        L"o Opit-Core precisa estar sendo executado como Administrador no momento da alteracao.\n\n"
                        L"Por favor, execute o programa como Administrador para ativar essa opcao.",
                        L"Opit-Core — Permissao de Administrador Necessaria", MB_OK | MB_ICONWARNING);
                    SendMessageW(m_chkStartWindowsAdmin, BM_SETCHECK, BST_UNCHECKED, 0);
                } else {
                    bool ok = ElevationHelper::SetRunAtStartupAdmin(true);
                    if (ok) {
                        SendMessageW(m_chkStartWindows, BM_SETCHECK, BST_UNCHECKED, 0);
                        startWin = false;
                    }
                }
            } else {
                ElevationHelper::SetRunAtStartupAdmin(false);
                ElevationHelper::SetRunAtStartup(startWin);
            }

            Config::Instance().Save(Config::Instance().GetConfigPath());
            SetWindowTextW(m_lblConfigStatus, L"Preferencias salvas com sucesso!");
            MessageBoxW(hwnd, L"Configuracoes salvas e aplicadas!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        case ID_BTN_RESET_DEFAULTS: {
            Config::Instance().SetProfile(Config::Profile::Balanced);
            Config::Instance().SetSampleIntervalMs(1000);
            Config::Instance().SetThermalLimitC(80.0);
            Config::Instance().SetThermalProtectionEnabled(true);
            Config::Instance().SetAutoGameModeEnabled(true);
            Config::Instance().SetCloseToTray(true);
            Config::Instance().SetStartMinimized(false);
            Config::Instance().SetStartWithWindows(false);

            SendMessageW(m_chkCloseToTray, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(m_chkStartMinimized, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(m_chkStartWindows, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(m_chkStartWindowsAdmin, BM_SETCHECK, BST_UNCHECKED, 0);
            ElevationHelper::SetRunAtStartup(false);
            ElevationHelper::SetRunAtStartupAdmin(false);

            SendMessageW(m_comboInterval, CB_SETCURSEL, 1, 0);
            SetWindowTextW(m_editThermalLimit, L"80");

            Config::Instance().Save(Config::Instance().GetConfigPath());
            MessageBoxW(hwnd, L"Padroes restaurados!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        case ID_BTN_REFRESH_LOGS:
            RefreshLogs();
            return 0;

        case ID_BTN_COPY_DIAG: {
            auto hw = CoreRuntime::Instance().GetHardwareInfo();
            std::wstringstream ss;
            ss << L"=== OPIT-CORE DIAGNOSTICO DO SISTEMA ===" << L"\r\n";
            ss << L"Sistema Operacional: " << StringUtils::ToWide(hw.osName) << L"\r\n";
            ss << L"Privilegios: " << (ElevationHelper::IsElevated() ? L"Administrador" : L"Usuario Padrao") << L"\r\n";
            ss << L"Processador: " << StringUtils::ToWide(hw.cpu.brand) << L" (" << hw.cpu.physicalCores << L"C/" << hw.cpu.logicalThreads << L"T)\r\n";
            if (hw.documentedCpu.found) {
                ss << L"Base Oficial da CPU: " << StringUtils::ToWide(hw.documentedCpu.model) 
                   << L" (Base: " << StringUtils::ToWide(hw.documentedCpu.clockBase)
                   << L" | Boost: " << StringUtils::ToWide(hw.documentedCpu.clockBoost)
                   << L" | TDP: " << StringUtils::ToWide(hw.documentedCpu.tdp) << L")\r\n";
            }
            ss << L"Memoria RAM: " << StringUtils::ToWide(StringUtils::FormatBytes(hw.memory.totalPhysicalBytes)) << L"\r\n";
            ss << L"Banco de Dados: " << StringUtils::ToWide(DatabaseManager::Instance().GetDatabasePath()) << L"\r\n";
            ss << L"Versao Schema SQLite: " << DatabaseManager::Instance().GetSchemaVersion() << L"\r\n";
            for (const auto& g : hw.gpus) {
                ss << L"GPU: " << StringUtils::ToWide(g.description) << L" (" << g.dedicatedVideoMemoryMB << L" MB VRAM)\r\n";
            }
            auto diag = AdaptiveEngine::Instance().GetLatestDiagnosis();
            if (diag.primaryType != BottleneckType::None) {
                ss << L"Diagnostico de Gargalo: " << StringUtils::ToWide(BottleneckTypeToString(diag.primaryType))
                   << L" (" << StringUtils::ToWide(diag.justification) << L")\r\n";
            } else {
                ss << L"Diagnostico de Gargalo: Nenhum gargalo critico ativo detectado\r\n";
            }

            std::wstring text = ss.str();
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                size_t sz = (text.length() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem) {
                    memcpy(GlobalLock(hMem), text.c_str(), sz);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
                MessageBoxW(hwnd, L"Diagnostico copiado para a area de transferencia!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case ID_BTN_COPY_PIX: {
            const std::wstring pix = L"alexcstos@gmail.com";
            bool copied = false;
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                size_t sz = (pix.length() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem) {
                    memcpy(GlobalLock(hMem), pix.c_str(), sz);
                    GlobalUnlock(hMem);
                    // Em sucesso a area de transferencia passa a ser dona do bloco: nao se libera.
                    copied = SetClipboardData(CF_UNICODETEXT, hMem) != nullptr;
                    if (!copied) GlobalFree(hMem);
                }
                CloseClipboard();
            }
            if (copied) {
                if (m_lblAboutStatus) SetWindowTextW(m_lblAboutStatus, L"Chave Pix copiada. Obrigado de verdade.");
            } else {
                MessageBoxW(hwnd, L"Nao foi possivel copiar para a area de transferencia.", L"Opit-Core", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        }
        break;

    case WM_CLOSE:
        if (Config::Instance().GetCloseToTray()) {
            Show(false);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_UPDATE);
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_quitRequested = true;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace opitcore
