// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "UnifiedSettingsWindow.h"
#include "AdvancedModuleDialogs.h"
#include "Theme.h"
#include "core/UnifiedSettingsModel.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/CoreRuntime.h"
#include "core/Logger.h"
#include "utils/StringUtils.h"

#include <shellapi.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace opitcore {

static const UINT ID_UNIFIED_BTN_OK = 6001;
static const UINT ID_UNIFIED_BTN_SAVE = 6002;
static const UINT ID_UNIFIED_BTN_CANCEL = 6003;
static const UINT ID_UNIFIED_ADV_BASE = 6100;

UnifiedSettingsWindow& UnifiedSettingsWindow::Instance() {
    static UnifiedSettingsWindow instance;
    return instance;
}

UnifiedSettingsWindow::UnifiedSettingsWindow() = default;

UnifiedSettingsWindow::~UnifiedSettingsWindow() {
    if (m_hFontNormal) DeleteObject(m_hFontNormal);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontDesc) DeleteObject(m_hFontDesc);
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
}

void UnifiedSettingsWindow::UpdateScroll(int clientHeight) {
    if (!m_hwnd) return;
    int maxScroll = m_contentHeight - clientHeight;
    if (maxScroll < 0) maxScroll = 0;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = m_contentHeight;
    si.nPage = clientHeight;
    si.nPos = m_scrollY;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

bool UnifiedSettingsWindow::ShowModal(HWND hParent) {
    m_hParent = hParent;
    m_hInstance = GetModuleHandle(nullptr);
    m_scrollY = 0;

    // Carregar dados salvos no modelo
    UnifiedSettingsModel::Instance().Load();

    auto& theme = ui::Theme::Instance();
    m_hFontNormal = theme.fontBody;
    m_hFontBold = theme.fontBodyBold;
    m_hFontTitle = theme.fontSection;
    m_hFontDesc = theme.fontSmall;
    m_hBgBrush = theme.brushBgDark;

    int cxBig = GetSystemMetrics(SM_CXICON);
    int cyBig = GetSystemMetrics(SM_CYICON);
    int cxSm = GetSystemMetrics(SM_CXSMICON);
    int cySm = GetSystemMetrics(SM_CYSMICON);
    if (cxSm < 16) cxSm = 16;
    if (cySm < 16) cySm = 16;

    HICON hIconBig = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, cxBig, cyBig, LR_SHARED);
    HICON hIconSm = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, cxSm, cySm, LR_SHARED);

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
    wc.lpfnWndProc = UnifiedSettingsWindow::WndProcStatic;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"OpitCoreUnifiedSettingsClass";
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_hBgBrush;

    RegisterClassExW(&wc);

    // Calcular altura responsiva respeitando a área de trabalho do monitor
    RECT rcWork{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxAvailableH = rcWork.bottom - rcWork.top - 40;
    int winW = 980;
    int winH = std::min(m_contentHeight + 40, maxAvailableH);

    RECT rcParent{};
    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;
    if (hParent && GetWindowRect(hParent, &rcParent)) {
        posX = rcParent.left + (rcParent.right - rcParent.left - winW) / 2;
        posY = rcParent.top + (rcParent.bottom - rcParent.top - winH) / 2;
    }
    if (posX < rcWork.left + 10) posX = rcWork.left + 10;
    if (posY < rcWork.top + 10) posY = rcWork.top + 10;

    DWORD winStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE;
    if (winH < m_contentHeight + 30) {
        winStyle |= WS_VSCROLL;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_CONTROLPARENT,
        wc.lpszClassName,
        L"Opit-Core — Painel Unificado de Configuracoes do Orquestrador",
        winStyle,
        posX, posY, winW, winH,
        hParent, nullptr, m_hInstance, this
    );

    if (!m_hwnd) return false;

    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    ui::Theme::EnableImmersiveDarkMode(m_hwnd);

    RECT rcClient{};
    GetClientRect(m_hwnd, &rcClient);
    UpdateScroll(rcClient.bottom);

    if (hParent) {
        EnableWindow(hParent, FALSE);
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    // Loop modal
    MSG msg;
    while (IsWindow(m_hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hParent) {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }

    return true;
}

LRESULT CALLBACK UnifiedSettingsWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UnifiedSettingsWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<UnifiedSettingsWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<UnifiedSettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void UnifiedSettingsWindow::CreateControls(HWND hwnd) {
    auto setFont = [this](HWND h, HFONT f = nullptr) {
        SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : m_hFontNormal), TRUE);
        return h;
    };

    // =========================================================================
    // 1. SEÇÃO DE ATIVAÇÃO GLOBAL NO TOPO (PINTADA COMO MODERN CARD NO WM_PAINT)
    // =========================================================================
    m_chkEnableOrchestrator = CreateWindowW(L"BUTTON", L"Activar orquestrador (aplica optimizacoes no sistema)",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            36, 44, 430, 24, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkEnableOrchestrator, m_hFontBold);

    m_chkSmartOrchestrator = CreateWindowW(L"BUTTON", L"Orquestrador inteligente",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           490, 44, 430, 24, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkSmartOrchestrator, m_hFontBold);

    // =========================================================================
    // 2. SEÇÃO DE MÓDULOS (GRID BALANCEADO E ESPAÇADO)
    // =========================================================================
    int rowH = 25;
    int yStart = 124;
    int leftX = 36;
    int rightX = 490;

    // --- Coluna Esquerda: Hardware, CPU, Energia & Privacidade ---
    m_chkModPowerPlans = CreateWindowW(L"BUTTON", L"Planos de energia", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       leftX, yStart, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModPowerPlans);

    m_chkModCpuMinMax = CreateWindowW(L"BUTTON", L"Min/Max CPU", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      leftX, yStart + rowH, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModCpuMinMax);

    m_chkModCoreParking = CreateWindowW(L"BUTTON", L"Core parking", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        leftX, yStart + rowH * 2, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModCoreParking);

    m_chkModAdaptiveHardware = CreateWindowW(L"BUTTON", L"Hardware adaptativo", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             leftX, yStart + rowH * 3, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModAdaptiveHardware);

    m_chkModDeepEco = CreateWindowW(L"BUTTON", L"Economia profunda", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    leftX, yStart + rowH * 4, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModDeepEco);

    m_chkPauseIdleDevicesEco = CreateWindowW(L"BUTTON", L"Pausar dispositivos e barramentos ociosos em Economia",
                                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             leftX, yStart + rowH * 5, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkPauseIdleDevicesEco);

    m_chkDisableTelemetry = CreateWindowW(L"BUTTON", L"Desativar Telemetria da Microsoft (Privacidade & Fundo)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          leftX, yStart + rowH * 6, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkDisableTelemetry);

    m_chkModPauseIndexingIdle = CreateWindowW(L"BUTTON", L"Pausar indexacao com o PC parado (modo manual)",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              leftX, yStart + rowH * 7, 430, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModPauseIndexingIdle);

    // --- Coluna Direita: GPU, Ventoinhas, Térmica, Processos & Serviços ---
    m_chkModGpuControl = CreateWindowW(L"BUTTON", L"Controlo GPU", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       rightX, yStart, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModGpuControl);

    m_chkModFanCurve = CreateWindowW(L"BUTTON", L"Ventoinhas (curva)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     rightX, yStart + rowH, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModFanCurve);

    m_chkModThermalGuardian = CreateWindowW(L"BUTTON", L"Guardiao termico", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            rightX, yStart + rowH * 2, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModThermalGuardian);

    m_chkModProcessControl = CreateWindowW(L"BUTTON", L"Controlo de processos", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           rightX, yStart + rowH * 3, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModProcessControl);

    m_chkModAppPriority = CreateWindowW(L"BUTTON", L"Prioridade de apps", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        rightX, yStart + rowH * 4, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModAppPriority);

    m_chkModSystemStability = CreateWindowW(L"BUTTON", L"Estabilidade do sistema (score + guia lag/stutter)",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            rightX, yStart + rowH * 5, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModSystemStability);

    m_chkModWindowsServices = CreateWindowW(L"BUTTON", L"Servicos Windows (parar em 2o plano - repoe ao sair)",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            rightX, yStart + rowH * 6, 440, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkModWindowsServices);

    m_chkSubSmartServices = CreateWindowW(L"BUTTON", L"Servicos inteligentes (auto: indexacao off em jogo)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          rightX + 20, yStart + rowH * 7, 420, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkSubSmartServices);

    m_chkSmartGamingServices = CreateWindowW(L"BUTTON", L"Otimizacao inteligente de servicos em jogos (Auto)",
                                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             rightX + 20, yStart + rowH * 8, 420, rowH, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_chkSmartGamingServices);

    // =========================================================================
    // 3. SEÇÃO "NÍVEL POR MÓDULO (MIN / MED / MAX / ULTRA)"
    // =========================================================================
    HWND lblInstruction = CreateWindowW(L"STATIC",
                                        L"Modo inteligente (auto) ligado: o orquestrador segue o Avancado. Desligado: usa so o nivel.",
                                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        36, 404, 890, 18, hwnd, nullptr, m_hInstance, nullptr);
    setFont(lblInstruction, m_hFontDesc);

    const wchar_t* moduleLabels[] = {
        L"Planos energia:",
        L"CPU min/max:",
        L"Core parking:",
        L"Processos:",
        L"GPU:",
        L"Ventoinhas:",
        L"Guardiao termico:",
        L"Estabilidade:",
        L"Servicos:"
    };

    const wchar_t* moduleHints[] = {
        L"Balanceia consumo e performance do sistema",
        L"Limites de frequencia e clocks de operacao",
        L"Desligamento dinamico de nucleos ociosos",
        L"Afinidade de threads, prioridades e I/O",
        L"Ajuste de tensao, clock e perfis graficos",
        L"Curvas termicas inteligentes e RPM",
        L"Protecao contra estrangulamento termico",
        L"Score de latencia e monitoramento DPC",
        L"Otimizacao de tarefas em segundo plano"
    };

    int rowY = 428;
    int spacingY = 28;
    m_moduleRows.clear();
    m_moduleRows.resize(9);

    for (int i = 0; i < 9; ++i) {
        ModuleRowControls row;
        row.lblName = CreateWindowW(L"STATIC", moduleLabels[i], WS_CHILD | WS_VISIBLE | SS_LEFT,
                                    36, rowY + 3, 140, 20, hwnd, nullptr, m_hInstance, nullptr);
        setFont(row.lblName, m_hFontBold);

        row.comboLevel = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       182, rowY, 95, 140, hwnd, nullptr, m_hInstance, nullptr);
        setFont(row.comboLevel);
        SendMessageW(row.comboLevel, CB_ADDSTRING, 0, (LPARAM)L"min");
        SendMessageW(row.comboLevel, CB_ADDSTRING, 0, (LPARAM)L"med");
        SendMessageW(row.comboLevel, CB_ADDSTRING, 0, (LPARAM)L"max");
        SendMessageW(row.comboLevel, CB_ADDSTRING, 0, (LPARAM)L"ultra");
        SendMessageW(row.comboLevel, CB_SETCURSEL, 1, 0); // med por padrão

        row.btnAdvanced = CreateWindowW(L"BUTTON", L"Avancado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        288, rowY, 105, 24, hwnd, (HMENU)(UINT_PTR)(ID_UNIFIED_ADV_BASE + i), m_hInstance, nullptr);
        setFont(row.btnAdvanced);

        row.chkAutoSmart = CreateWindowW(L"BUTTON", L"Modo inteligente (auto)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         406, rowY + 1, 200, 22, hwnd, nullptr, m_hInstance, nullptr);
        setFont(row.chkAutoSmart);

        row.lblHint = CreateWindowW(L"STATIC", moduleHints[i], WS_CHILD | WS_VISIBLE | SS_LEFT,
                                    616, rowY + 3, 310, 20, hwnd, nullptr, m_hInstance, nullptr);
        setFont(row.lblHint, m_hFontDesc);

        m_moduleRows[i] = row;
        rowY += spacingY;
    }

    // Rodapé descritivo de serviços
    HWND lblServicesDesc = CreateWindowW(L"STATIC",
                                         L"Servicos auto: Jogo/Desktop/Internet/Idle/Bateria (Avancado). min=off | med=indexacao | max=+Spooler | ultra=+telemetria.",
                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         36, 684, 890, 20, hwnd, nullptr, m_hInstance, nullptr);
    setFont(lblServicesDesc, m_hFontDesc);

    // =========================================================================
    // 4. SEÇÃO AGRUPAMENTO SVCHOST
    // =========================================================================
    m_comboSvchost = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   36, 758, 460, 120, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_comboSvchost);
    SendMessageW(m_comboSvchost, CB_ADDSTRING, 0, (LPARAM)L"Minimo: mais isolamento entre servicos. Mais processos svchost. Mais seguro.");
    SendMessageW(m_comboSvchost, CB_ADDSTRING, 0, (LPARAM)L"Padrao do Windows (Equilibrado)");
    SendMessageW(m_comboSvchost, CB_ADDSTRING, 0, (LPARAM)L"Maximo agrupamento (Economia de recursos, menos processos)");
    SendMessageW(m_comboSvchost, CB_SETCURSEL, 0, 0);

    m_lblSvchostDesc = CreateWindowW(L"STATIC",
                                     L"Ajusta a divisao de servicos por processo svchost.exe no kernel do Windows sem afetar arquivos.",
                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     510, 758, 420, 36, hwnd, nullptr, m_hInstance, nullptr);
    setFont(m_lblSvchostDesc, m_hFontDesc);

    // =========================================================================
    // 5. BARRA DE AÇÕES (RODAPÉ DA JANELA)
    // =========================================================================
    m_btnOk = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            600, 820, 105, 32, hwnd, (HMENU)(UINT_PTR)ID_UNIFIED_BTN_OK, m_hInstance, nullptr);
    setFont(m_btnOk, m_hFontBold);

    m_btnSave = CreateWindowW(L"BUTTON", L"Salvar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              715, 820, 105, 32, hwnd, (HMENU)(UINT_PTR)ID_UNIFIED_BTN_SAVE, m_hInstance, nullptr);
    setFont(m_btnSave, m_hFontBold);

    m_btnCancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                830, 820, 105, 32, hwnd, (HMENU)(UINT_PTR)ID_UNIFIED_BTN_CANCEL, m_hInstance, nullptr);
    setFont(m_btnCancel);

    // Aplicar estilo Dark Explorer a todos os controles filhos para um acabamento premium consistente
    EnumChildWindows(hwnd, [](HWND hChild, LPARAM) -> BOOL {
        ui::Theme::ApplyDarkExplorerTheme(hChild);
        return TRUE;
    }, 0);

    // Sincronizar dados do modelo para os componentes criados
    SyncModelToUI();
}

void UnifiedSettingsWindow::SyncModelToUI() {
    const auto& m = UnifiedSettingsModel::Instance();

    auto setChk = [](HWND h, bool v) {
        SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
    };

    auto setComboLevel = [](HWND h, const std::string& lvl) {
        int idx = 1; // med
        if (lvl == "min") idx = 0;
        else if (lvl == "med") idx = 1;
        else if (lvl == "max") idx = 2;
        else if (lvl == "ultra") idx = 3;
        SendMessageW(h, CB_SETCURSEL, idx, 0);
    };

    // 1. Ativação Global
    setChk(m_chkEnableOrchestrator, m.enableOrchestrator);
    setChk(m_chkSmartOrchestrator, m.smartOrchestrator);

    // 2. Módulos
    setChk(m_chkModPowerPlans, m.modPowerPlans);
    setChk(m_chkModCoreParking, m.modCoreParking);
    setChk(m_chkModGpuControl, m.modGpuControl);
    setChk(m_chkModThermalGuardian, m.modThermalGuardian);
    setChk(m_chkModAdaptiveHardware, m.modAdaptiveHardware);
    setChk(m_chkModWindowsServices, m.modWindowsServices);
    setChk(m_chkSubSmartServices, m.subSmartServices);
    setChk(m_chkModSystemStability, m.modSystemStability);
    setChk(m_chkModPauseIndexingIdle, m.modPauseIndexingIdle);
    setChk(m_chkDisableTelemetry, m.disableMicrosoftTelemetry);
    setChk(m_chkSmartGamingServices, m.smartGamingServiceOptimization);
    setChk(m_chkPauseIdleDevicesEco, m.pauseIdleDevicesEco);

    setChk(m_chkModCpuMinMax, m.modCpuMinMax);
    setChk(m_chkModProcessControl, m.modProcessControl);
    setChk(m_chkModFanCurve, m.modFanCurve);
    setChk(m_chkModDeepEco, m.modDeepEco);
    setChk(m_chkModAppPriority, m.modAppPriority);

    // 3. Níveis por módulo
    if (m_moduleRows.size() == 9) {
        setComboLevel(m_moduleRows[0].comboLevel, m.levelPowerPlans.level);
        setChk(m_moduleRows[0].chkAutoSmart, m.levelPowerPlans.autoSmartMode);

        setComboLevel(m_moduleRows[1].comboLevel, m.levelCpuMinMax.level);
        setChk(m_moduleRows[1].chkAutoSmart, m.levelCpuMinMax.autoSmartMode);

        setComboLevel(m_moduleRows[2].comboLevel, m.levelCoreParking.level);
        setChk(m_moduleRows[2].chkAutoSmart, m.levelCoreParking.autoSmartMode);

        setComboLevel(m_moduleRows[3].comboLevel, m.levelProcesses.level);
        setChk(m_moduleRows[3].chkAutoSmart, m.levelProcesses.autoSmartMode);

        setComboLevel(m_moduleRows[4].comboLevel, m.levelGpu.level);
        setChk(m_moduleRows[4].chkAutoSmart, m.levelGpu.autoSmartMode);

        setComboLevel(m_moduleRows[5].comboLevel, m.levelFans.level);
        setChk(m_moduleRows[5].chkAutoSmart, m.levelFans.autoSmartMode);

        setComboLevel(m_moduleRows[6].comboLevel, m.levelThermalGuardian.level);
        setChk(m_moduleRows[6].chkAutoSmart, m.levelThermalGuardian.autoSmartMode);

        setComboLevel(m_moduleRows[7].comboLevel, m.levelStability.level);
        setChk(m_moduleRows[7].chkAutoSmart, m.levelStability.autoSmartMode);

        setComboLevel(m_moduleRows[8].comboLevel, m.levelServices.level);
        setChk(m_moduleRows[8].chkAutoSmart, m.levelServices.autoSmartMode);
    }

    // 5. Agrupamento svchost
    SendMessageW(m_comboSvchost, CB_SETCURSEL, m.svchostGrouping, 0);
}

void UnifiedSettingsWindow::SyncUIToModel() {
    auto& m = UnifiedSettingsModel::Instance();

    auto isChecked = [](HWND h) -> bool {
        return (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
    };

    auto getComboLevel = [](HWND h) -> std::string {
        int idx = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        switch (idx) {
            case 0: return "min";
            case 1: return "med";
            case 2: return "max";
            case 3: return "ultra";
            default: return "med";
        }
    };

    // 1. Ativação Global
    m.enableOrchestrator = isChecked(m_chkEnableOrchestrator);
    m.smartOrchestrator = isChecked(m_chkSmartOrchestrator);

    // 2. Módulos
    m.modPowerPlans = isChecked(m_chkModPowerPlans);
    m.modCoreParking = isChecked(m_chkModCoreParking);
    m.modGpuControl = isChecked(m_chkModGpuControl);
    m.modThermalGuardian = isChecked(m_chkModThermalGuardian);
    m.modAdaptiveHardware = isChecked(m_chkModAdaptiveHardware);
    m.modWindowsServices = isChecked(m_chkModWindowsServices);
    m.subSmartServices = isChecked(m_chkSubSmartServices);
    m.modSystemStability = isChecked(m_chkModSystemStability);
    m.modPauseIndexingIdle = isChecked(m_chkModPauseIndexingIdle);
    m.disableMicrosoftTelemetry = isChecked(m_chkDisableTelemetry);
    m.smartGamingServiceOptimization = isChecked(m_chkSmartGamingServices);
    m.pauseIdleDevicesEco = isChecked(m_chkPauseIdleDevicesEco);
    ModuleAdvancedSettings::Instance().powerPlans.pauseIdleDevicesEco = m.pauseIdleDevicesEco;

    m.modCpuMinMax = isChecked(m_chkModCpuMinMax);
    m.modProcessControl = isChecked(m_chkModProcessControl);
    m.modFanCurve = isChecked(m_chkModFanCurve);
    m.modDeepEco = isChecked(m_chkModDeepEco);
    m.modAppPriority = isChecked(m_chkModAppPriority);

    // 3. Níveis
    if (m_moduleRows.size() == 9) {
        m.levelPowerPlans.level = getComboLevel(m_moduleRows[0].comboLevel);
        m.levelPowerPlans.autoSmartMode = isChecked(m_moduleRows[0].chkAutoSmart);

        m.levelCpuMinMax.level = getComboLevel(m_moduleRows[1].comboLevel);
        m.levelCpuMinMax.autoSmartMode = isChecked(m_moduleRows[1].chkAutoSmart);

        m.levelCoreParking.level = getComboLevel(m_moduleRows[2].comboLevel);
        m.levelCoreParking.autoSmartMode = isChecked(m_moduleRows[2].chkAutoSmart);

        m.levelProcesses.level = getComboLevel(m_moduleRows[3].comboLevel);
        m.levelProcesses.autoSmartMode = isChecked(m_moduleRows[3].chkAutoSmart);

        m.levelGpu.level = getComboLevel(m_moduleRows[4].comboLevel);
        m.levelGpu.autoSmartMode = isChecked(m_moduleRows[4].chkAutoSmart);

        m.levelFans.level = getComboLevel(m_moduleRows[5].comboLevel);
        m.levelFans.autoSmartMode = isChecked(m_moduleRows[5].chkAutoSmart);

        m.levelThermalGuardian.level = getComboLevel(m_moduleRows[6].comboLevel);
        m.levelThermalGuardian.autoSmartMode = isChecked(m_moduleRows[6].chkAutoSmart);

        m.levelStability.level = getComboLevel(m_moduleRows[7].comboLevel);
        m.levelStability.autoSmartMode = isChecked(m_moduleRows[7].chkAutoSmart);

        m.levelServices.level = getComboLevel(m_moduleRows[8].comboLevel);
        m.levelServices.autoSmartMode = isChecked(m_moduleRows[8].chkAutoSmart);
    }

    // 5. Agrupamento svchost
    m.svchostGrouping = (int)SendMessageW(m_comboSvchost, CB_GETCURSEL, 0, 0);
}

void UnifiedSettingsWindow::ShowAdvancedDialog(int moduleIndex) {
    AdvancedModuleDialogs::ShowDialog(moduleIndex, m_hwnd);
    SyncModelToUI();
}

LRESULT UnifiedSettingsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1; // Double buffering elimina cintilação

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        // Double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Fundo escuro uniforme da janela
        FillRect(memDC, &rcClient, ui::Theme::Instance().brushBgDark);

        // 1. Card Ativação Global
        RECT rcCard1 = { 20, 16 - m_scrollY, 944, 84 - m_scrollY };
        ui::Theme::DrawModernCard(memDC, rcCard1, L"ATIVAÇÃO GLOBAL DO ORQUESTRADOR", ui::Theme::AccentCyan);

        // 2. Card Módulos do Sistema
        RECT rcCard2 = { 20, 96 - m_scrollY, 944, 362 - m_scrollY };
        ui::Theme::DrawModernCard(memDC, rcCard2, L"MÓDULOS DO SISTEMA (RECURSOS ATIVADOS / DESATIVADOS)", ui::Theme::AccentBlue);

        // 3. Card Nível por Módulo
        RECT rcCard3 = { 20, 374 - m_scrollY, 944, 716 - m_scrollY };
        ui::Theme::DrawModernCard(memDC, rcCard3, L"NÍVEL POR MÓDULO (MIN / MED / MAX / ULTRA)", ui::Theme::AccentCyan);

        // 4. Card Agrupamento svchost
        RECT rcCard4 = { 20, 728 - m_scrollY, 944, 806 - m_scrollY };
        ui::Theme::DrawModernCard(memDC, rcCard4, L"AGRUPAMENTO SVCHOST (OTIMIZAÇÃO DE MEMÓRIA)", ui::Theme::AccentBlue);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        COLORREF textCol = ui::Theme::TextPrimary;
        if (hCtrl == m_lblSvchostDesc) {
            textCol = ui::Theme::TextSecondary;
        }
        for (const auto& row : m_moduleRows) {
            if (hCtrl == row.lblHint) {
                textCol = ui::Theme::TextSecondary;
                break;
            }
        }
        return ui::Theme::HandleCtlColorStatic(hdcStatic, hCtrl, textCol, ui::Theme::BgCard);
    }

    case WM_CTLCOLORBTN: {
        return ui::Theme::HandleCtlColorBtn((HDC)wParam, (HWND)lParam);
    }

    case WM_CTLCOLOREDIT: {
        return ui::Theme::HandleCtlColorEdit((HDC)wParam, (HWND)lParam);
    }

    case WM_CTLCOLORLISTBOX: {
        return ui::Theme::HandleCtlColorListBox((HDC)wParam, (HWND)lParam);
    }

    case WM_VSCROLL: {
        RECT rcClient{};
        GetClientRect(hwnd, &rcClient);
        int maxScroll = m_contentHeight - rcClient.bottom;
        if (maxScroll <= 0) return 0;

        int oldPos = m_scrollY;
        switch (LOWORD(wParam)) {
            case SB_LINEUP:   m_scrollY -= 30; break;
            case SB_LINEDOWN: m_scrollY += 30; break;
            case SB_PAGEUP:   m_scrollY -= rcClient.bottom / 2; break;
            case SB_PAGEDOWN: m_scrollY += rcClient.bottom / 2; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                m_scrollY = HIWORD(wParam);
                break;
        }

        m_scrollY = std::clamp(m_scrollY, 0, maxScroll);
        int dy = oldPos - m_scrollY;
        if (dy != 0) {
            ScrollWindowEx(hwnd, 0, dy, nullptr, nullptr, nullptr, nullptr,
                           SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            UpdateScroll(rcClient.bottom);
            UpdateWindow(hwnd);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        RECT rcClient{};
        GetClientRect(hwnd, &rcClient);
        int maxScroll = m_contentHeight - rcClient.bottom;
        if (maxScroll <= 0) return 0;

        int oldPos = m_scrollY;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        m_scrollY -= (delta / WHEEL_DELTA) * 40;
        m_scrollY = std::clamp(m_scrollY, 0, maxScroll);

        int dy = oldPos - m_scrollY;
        if (dy != 0) {
            ScrollWindowEx(hwnd, 0, dy, nullptr, nullptr, nullptr, nullptr,
                           SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            UpdateScroll(rcClient.bottom);
            UpdateWindow(hwnd);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);

        if (cmdId >= ID_UNIFIED_ADV_BASE && cmdId < ID_UNIFIED_ADV_BASE + 9) {
            int modIdx = cmdId - ID_UNIFIED_ADV_BASE;
            ShowAdvancedDialog(modIdx);
            return 0;
        }

        switch (cmdId) {
        case ID_UNIFIED_BTN_OK:
            SyncUIToModel();
            // O espelho de pauseIdleDevicesEco feito no SyncUIToModel e persistido aqui tambem:
            // sem isto a janela avancada de Planos de Energia reabria com o valor antigo do banco
            // e as duas telas divergiam depois do restart.
            ModuleAdvancedSettings::Instance().Save();
            UnifiedSettingsModel::Instance().Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();
            DestroyWindow(hwnd);
            return 0;

        case ID_UNIFIED_BTN_SAVE:
            SyncUIToModel();
            ModuleAdvancedSettings::Instance().Save();
            UnifiedSettingsModel::Instance().Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();
            MessageBoxW(hwnd, L"Configuracoes salvas e aplicadas ativamente pelo motor!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;

        case ID_UNIFIED_BTN_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace opitcore
