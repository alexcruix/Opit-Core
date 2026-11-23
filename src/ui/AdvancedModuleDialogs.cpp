// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "AdvancedModuleDialogs.h"
#include "Theme.h"
#include "ai/AiAssistantManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/NotificationManager.h"
#include <thread>
#include "core/CoreRuntime.h"
#include "core/DatabaseManager.h"
#include "core/Config.h"
#include "power/PowerManager.h"
#include "hardware/HardwareDetection.h"
#include "hardware/GpuControlManager.h"
#include "control/ModeCoordinator.h"
#include "metrics/SystemMonitor.h"
#include "platform/windows/ProcessManager.h"
#include "fans/FanControlManager.h"
#include "FanCurveEditorDialog.h"
#include "utils/StringUtils.h"
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>

namespace opitcore {

static const UINT ID_ADV_OK = 1001;
static const UINT ID_ADV_SAVE = 1002;
static const UINT ID_ADV_CANCEL = 1003;

static const UINT ID_PWR_BTN_FORCE_ECO = 1101;
static const UINT ID_CPU_COMBO_PLAN = 1102;
static const UINT ID_CPU_BTN_APPLY_PLAN = 1103;
static const UINT ID_CPU_BTN_RELOAD = 1104;
static const UINT ID_CPU_COMBO_CLASS = 1105;
static const UINT ID_CP_BTN_MAX_PARKING = 1105;
static const UINT ID_GPU_COMBO_SELECT = 1106;
static const UINT ID_GPU_COMBO_PROFILE = 1107;
static const UINT ID_GPU_BTN_APPLY = 1108;
static const UINT ID_GPU_BTN_RESTORE = 1109;

static const UINT ID_GAME_BTN_BROWSE = 1110;
static const UINT ID_GAME_COMBO_CPU_PROF = 1111;
static const UINT ID_GAME_COMBO_GPU_PROF = 1112;
static const UINT ID_GAME_BTN_SAVE = 1113;
static const UINT ID_GAME_BTN_CANCEL = 1114;
static const UINT ID_GAME_BTN_MAX_SILICON = 1115;

static const UINT ID_FAN_COMBO_DEV = 1201;
static const UINT ID_FAN_COMBO_MODE = 1202;
static const UINT ID_FAN_BTN_APPLY_SPEED = 1203;
static const UINT ID_FAN_BTN_RESTORE_FIRMWARE = 1204;
static const UINT ID_FAN_BTN_REFRESH = 1205;
static const UINT ID_FAN_BTN_GRAPH_EDITOR = 1206;

static HFONT GetDlgFont(bool bold = false) {
    auto& theme = ui::Theme::Instance();
    return bold ? theme.fontBodyBold : theme.fontBody;
}

static LRESULT CALLBACK AdvDialogRouterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC targetProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        targetProc = reinterpret_cast<WNDPROC>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(targetProc));
    }

    if (msg == WM_CTLCOLORSTATIC) {
        HWND hCtl = (HWND)lp;
        wchar_t clsName[32] = {0};
        if (GetClassNameW(hCtl, clsName, 32) && _wcsicmp(clsName, L"EDIT") == 0) {
            SetBkMode((HDC)wp, OPAQUE);
            SetBkColor((HDC)wp, ui::Theme::BgInput);
            SetTextColor((HDC)wp, ui::Theme::TextPrimary);
            return (INT_PTR)ui::Theme::Instance().brushBgInput;
        }
        HDC hdc = (HDC)wp;
        return ui::Theme::HandleCtlColorStatic(hdc, hwnd, ui::Theme::TextPrimary, ui::Theme::BgDark);
    }
    if (msg == WM_CTLCOLORDLG) {
        HDC hdc = (HDC)wp;
        return ui::Theme::HandleCtlColorStatic(hdc, hwnd, ui::Theme::TextPrimary, ui::Theme::BgDark);
    }
    if (msg == WM_CTLCOLOREDIT) {
        return ui::Theme::HandleCtlColorEdit((HDC)wp, hwnd);
    }
    if (msg == WM_CTLCOLORLISTBOX) {
        return ui::Theme::HandleCtlColorListBox((HDC)wp, hwnd);
    }
    if (msg == WM_CTLCOLORBTN) {
        return ui::Theme::HandleCtlColorBtn((HDC)wp, hwnd);
    }

    if (targetProc) {
        return targetProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HICON GetDialogIcon(int resId, const wchar_t* fallbackFile, bool smallIcon) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    int cx = smallIcon ? GetSystemMetrics(SM_CXSMICON) : GetSystemMetrics(SM_CXICON);
    int cy = smallIcon ? GetSystemMetrics(SM_CYSMICON) : GetSystemMetrics(SM_CYICON);
    if (cx < 16) cx = 16;
    if (cy < 16) cy = 16;

    // 1. Tentar carregar do recurso embutido
    HICON hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(resId), IMAGE_ICON, cx, cy, LR_SHARED);
    if (hIcon) return hIcon;

    // 2. Tentar carregar do arquivo em disco
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
        std::wstring full = base + fallbackFile;
        hIcon = (HICON)LoadImageW(nullptr, full.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
        if (hIcon) return hIcon;
    }
    return (HICON)LoadImageW(nullptr, MAKEINTRESOURCEW(IDI_APPLICATION), IMAGE_ICON, cx, cy, LR_SHARED);
}

static HWND CreateModalWindow(HWND hParent, const wchar_t* title, int width, int height, WNDPROC proc, int iconResId = 101, const wchar_t* iconFile = L"opit_balanced.ico") {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(nullptr);
    static bool classRegistered = false;
    static const wchar_t* className = L"OpitCore_AdvDialogClass";

    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = AdvDialogRouterProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = ui::Theme::Instance().brushBgDark;
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // Limitar ao tamanho da área de trabalho do monitor
    RECT rcWork{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxW = rcWork.right - rcWork.left - 40;
    int maxH = rcWork.bottom - rcWork.top - 40;
    if (width > maxW) width = maxW;
    if (height > maxH) height = maxH;

    RECT rcClient = {0, 0, width, height};
    AdjustWindowRectEx(&rcClient, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
    int realWidth = rcClient.right - rcClient.left;
    int realHeight = rcClient.bottom - rcClient.top;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int x = rcParent.left + (rcParent.right - rcParent.left - realWidth) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - realHeight) / 2;
    if (x < rcWork.left + 10) x = rcWork.left + 10;
    if (y < rcWork.top + 10) y = rcWork.top + 10;

    EnableWindow(hParent, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, className, title,
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                x, y, realWidth, realHeight, hParent, nullptr, hInst, (LPVOID)proc);

    if (hwnd) {
        HICON hIconBig = GetDialogIcon(iconResId, iconFile, false);
        HICON hIconSm  = GetDialogIcon(iconResId, iconFile, true);
        if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        if (hIconSm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
    }

    ui::Theme::EnableImmersiveDarkMode(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

static void RunModalLoop(HWND hwnd, HWND hParent) {
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hParent, TRUE);
    SetActiveWindow(hParent);
}

static void CreateStandardButtons(HWND hwnd, int dlgWidth, int dlgHeight) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    int btnW = 95;
    int btnH = 28;
    int btnY = dlgHeight - 44;
    int rightX = dlgWidth - 25;

    HWND btnCancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   rightX - btnW, btnY, btnW, btnH, hwnd, (HMENU)(INT_PTR)ID_ADV_CANCEL, hInst, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
    ui::Theme::ApplyDarkExplorerTheme(btnCancel);

    HWND btnSave = CreateWindowW(L"BUTTON", L"Salvar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 rightX - btnW * 2 - 10, btnY, btnW, btnH, hwnd, (HMENU)(INT_PTR)ID_ADV_SAVE, hInst, nullptr);
    SendMessageW(btnSave, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
    ui::Theme::ApplyDarkExplorerTheme(btnSave);

    HWND btnOk = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               rightX - btnW * 3 - 20, btnY, btnW, btnH, hwnd, (HMENU)(INT_PTR)ID_ADV_OK, hInst, nullptr);
    SendMessageW(btnOk, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
    ui::Theme::ApplyDarkExplorerTheme(btnOk);

    // Aplica o tema dark em todos os controles filhos do diálogo (botões, combos, checkboxes, edits)
    EnumChildWindows(hwnd, [](HWND hChild, LPARAM) -> BOOL {
        ui::Theme::ApplyDarkExplorerTheme(hChild);
        return TRUE;
    }, 0);
}

// =========================================================================
// 1. DIÁLOGO: PLANOS DE ENERGIA
// =========================================================================
static HWND s_comboPwrAc = nullptr;
static HWND s_comboPwrDc = nullptr;
static std::vector<PowerScheme> s_pwrSchemes;
static HWND s_chkPwrAutoGame = nullptr;
static HWND s_chkPwrRestoreGame = nullptr;
static HWND s_chkPwrFallback = nullptr;
static HWND s_editPwrDelay = nullptr;
static HWND s_btnPwrForceEco = nullptr;
static HWND s_lblPwrEcoStatus = nullptr;
static HWND s_chkPwrPauseIdleDevices = nullptr;

static void UpdatePwrEcoStatusUI(HWND hwnd) {
    auto& adv = ModuleAdvancedSettings::Instance();
    if (adv.powerPlans.forceEcoPlan) {
        SetWindowTextW(s_btnPwrForceEco, L"Forcar plano economico: [ATIVADO]");
        SetWindowTextW(s_lblPwrEcoStatus, L"Estado: Ativo e Aplicado no Windows com precedencia absoluta sobre o modo automatico.");
    } else {
        SetWindowTextW(s_btnPwrForceEco, L"Forcar plano economico: [DESATIVADO]");
        SetWindowTextW(s_lblPwrEcoStatus, L"Estado: Desativado (O orquestrador segue os planos configurados e politicas de foco).");
    }
}

static LRESULT CALLBACK PowerPlansWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Politicas de Planos de Energia AC / DC ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 420, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lbl1 = CreateWindowW(L"STATIC", L"Plano de energia preferido na tomada (AC):", WS_CHILD | WS_VISIBLE,
                                  35, 35, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboPwrAc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     35, 55, 560, 150, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboPwrAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl2 = CreateWindowW(L"STATIC", L"Plano de energia preferido em bateria (DC):", WS_CHILD | WS_VISIBLE,
                                  35, 88, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboPwrDc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     35, 108, 560, 150, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboPwrDc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        PowerManager pm;
        s_pwrSchemes = pm.ListSchemes();
        int selAc = 0, selDc = 0;
        for (size_t i = 0; i < s_pwrSchemes.size(); ++i) {
            std::wstring wName = StringUtils::ToWide(s_pwrSchemes[i].name);
            SendMessageW(s_comboPwrAc, CB_ADDSTRING, 0, (LPARAM)wName.c_str());
            SendMessageW(s_comboPwrDc, CB_ADDSTRING, 0, (LPARAM)wName.c_str());
            if (s_pwrSchemes[i].guid == adv.powerPlans.preferredSchemeAC || s_pwrSchemes[i].name == adv.powerPlans.preferredSchemeAC) {
                selAc = (int)i;
            }
            if (s_pwrSchemes[i].guid == adv.powerPlans.preferredSchemeDC || s_pwrSchemes[i].name == adv.powerPlans.preferredSchemeDC) {
                selDc = (int)i;
            }
        }
        SendMessageW(s_comboPwrAc, CB_SETCURSEL, selAc, 0);
        SendMessageW(s_comboPwrDc, CB_SETCURSEL, selDc, 0);

        s_chkPwrAutoGame = CreateWindowW(L"BUTTON", L"Alternar automaticamente para Alto Desempenho ao iniciar jogo",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 142, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkPwrAutoGame, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkPwrAutoGame, BM_SETCHECK, adv.powerPlans.autoSwitchOnGame ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkPwrRestoreGame = CreateWindowW(L"BUTTON", L"Restaurar plano anterior ao encerrar jogo",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 168, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkPwrRestoreGame, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkPwrRestoreGame, BM_SETCHECK, adv.powerPlans.restoreOnGameExit ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkPwrFallback = CreateWindowW(L"BUTTON", L"Fallback seguro: usar plano Equilibrado se o plano configurado falhar",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 194, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkPwrFallback, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkPwrFallback, BM_SETCHECK, adv.powerPlans.fallbackToBalanced ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lbl3 = CreateWindowW(L"STATIC", L"Tempo de confirmacao / estabilizacao (ms):", WS_CHILD | WS_VISIBLE,
                                  35, 224, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_editPwrDelay = CreateWindowW(L"EDIT", std::to_wstring(adv.powerPlans.confirmationDelayMs).c_str(),
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 221, 90, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editPwrDelay, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Seção Novo Controle: Forçar Plano Econômico e Pausa de Dispositivos Ociosos
        HWND grpEco = CreateWindowW(L"BUTTON", L" Imposicao e Otimizacoes de Economia de Energia ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    35, 255, 560, 165, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpEco, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_btnPwrForceEco = CreateWindowW(L"BUTTON", L"Forcar plano economico: [DESATIVADO]",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 50, 278, 290, 28, hwnd, (HMENU)(INT_PTR)ID_PWR_BTN_FORCE_ECO, hInst, nullptr);
        SendMessageW(s_btnPwrForceEco, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_lblPwrEcoStatus = CreateWindowW(L"STATIC", L"Estado: Desativado", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          50, 310, 530, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblPwrEcoStatus, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_chkPwrPauseIdleDevices = CreateWindowW(L"BUTTON", L"Pausar dispositivos e barramentos ociosos em modo Economico (USB / PCIe ASPM)",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 50, 332, 530, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkPwrPauseIdleDevices, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        SendMessageW(s_chkPwrPauseIdleDevices, BM_SETCHECK, adv.powerPlans.pauseIdleDevicesEco ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblEcoDesc = CreateWindowW(L"STATIC",
            L"Ao marcar a opcao acima, portas USB inativas e links PCIe entram em suspensao no modo Economico para diminuir o consumo ao maximo. Se desmarcado, os dispositivos permanecem sempre ativos sem risco de corte.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 50, 358, 530, 54, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblEcoDesc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        UpdatePwrEcoStatusUI(hwnd);
        CreateStandardButtons(hwnd, 640, 490);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_PWR_BTN_FORCE_ECO) {
            PowerManager pm;
            bool newTarget = !adv.powerPlans.forceEcoPlan;
            std::string appliedName, statusMsg;
            if (pm.ForceEcoPlan(newTarget, appliedName, statusMsg)) {
                adv.powerPlans.forceEcoPlan = newTarget;
                adv.Save();
                CoreRuntime::Instance().ApplyUnifiedSettings();
                UpdatePwrEcoStatusUI(hwnd);
            } else {
                MessageBoxW(hwnd, StringUtils::ToWide(statusMsg).c_str(), L"Aviso de Planos de Energia", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            int selAc = (int)SendMessageW(s_comboPwrAc, CB_GETCURSEL, 0, 0);
            if (selAc >= 0 && selAc < (int)s_pwrSchemes.size()) {
                adv.powerPlans.preferredSchemeAC = s_pwrSchemes[selAc].guid;
            }
            int selDc = (int)SendMessageW(s_comboPwrDc, CB_GETCURSEL, 0, 0);
            if (selDc >= 0 && selDc < (int)s_pwrSchemes.size()) {
                adv.powerPlans.preferredSchemeDC = s_pwrSchemes[selDc].guid;
            }

            adv.powerPlans.autoSwitchOnGame = (SendMessageW(s_chkPwrAutoGame, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.powerPlans.restoreOnGameExit = (SendMessageW(s_chkPwrRestoreGame, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.powerPlans.fallbackToBalanced = (SendMessageW(s_chkPwrFallback, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t buf[32]{};
            GetWindowTextW(s_editPwrDelay, buf, 32);
            adv.powerPlans.confirmationDelayMs = _wtoi(buf);

            adv.powerPlans.pauseIdleDevicesEco = (SendMessageW(s_chkPwrPauseIdleDevices, BM_GETCHECK, 0, 0) == BST_CHECKED);
            UnifiedSettingsModel::Instance().pauseIdleDevicesEco = adv.powerPlans.pauseIdleDevicesEco;
            UnifiedSettingsModel::Instance().Save();

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Configuracoes de Planos de Energia salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 2. DIÁLOGO: CPU MIN/MAX (CONFIGURAÇÃO POR PLANO REAL DO WINDOWS)
// =========================================================================
static HWND s_comboCpuPlan = nullptr;
static HWND s_editCpuMinAc = nullptr;
static HWND s_editCpuMaxAc = nullptr;
static HWND s_editCpuMinDc = nullptr;
static HWND s_editCpuMaxDc = nullptr;
static HWND s_chkCpuTurbo = nullptr;
static HWND s_editCpuInterval = nullptr;
static HWND s_lblCpuVerifiedStatus = nullptr;
static HWND s_comboCpuClass = nullptr;
static int s_classComboPrev = 0;
static std::vector<PowerScheme> s_cpuSchemes;

static void LoadValuesForSelectedScheme(HWND hwnd) {
    int sel = (int)SendMessageW(s_comboCpuPlan, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_cpuSchemes.size()) return;

    auto& adv = ModuleAdvancedSettings::Instance();
    const auto& sch = s_cpuSchemes[sel];
    PlanThrottleSettings t = adv.cpuMinMax.GetPlanThrottle(sch.guid.empty() ? sch.name : sch.guid);

    SetWindowTextW(s_editCpuMinAc, std::to_wstring(t.minPercentAC).c_str());
    SetWindowTextW(s_editCpuMaxAc, std::to_wstring(t.maxPercentAC).c_str());
    SetWindowTextW(s_editCpuMinDc, std::to_wstring(t.minPercentDC).c_str());
    SetWindowTextW(s_editCpuMaxDc, std::to_wstring(t.maxPercentDC).c_str());

    std::wstring planType = L"Equilibrado";
    std::string s = StringUtils::ToLower(sch.name + " " + sch.guid);
    if (s.find("a1841308-3541-4fab-bc81-f71556f20b4a") != std::string::npos ||
        s.find("eco") != std::string::npos || s.find("economia") != std::string::npos || s.find("saver") != std::string::npos) {
        planType = L"Economia";
    } else if (s.find("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") != std::string::npos ||
               s.find("e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos ||
               s.find("alto") != std::string::npos || s.find("high") != std::string::npos || s.find("ultimate") != std::string::npos) {
        planType = L"Alta Performance";
    }

    std::wstring status = L"Plano [" + planType + L"]: " + StringUtils::ToWide(sch.name) +
                          (sch.isActive ? L" (Ativo agora no Windows)" : L"");

    // Segunda linha: a classe de hardware que escolheu o perfil e as faixas de uso de CPU que o
    // orquestrador vai obedecer. Sao os proprios limites salvos acima, nao uma constante do codigo,
    // entao o que esta escrito aqui e o que o motor faz quando o uso muda de faixa.
    const bool manualClass = adv.hwClassOverride != "auto";
    const bool laptop = adv.EffectiveHasBattery();
    const auto faixa = [&adv](bool dc) {
        const int ecoTop = static_cast<int>(adv.cpuMinMax.balancedPlan.Min(dc)) - 1;
        const int balBase = static_cast<int>(adv.cpuMinMax.balancedPlan.Min(dc));
        const int balTop = static_cast<int>(adv.cpuMinMax.highPerfPlan.Min(dc)) - 1;
        const int highBase = static_cast<int>(adv.cpuMinMax.highPerfPlan.Min(dc));
        return L"0-" + std::to_wstring(ecoTop) + L"% / " + std::to_wstring(balBase) + L"-" + std::to_wstring(balTop) +
               L"% / " + std::to_wstring(highBase) + L"-100%";
    };
    // CRLF e nao LF: o static do Windows so quebra linha com o par completo, e com LF sozinho o texto
    // inteiro da segunda linha se perdia (a janela mostrava uma linha e meia sem nenhuma dica disso).
    status += L"\r\nPerfil do hardware: " + std::wstring(laptop ? L"Notebook" : L"Desktop / Servidor");
    if (manualClass) {
        // Transparencia: classe forçada pela usuaria nao pode se passar pela deteccao da maquina.
        status += L" (definido manualmente; detectado: " +
                  std::wstring(SystemHasBattery() ? L"Notebook" : L"Desktop / Servidor") + L")";
    }
    status += L"  ·  faixas que o orquestrador obedece: " + faixa(false);
    if (laptop) {
        status += L"  ·  na bateria: " + faixa(true);
    }

    SetWindowTextW(s_lblCpuVerifiedStatus, status.c_str());
}

static LRESULT CALLBACK CpuMinMaxWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Configuracao de Limites de CPU por Plano de Energia ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 460, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblPlan = CreateWindowW(L"STATIC", L"Selecionar Plano de Energia do Windows para configurar:", WS_CHILD | WS_VISIBLE, 35, 32, 400, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblPlan, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_comboCpuPlan = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       35, 52, 560, 150, hwnd, (HMENU)(INT_PTR)ID_CPU_COMBO_PLAN, hInst, nullptr);
        SendMessageW(s_comboCpuPlan, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        PowerManager pm;
        s_cpuSchemes = pm.ListSchemes();
        int activeIdx = 0;
        for (size_t i = 0; i < s_cpuSchemes.size(); ++i) {
            std::wstring wName = StringUtils::ToWide(s_cpuSchemes[i].name);
            if (s_cpuSchemes[i].isActive) {
                wName += L" [ATIVO NO WINDOWS]";
                activeIdx = (int)i;
            }
            SendMessageW(s_comboCpuPlan, CB_ADDSTRING, 0, (LPARAM)wName.c_str());
        }
        SendMessageW(s_comboCpuPlan, CB_SETCURSEL, activeIdx, 0);

        // Grupo Tomada (AC)
        HWND grpAc = CreateWindowW(L"BUTTON", L" Alimentacao pela Tomada (AC) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   35, 88, 270, 100, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpAc, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblMinAc = CreateWindowW(L"STATIC", L"Minimo (%):", WS_CHILD | WS_VISIBLE, 50, 115, 90, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMinAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpuMinAc = CreateWindowW(L"EDIT", std::to_wstring(adv.cpuMinMax.minPercentAC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 150, 112, 60, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpuMinAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblMaxAc = CreateWindowW(L"STATIC", L"Maximo (%):", WS_CHILD | WS_VISIBLE, 50, 148, 90, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMaxAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpuMaxAc = CreateWindowW(L"EDIT", std::to_wstring(adv.cpuMinMax.maxPercentAC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 150, 145, 60, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpuMaxAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Grupo Bateria (DC)
        HWND grpDc = CreateWindowW(L"BUTTON", L" Alimentacao pela Bateria (DC) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   325, 88, 270, 100, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpDc, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblMinDc = CreateWindowW(L"STATIC", L"Minimo (%):", WS_CHILD | WS_VISIBLE, 340, 115, 90, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMinDc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpuMinDc = CreateWindowW(L"EDIT", std::to_wstring(adv.cpuMinMax.minPercentDC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 440, 112, 60, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpuMinDc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblMaxDc = CreateWindowW(L"STATIC", L"Maximo (%):", WS_CHILD | WS_VISIBLE, 340, 148, 90, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMaxDc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpuMaxDc = CreateWindowW(L"EDIT", std::to_wstring(adv.cpuMinMax.maxPercentDC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 440, 145, 60, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpuMaxDc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnApplyPlan = CreateWindowW(L"BUTTON", L"Aplicar Limites ao Plano Selecionado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          35, 198, 270, 28, hwnd, (HMENU)(INT_PTR)ID_CPU_BTN_APPLY_PLAN, hInst, nullptr);
        SendMessageW(btnApplyPlan, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND btnReload = CreateWindowW(L"BUTTON", L"Reler Valores do Windows", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       325, 198, 270, 28, hwnd, (HMENU)(INT_PTR)ID_CPU_BTN_RELOAD, hInst, nullptr);
        SendMessageW(btnReload, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Override manual da classe de hardware: maquinas que a heuristica (bateria) errou podem
        // forcar a tabela de bandas certa. "Detectar automaticamente" devolve a deteccao; escolher
        // uma classe re-aplica o perfil-padrao dela depois de confirmacao, porque substitui os doze
        // limites salvos.
        HWND lblClass = CreateWindowW(L"STATIC", L"Classe do equipamento:", WS_CHILD | WS_VISIBLE, 35, 237, 165, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblClass, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboCpuClass = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        205, 232, 250, 120, hwnd, (HMENU)(INT_PTR)ID_CPU_COMBO_CLASS, hInst, nullptr);
        SendMessageW(s_comboCpuClass, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboCpuClass, CB_ADDSTRING, 0, (LPARAM)L"Detectar automaticamente");
        SendMessageW(s_comboCpuClass, CB_ADDSTRING, 0, (LPARAM)L"Desktop / Servidor (forcar)");
        SendMessageW(s_comboCpuClass, CB_ADDSTRING, 0, (LPARAM)L"Notebook (forcar)");
        s_classComboPrev = (adv.hwClassOverride == "desktop") ? 1 : (adv.hwClassOverride == "notebook") ? 2 : 0;
        SendMessageW(s_comboCpuClass, CB_SETCURSEL, s_classComboPrev, 0);

        s_lblCpuVerifiedStatus = CreateWindowW(L"STATIC", L"Configuracoes salvas pelo usuario mantidas separadamente por plano.", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                              35, 270, 560, 56, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblCpuVerifiedStatus, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_chkCpuTurbo = CreateWindowW(L"BUTTON", L"Permitir Turbo Boost do processador em alta demanda",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 334, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkCpuTurbo, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkCpuTurbo, BM_SETCHECK, adv.cpuMinMax.allowTurboBoost ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lbl5 = CreateWindowW(L"STATIC", L"Intervalo de reavaliacao da carga de CPU (ms):", WS_CHILD | WS_VISIBLE, 35, 366, 310, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl5, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpuInterval = CreateWindowW(L"EDIT", std::to_wstring(adv.cpuMinMax.reevaluationIntervalMs).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 362, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpuInterval, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblNote = CreateWindowW(L"STATIC",
            L"Transparencia Tecnica:\r\nOs percentuais configuram as diretivas PROCTHROTTLEMIN e PROCTHROTTLEMAX separadamente para Economia, Balanceado e Alta Performance no Windows.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 35, 396, 560, 45, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblNote, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        LoadValuesForSelectedScheme(hwnd);
        CreateStandardButtons(hwnd, 640, 520);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_CPU_COMBO_CLASS && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(s_comboCpuClass, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel > 2 || sel == s_classComboPrev) return 0;
            static const char* kClassValues[3] = {"auto", "desktop", "notebook"};
            const std::string chosen = kClassValues[sel];
            const bool chosenHasBattery = (chosen == "notebook") || (chosen == "auto" && SystemHasBattery());
            const std::string chosenClass = chosenHasBattery ? "notebook" : "desktop";
            // "Detectar automaticamente" sem mudanca de classe so grava a preferencia: re-semear
            // substituiria os limites ajustados pela usuaria sem motivo. Classe forcada e sempre
            // re-aplicada, porque foi um pedido explicito do perfil-padrao dela.
            const bool reaplica = (chosen == "desktop" || chosen == "notebook") ||
                                  (chosenClass != adv.EffectiveHwClassName());
            if (reaplica) {
                std::wstring clsName = chosenHasBattery ? L"Notebook" : L"Desktop / Servidor";
                std::wstring msg = L"Aplicar os valores-padrao da classe " + clsName +
                                   L"? Os limites Min/Max salvos dos tres planos serao substituidos:\r\n" +
                                   (chosenHasBattery
                                      ? L"tomada 0-25 / 26-50 / 51-75  ·  bateria 0-20 / 21-35 / 36-60"
                                      : L"0-35 / 36-78 / 79-100");
                if (MessageBoxW(hwnd, msg.c_str(), L"Classe do equipamento", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    SendMessageW(s_comboCpuClass, CB_SETCURSEL, s_classComboPrev, 0);
                    return 0;
                }
                adv.cpuMinMax.ApplyStandardProfile(chosenHasBattery);
            }
            adv.hwClassOverride = chosen;
            adv.Save();
            s_classComboPrev = sel;
            LoadValuesForSelectedScheme(hwnd);
            return 0;
        }

        if (id == ID_CPU_COMBO_PLAN && code == CBN_SELCHANGE) {
            LoadValuesForSelectedScheme(hwnd);
            return 0;
        }

        if (id == ID_CPU_BTN_RELOAD) {
            LoadValuesForSelectedScheme(hwnd);
            return 0;
        }

        if (id == ID_CPU_BTN_APPLY_PLAN) {
            int sel = (int)SendMessageW(s_comboCpuPlan, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)s_cpuSchemes.size()) return 0;

            wchar_t b1[32]{}, b2[32]{}, b3[32]{}, b4[32]{};
            GetWindowTextW(s_editCpuMinAc, b1, 32);
            GetWindowTextW(s_editCpuMaxAc, b2, 32);
            GetWindowTextW(s_editCpuMinDc, b3, 32);
            GetWindowTextW(s_editCpuMaxDc, b4, 32);

            uint32_t minAc = _wtoi(b1), maxAc = _wtoi(b2);
            uint32_t minDc = _wtoi(b3), maxDc = _wtoi(b4);

            if (minAc > maxAc) {
                MessageBoxW(hwnd, L"O valor Minimo de CPU em AC nao pode ser maior que o Maximo!", L"Validacao de CPU", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (minDc > maxDc) {
                MessageBoxW(hwnd, L"O valor Minimo de CPU em DC nao pode ser maior que o Maximo!", L"Validacao de CPU", MB_OK | MB_ICONWARNING);
                return 0;
            }

            const auto& sch = s_cpuSchemes[sel];
            PlanThrottleSettings throttle{minAc, maxAc, minDc, maxDc};
            adv.cpuMinMax.SetPlanThrottle(sch.guid.empty() ? sch.name : sch.guid, throttle);

            PowerManager pm;
            pm.WriteSchemeProcessorThrottle(sch.guid, minAc, maxAc, minDc, maxDc);

            if (sch.isActive) {
                adv.cpuMinMax.minPercentAC = minAc;
                adv.cpuMinMax.maxPercentAC = maxAc;
                adv.cpuMinMax.minPercentDC = minDc;
                adv.cpuMinMax.maxPercentDC = maxDc;
                pm.SetProcessorThrottleRange(minAc, maxAc);
                Config::Instance().SetProcessorThrottleCap(maxAc);
            }
            adv.Save();

            std::wstring okMsg = L"Limites aplicados exclusivamente ao plano [" + StringUtils::ToWide(sch.name) + L"] (" +
                                 std::to_wstring(minAc) + L"% - " + std::to_wstring(maxAc) + L"%)!";
            SetWindowTextW(s_lblCpuVerifiedStatus, okMsg.c_str());
            MessageBoxW(hwnd, okMsg.c_str(), L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            int sel = (int)SendMessageW(s_comboCpuPlan, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)s_cpuSchemes.size()) {
                wchar_t b1[32]{}, b2[32]{}, b3[32]{}, b4[32]{};
                GetWindowTextW(s_editCpuMinAc, b1, 32);
                GetWindowTextW(s_editCpuMaxAc, b2, 32);
                GetWindowTextW(s_editCpuMinDc, b3, 32);
                GetWindowTextW(s_editCpuMaxDc, b4, 32);

                uint32_t minAc = _wtoi(b1), maxAc = _wtoi(b2);
                uint32_t minDc = _wtoi(b3), maxDc = _wtoi(b4);
                if (minAc <= maxAc && minDc <= maxDc) {
                    const auto& sch = s_cpuSchemes[sel];
                    adv.cpuMinMax.SetPlanThrottle(sch.guid.empty() ? sch.name : sch.guid, {minAc, maxAc, minDc, maxDc});
                    PowerManager pm;
                    pm.WriteSchemeProcessorThrottle(sch.guid, minAc, maxAc, minDc, maxDc);
                }
                adv.cpuMinMax.targetSchemeGuid = s_cpuSchemes[sel].guid;
            }

            wchar_t b5[32]{};
            GetWindowTextW(s_editCpuInterval, b5, 32);
            adv.cpuMinMax.allowTurboBoost = (SendMessageW(s_chkCpuTurbo, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.cpuMinMax.reevaluationIntervalMs = _wtoi(b5);

            PowerManager pm;
            std::string ecoGuid = pm.FindEcoScheme();
            std::string balGuid = pm.FindBalancedScheme();
            std::string highGuid = pm.FindHighPerformanceScheme();
            if (!ecoGuid.empty()) {
                pm.WriteSchemeProcessorThrottle(ecoGuid,
                    adv.cpuMinMax.ecoPlan.minPercentAC, adv.cpuMinMax.ecoPlan.maxPercentAC,
                    adv.cpuMinMax.ecoPlan.minPercentDC, adv.cpuMinMax.ecoPlan.maxPercentDC);
            }
            if (!balGuid.empty()) {
                pm.WriteSchemeProcessorThrottle(balGuid,
                    adv.cpuMinMax.balancedPlan.minPercentAC, adv.cpuMinMax.balancedPlan.maxPercentAC,
                    adv.cpuMinMax.balancedPlan.minPercentDC, adv.cpuMinMax.balancedPlan.maxPercentDC);
            }
            if (!highGuid.empty()) {
                pm.WriteSchemeProcessorThrottle(highGuid,
                    adv.cpuMinMax.highPerfPlan.minPercentAC, adv.cpuMinMax.highPerfPlan.maxPercentAC,
                    adv.cpuMinMax.highPerfPlan.minPercentDC, adv.cpuMinMax.highPerfPlan.maxPercentDC);
            }

            PowerScheme activeSch = pm.GetActiveScheme();
            PlanThrottleSettings curThrottle = adv.cpuMinMax.GetPlanThrottle(activeSch.guid.empty() ? activeSch.name : activeSch.guid);
            adv.cpuMinMax.minPercentAC = curThrottle.minPercentAC;
            adv.cpuMinMax.maxPercentAC = curThrottle.maxPercentAC;
            adv.cpuMinMax.minPercentDC = curThrottle.minPercentDC;
            adv.cpuMinMax.maxPercentDC = curThrottle.maxPercentDC;
            pm.SetProcessorThrottleRange(curThrottle.minPercentAC, curThrottle.maxPercentAC);
            Config::Instance().SetProcessorThrottleCap(curThrottle.maxPercentAC);

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Limites de CPU salvos separadamente por plano de energia com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 3. DIÁLOGO: CORE PARKING
// =========================================================================
static HWND s_comboCpAc = nullptr;
static HWND s_comboCpDc = nullptr;
static HWND s_chkCpUnparkGame = nullptr;
static HWND s_editCpHysteresis = nullptr;
static HWND s_btnCpMaxParking = nullptr;
static HWND s_lblCpStatus = nullptr;

static void UpdateCpStatusUI(HWND hwnd) {
    auto& adv = ModuleAdvancedSettings::Instance();
    if (adv.coreParking.forceMaxParking) {
        SetWindowTextW(s_btnCpMaxParking, L"Maximo de nucleos pausados: [ATIVADO]");
        SetWindowTextW(s_lblCpStatus, L"Estado: Ativo (5% nucleos minimos desestacionados - permite ao escalonador do Windows pausar o maximo de nucleos sob ociosidade).");
    } else {
        SetWindowTextW(s_btnCpMaxParking, L"Maximo de nucleos pausados: [DESATIVADO]");
        SetWindowTextW(s_lblCpStatus, L"Estado: Desativado (politica padrao baseada no perfil e demanda de tarefas).");
    }
}

static LRESULT CALLBACK CoreParkingWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Politica de Estacionamento de Nucleos (Core Parking) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 420, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        DWORD logicalCores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        std::wstring wTop = L"Topologia detectada: " + std::to_wstring(logicalCores) + L" processadores logicos no agendador.";
        HWND lblTop = CreateWindowW(L"STATIC", wTop.c_str(), WS_CHILD | WS_VISIBLE, 35, 32, 540, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblTop, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lbl1 = CreateWindowW(L"STATIC", L"Politica de disponibilidade na tomada (AC):", WS_CHILD | WS_VISIBLE, 35, 58, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboCpAc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 35, 78, 560, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboCpAc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboCpAc, CB_ADDSTRING, 0, (LPARAM)L"Economia Maxima (Pausar ate 90% dos nucleos ociosos)");
        SendMessageW(s_comboCpAc, CB_ADDSTRING, 0, (LPARAM)L"Equilibrado (50% dos nucleos ativos sob demanda)");
        SendMessageW(s_comboCpAc, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (100% dos nucleos sempre ativos - sem pausa)");

        if (adv.coreParking.minCoresActiveAC <= 25) SendMessageW(s_comboCpAc, CB_SETCURSEL, 0, 0);
        else if (adv.coreParking.minCoresActiveAC <= 60) SendMessageW(s_comboCpAc, CB_SETCURSEL, 1, 0);
        else SendMessageW(s_comboCpAc, CB_SETCURSEL, 2, 0);

        s_chkCpUnparkGame = CreateWindowW(L"BUTTON", L"Forcar desestacionamento imediato de todos os nucleos ao detectar jogo",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 115, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkCpUnparkGame, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkCpUnparkGame, BM_SETCHECK, adv.coreParking.forceUnparkOnGame ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lbl3 = CreateWindowW(L"STATIC", L"Histerese para alternancia de nucleos (ms):", WS_CHILD | WS_VISIBLE, 35, 145, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editCpHysteresis = CreateWindowW(L"EDIT", std::to_wstring(adv.coreParking.hysteresisMs).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 360, 142, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editCpHysteresis, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Seção Novo Botão: Máximo de núcleos pausados
        HWND grpMax = CreateWindowW(L"BUTTON", L" Imposicao de Maximo Estacionamento ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    35, 180, 560, 145, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpMax, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_btnCpMaxParking = CreateWindowW(L"BUTTON", L"Maximo de nucleos pausados: [DESATIVADO]",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 50, 205, 320, 32, hwnd, (HMENU)(INT_PTR)ID_CP_BTN_MAX_PARKING, hInst, nullptr);
        SendMessageW(s_btnCpMaxParking, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_lblCpStatus = CreateWindowW(L"STATIC", L"Estado: Desativado", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      50, 245, 530, 32, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblCpStatus, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblClarification = CreateWindowW(L"STATIC",
            L"Transparencia Tecnica: O estacionamento de nucleos (Core Parking) e uma diretiva logica de agendamento em C-State do kernel do Windows. Ele NAO desliga fisicamente os nucleos nem reduz a vida util da CPU.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 50, 280, 530, 38, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblClarification, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        UpdateCpStatusUI(hwnd);
        CreateStandardButtons(hwnd, 640, 490);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_CP_BTN_MAX_PARKING) {
            PowerManager pm;
            bool newTarget = !adv.coreParking.forceMaxParking;
            uint32_t appliedVal = 0;
            if (pm.ForceMaxCoreParking(newTarget, appliedVal)) {
                adv.coreParking.forceMaxParking = newTarget;
                adv.Save();
                CoreRuntime::Instance().ApplyUnifiedSettings();
                UpdateCpStatusUI(hwnd);
            }
            return 0;
        }

        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            int selAc = (int)SendMessageW(s_comboCpAc, CB_GETCURSEL, 0, 0);
            if (selAc == 0) adv.coreParking.minCoresActiveAC = 10;
            else if (selAc == 1) adv.coreParking.minCoresActiveAC = 50;
            else adv.coreParking.minCoresActiveAC = 100;

            adv.coreParking.forceUnparkOnGame = (SendMessageW(s_chkCpUnparkGame, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t buf[32]{};
            GetWindowTextW(s_editCpHysteresis, buf, 32);
            adv.coreParking.hysteresisMs = _wtoi(buf);

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Politicas de Core Parking salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 4. DIÁLOGO: PROCESSOS (CONFIGURAÇÃO POR ESTADO DE JANELA E MODO AUTO)
// =========================================================================
static HWND s_chkProcAuto = nullptr;
static HWND s_comboProcMax = nullptr;
static HWND s_comboProcNorm = nullptr;
static HWND s_comboProcMin = nullptr;
static HWND s_comboProcBg = nullptr;
static HWND s_comboProcTray = nullptr;
static HWND s_comboProcNoWin = nullptr;
static HWND s_chkProcPresMedia = nullptr;
static HWND s_chkProcProtectCrit = nullptr;
static HWND s_editProcExcluded = nullptr;

static void FillPriorityCombo(HWND hCombo, uint32_t currentPrio) {
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Alta Prioridade");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Acima do Normal");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Normal");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Abaixo do Normal");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Ociosa (Baixa)");

    if (currentPrio == HIGH_PRIORITY_CLASS) SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
    else if (currentPrio == ABOVE_NORMAL_PRIORITY_CLASS) SendMessageW(hCombo, CB_SETCURSEL, 1, 0);
    else if (currentPrio == NORMAL_PRIORITY_CLASS) SendMessageW(hCombo, CB_SETCURSEL, 2, 0);
    else if (currentPrio == BELOW_NORMAL_PRIORITY_CLASS) SendMessageW(hCombo, CB_SETCURSEL, 3, 0);
    else SendMessageW(hCombo, CB_SETCURSEL, 4, 0);
}

static uint32_t GetPriorityFromCombo(HWND hCombo) {
    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    switch (sel) {
        case 0: return HIGH_PRIORITY_CLASS;
        case 1: return ABOVE_NORMAL_PRIORITY_CLASS;
        case 2: return NORMAL_PRIORITY_CLASS;
        case 3: return BELOW_NORMAL_PRIORITY_CLASS;
        case 4: return IDLE_PRIORITY_CLASS;
        default: return NORMAL_PRIORITY_CLASS;
    }
}

static LRESULT CALLBACK ProcessesWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Politicas de Prioridade por Estado de Execucao da Aplicacao ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 420, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_chkProcAuto = CreateWindowW(L"BUTTON", L"Modo AUTO de Processos: Detectar dinamicamente o estado das janelas e tarefas ativas",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 32, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkProcAuto, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        SendMessageW(s_chkProcAuto, BM_SETCHECK, adv.processes.autoProcessManagement ? BST_CHECKED : BST_UNCHECKED, 0);

        // Estados 1 e 2
        HWND lbl1 = CreateWindowW(L"STATIC", L"1. Aplicacao maximizada:", WS_CHILD | WS_VISIBLE, 35, 62, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcMax = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 210, 59, 130, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcMax, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcMax, adv.processes.prioMaximized);

        HWND lbl2 = CreateWindowW(L"STATIC", L"2. Janela normal (sem max):", WS_CHILD | WS_VISIBLE, 355, 62, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcNorm = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 510, 59, 90, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcNorm, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcNorm, adv.processes.prioNormal);

        // Estados 3 e 4
        HWND lbl3 = CreateWindowW(L"STATIC", L"3. Aplicacao minimizada:", WS_CHILD | WS_VISIBLE, 35, 95, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcMin = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 210, 92, 130, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcMin, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcMin, adv.processes.prioMinimized);

        HWND lbl4 = CreateWindowW(L"STATIC", L"4. Segundo plano:", WS_CHILD | WS_VISIBLE, 355, 95, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl4, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcBg = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 510, 92, 90, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcBg, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcBg, adv.processes.prioBackground);

        // Estados 5 e 6
        HWND lbl5 = CreateWindowW(L"STATIC", L"5. Area de notificacao (Tray):", WS_CHILD | WS_VISIBLE, 35, 128, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl5, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcTray = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 210, 125, 130, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcTray, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcTray, adv.processes.prioSystemTray);

        HWND lbl6 = CreateWindowW(L"STATIC", L"6. Sem janela visivel:", WS_CHILD | WS_VISIBLE, 355, 128, 170, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl6, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboProcNoWin = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 510, 125, 90, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboProcNoWin, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        FillPriorityCombo(s_comboProcNoWin, adv.processes.prioNoVisibleWindow);

        s_chkProcPresMedia = CreateWindowW(L"BUTTON", L"Preservar tarefas ativas: Nao reduzir apps de audio, streaming e comunicacao (Spotify, OBS, Discord)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 160, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkProcPresMedia, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkProcPresMedia, BM_SETCHECK, adv.processes.preserveAudioVideoTasks ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkProcProtectCrit = CreateWindowW(L"BUTTON", L"Proteger incondicionalmente processos criticos do sistema operacional (Windows)",
                                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 186, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkProcProtectCrit, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkProcProtectCrit, BM_SETCHECK, adv.processes.protectCriticalProcesses ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblExcl = CreateWindowW(L"STATIC", L"Lista de processos excluidos (separados por ponto-e-virgula ';'):", WS_CHILD | WS_VISIBLE, 35, 215, 560, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblExcl, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        std::wstring wExcluded = StringUtils::ToWide(adv.processes.excludedProcesses);
        s_editProcExcluded = CreateWindowW(L"EDIT", wExcluded.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 35, 235, 560, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editProcExcluded, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblDesc = CreateWindowW(L"STATIC",
            L"Metodologia do Modo Auto:\r\nO Opit-Core monitora os estados de janela sem adivinhar contexto. Tarefas de fundo vitais mantem prioridade normal, enquanto processos minimizados inativos cedem CPU para a aplicacao principal.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 35, 270, 560, 48, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblDesc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        CreateStandardButtons(hwnd, 640, 490);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            adv.processes.autoProcessManagement = (SendMessageW(s_chkProcAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.processes.prioMaximized = GetPriorityFromCombo(s_comboProcMax);
            adv.processes.prioNormal = GetPriorityFromCombo(s_comboProcNorm);
            adv.processes.prioMinimized = GetPriorityFromCombo(s_comboProcMin);
            adv.processes.prioBackground = GetPriorityFromCombo(s_comboProcBg);
            adv.processes.prioSystemTray = GetPriorityFromCombo(s_comboProcTray);
            adv.processes.prioNoVisibleWindow = GetPriorityFromCombo(s_comboProcNoWin);
            adv.processes.preserveAudioVideoTasks = (SendMessageW(s_chkProcPresMedia, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.processes.protectCriticalProcesses = (SendMessageW(s_chkProcProtectCrit, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t buf[512]{};
            GetWindowTextW(s_editProcExcluded, buf, 512);
            adv.processes.excludedProcesses = StringUtils::ToUtf8(std::wstring(buf));

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Configuracoes de Processos salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 5. DIALOGO: GPU (MULTI-ADAPTADOR, TELEMETRIA E CONTROLES AVANCADOS)
// =========================================================================
static HWND s_comboGpuList = nullptr;
static HWND s_lblGpuDetails = nullptr;
static HWND s_lblGpuVendorStatus = nullptr;
static HWND s_comboGpuProfile = nullptr;
static HWND s_editGpuCore = nullptr;
static HWND s_lblGpuCoreRange = nullptr;
static HWND s_editGpuMem = nullptr;
static HWND s_lblGpuMemRange = nullptr;
static HWND s_editGpuPower = nullptr;
static HWND s_lblGpuPowerRange = nullptr;
static HWND s_lblGpuRealStatus = nullptr;
static HWND s_comboGpuPref = nullptr;
static HWND s_chkGpuLatency = nullptr;
static std::vector<GpuDetails> s_detectedGpus;

static void UpdateGpuDetailsUI(HWND hwnd) {
    int sel = (int)SendMessageW(s_comboGpuList, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_detectedGpus.size()) return;

    const auto& g = s_detectedGpus[sel];
    auto caps = GpuControlManager::Instance().GetCapabilities((size_t)sel);
    auto status = GpuControlManager::Instance().GetStatus((size_t)sel);

    std::wstring vendorStr = (g.vendorId == 0x10DE) ? L"NVIDIA" :
                             (g.vendorId == 0x1002) ? L"AMD" :
                             (g.vendorId == 0x8086) ? L"Intel" : L"Microsoft/Generico";

    std::wstring info = L"Fabricante: " + vendorStr + L" | Dispositivo: " + StringUtils::ToWide(g.description) + L"\r\n" +
                        L"VRAM Dedicada: " + std::to_wstring(g.dedicatedVideoMemoryMB) + L" MB | " +
                        L"Memoria Compartilhada: " + std::to_wstring(g.sharedSystemMemoryMB) + L" MB | " +
                        L"Tipo: " + std::wstring(g.isDedicated ? L"Dedicada" : L"Integrada");
    SetWindowTextW(s_lblGpuDetails, info.c_str());

    std::wstring statusStr = L"Suporte Oficial de Hardware: " +
                             std::wstring(caps.supported ? L"[ATIVO]" : L"[TELEMETRIA / PROTEGIDO]") +
                             L" - " + StringUtils::ToWide(caps.limitationReason);
    SetWindowTextW(s_lblGpuVendorStatus, statusStr.c_str());

    // Atualiza ranges dos controles
    if (s_lblGpuCoreRange) {
        std::wstring r = L"[" + std::to_wstring(caps.minCoreClockOffsetMHz) + L" a +" + std::to_wstring(caps.maxCoreClockOffsetMHz) + L" MHz]";
        SetWindowTextW(s_lblGpuCoreRange, r.c_str());
    }
    if (s_lblGpuMemRange) {
        std::wstring r = L"[" + std::to_wstring(caps.minMemoryClockOffsetMHz) + L" a +" + std::to_wstring(caps.maxMemoryClockOffsetMHz) + L" MHz]";
        SetWindowTextW(s_lblGpuMemRange, r.c_str());
    }
    if (s_lblGpuPowerRange) {
        std::wstring r = L"[" + std::to_wstring(caps.minPowerLimitPercent) + L"% a " + std::to_wstring(caps.maxPowerLimitPercent) + L"%]";
        SetWindowTextW(s_lblGpuPowerRange, r.c_str());
    }

    // Exibe valores solicitados vs aplicados com total transparência técnica
    if (s_lblGpuRealStatus) {
        std::wstring rs = L"Status Real: Core Solicitado: " + std::to_wstring(status.requestedCoreClockOffsetMHz) +
                          L" MHz | Aplicado: " + std::to_wstring(status.appliedCoreClockOffsetMHz) +
                          L" MHz | Power Limit: " + std::to_wstring(status.appliedPowerLimitPercent) + L"%";
        SetWindowTextW(s_lblGpuRealStatus, rs.c_str());
    }

    // Os campos de ajuste de GPU permanecem sempre acessíveis para o usuário configurar
    EnableWindow(s_editGpuCore, TRUE);
    EnableWindow(s_editGpuMem, TRUE);
    EnableWindow(s_editGpuPower, TRUE);
}

static LRESULT CALLBACK GpuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Personalizacao de Consumo, Frequencias e Desempenho da GPU ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 15, 8, 650, 485, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblSelect = CreateWindowW(L"STATIC", L"Adaptador Grafico Selecionado:", WS_CHILD | WS_VISIBLE, 30, 28, 400, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblSelect, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_comboGpuList = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                       30, 46, 620, 150, hwnd, (HMENU)(INT_PTR)ID_GPU_COMBO_SELECT, hInst, nullptr);
        SendMessageW(s_comboGpuList, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_detectedGpus = CoreRuntime::Instance().GetHardwareInfo().gpus;
        int selGpuIdx = 0;
        if (s_detectedGpus.empty()) {
            SendMessageW(s_comboGpuList, CB_ADDSTRING, 0, (LPARAM)L"Adaptador Grafico Padrao do Windows");
        } else {
            for (size_t i = 0; i < s_detectedGpus.size(); ++i) {
                std::wstring desc = L"[" + std::to_wstring(i) + L"] " + StringUtils::ToWide(s_detectedGpus[i].description) +
                                    L" (" + std::to_wstring(s_detectedGpus[i].dedicatedVideoMemoryMB) + L" MB)";
                SendMessageW(s_comboGpuList, CB_ADDSTRING, 0, (LPARAM)desc.c_str());
                if (!adv.gpu.preferredAdapterName.empty() && s_detectedGpus[i].description == adv.gpu.preferredAdapterName) {
                    selGpuIdx = (int)i;
                }
            }
        }
        SendMessageW(s_comboGpuList, CB_SETCURSEL, selGpuIdx, 0);

        s_lblGpuDetails = CreateWindowW(L"STATIC", L"Carregando informacoes de hardware...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        30, 74, 620, 42, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuDetails, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_lblGpuVendorStatus = CreateWindowW(L"STATIC", L"Verificando suporte...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                             30, 118, 620, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuVendorStatus, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Grupo Perfil da GPU
        HWND lblProf = CreateWindowW(L"STATIC", L"Perfil de Consumo / Desempenho:", WS_CHILD | WS_VISIBLE, 30, 142, 250, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblProf, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboGpuProfile = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                          30, 160, 320, 130, hwnd, (HMENU)(INT_PTR)ID_GPU_COMBO_PROFILE, hInst, nullptr);
        SendMessageW(s_comboGpuProfile, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboGpuProfile, CB_ADDSTRING, 0, (LPARAM)L"Economico (Baixo Consumo / Power Limit Reduzido)");
        SendMessageW(s_comboGpuProfile, CB_ADDSTRING, 0, (LPARAM)L"Balanceado (Padrao do Fabricante - 100% TDP)");
        SendMessageW(s_comboGpuProfile, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (Maximo Poder Seguro)");
        SendMessageW(s_comboGpuProfile, CB_ADDSTRING, 0, (LPARAM)L"Personalizado (Ajuste Manual de Clocks e TDP)");
        SendMessageW(s_comboGpuProfile, CB_SETCURSEL, std::clamp(adv.gpu.powerProfile, 0, 3), 0);

        // Controles de Hardware preenchidos com as preferências salvas do usuário
        std::wstring strCore = std::to_wstring(adv.gpu.coreClockOffsetMHz);
        std::wstring strMem = std::to_wstring(adv.gpu.memoryClockOffsetMHz);
        std::wstring strPwr = std::to_wstring(adv.gpu.powerLimitPercent);

        HWND lblCore = CreateWindowW(L"STATIC", L"Clock do Nucleo (Offset MHz):", WS_CHILD | WS_VISIBLE, 30, 198, 200, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblCore, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGpuCore = CreateWindowW(L"EDIT", strCore.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 230, 195, 70, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGpuCore, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_lblGpuCoreRange = CreateWindowW(L"STATIC", L"[-200 a +200 MHz]", WS_CHILD | WS_VISIBLE, 310, 198, 140, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuCoreRange, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblMem = CreateWindowW(L"STATIC", L"Clock da Memoria (Offset MHz):", WS_CHILD | WS_VISIBLE, 30, 226, 200, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMem, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGpuMem = CreateWindowW(L"EDIT", strMem.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 230, 223, 70, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGpuMem, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_lblGpuMemRange = CreateWindowW(L"STATIC", L"[-200 a +400 MHz]", WS_CHILD | WS_VISIBLE, 310, 226, 140, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuMemRange, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblPwr = CreateWindowW(L"STATIC", L"Limite de Potencia (Power Limit %):", WS_CHILD | WS_VISIBLE, 30, 254, 200, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblPwr, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGpuPower = CreateWindowW(L"EDIT", strPwr.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER, 230, 251, 70, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGpuPower, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_lblGpuPowerRange = CreateWindowW(L"STATIC", L"[60% a 115%]", WS_CHILD | WS_VISIBLE, 310, 254, 140, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuPowerRange, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnApplyGpu = CreateWindowW(L"BUTTON", L"Aplicar Ajustes na GPU", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         460, 195, 180, 36, hwnd, (HMENU)(INT_PTR)ID_GPU_BTN_APPLY, hInst, nullptr);
        SendMessageW(btnApplyGpu, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND btnRestoreGpu = CreateWindowW(L"BUTTON", L"Restaurar Padroes", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           460, 238, 180, 32, hwnd, (HMENU)(INT_PTR)ID_GPU_BTN_RESTORE, hInst, nullptr);
        SendMessageW(btnRestoreGpu, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_lblGpuRealStatus = CreateWindowW(L"STATIC", L"Valores Solicitados vs Aplicados: Carregando...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           30, 288, 620, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGpuRealStatus, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Preferência DXGI
        HWND lblPref = CreateWindowW(L"STATIC", L"Preferencia de Aceleracao Grafica no Windows (DXGI):", WS_CHILD | WS_VISIBLE, 30, 316, 420, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblPref, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboGpuPref = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 30, 334, 620, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboGpuPref, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboGpuPref, CB_ADDSTRING, 0, (LPARAM)L"Padrao do Sistema Operacional (Windows Decide)");
        SendMessageW(s_comboGpuPref, CB_ADDSTRING, 0, (LPARAM)L"Economia de Energia (Forcar GPU Integrada)");
        SendMessageW(s_comboGpuPref, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (Forcar GPU Dedicada Prioritaria)");
        SendMessageW(s_comboGpuPref, CB_SETCURSEL, adv.gpu.graphicsPreference, 0);

        s_chkGpuLatency = CreateWindowW(L"BUTTON", L"Sinalizar modo de baixa latencia em renderizacao para aplicacoes 3D (DXGI)",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 366, 620, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkGpuLatency, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkGpuLatency, BM_SETCHECK, adv.gpu.dxgiLowLatency ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblNotice = CreateWindowW(L"STATIC",
            L"Transparencia e Seguranca: Os ajustes de clock e TDP utilizam APIs oficiais (NVAPI / ADLX). Se a placa ou driver nao permitirem alteracao em tempo de execucao, o Opit-Core mantem a operacao segura sem falhas ou BSODs.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 30, 396, 620, 40, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblNotice, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        UpdateGpuDetailsUI(hwnd);
        CreateStandardButtons(hwnd, 680, 550);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_GPU_COMBO_SELECT && code == CBN_SELCHANGE) {
            UpdateGpuDetailsUI(hwnd);
            return 0;
        }

        if (id == ID_GPU_COMBO_PROFILE && code == CBN_SELCHANGE) {
            int selProf = (int)SendMessageW(s_comboGpuProfile, CB_GETCURSEL, 0, 0);
            int selGpu = (int)SendMessageW(s_comboGpuList, CB_GETCURSEL, 0, 0);
            if (selGpu < 0) selGpu = 0;

            GpuPowerProfile prof = GpuPowerProfile::Balanced;
            if (selProf == 0) prof = GpuPowerProfile::Eco;
            else if (selProf == 2) prof = GpuPowerProfile::HighPerformance;
            else if (selProf == 3) prof = GpuPowerProfile::Custom;

            std::string msg;
            GpuControlManager::Instance().ApplyProfile((size_t)selGpu, prof, msg);
            UpdateGpuDetailsUI(hwnd);
            return 0;
        }

        if (id == ID_GPU_BTN_APPLY) {
            int selGpu = (int)SendMessageW(s_comboGpuList, CB_GETCURSEL, 0, 0);
            if (selGpu < 0) selGpu = 0;

            wchar_t b1[32]{}, b2[32]{}, b3[32]{};
            GetWindowTextW(s_editGpuCore, b1, 32);
            GetWindowTextW(s_editGpuMem, b2, 32);
            GetWindowTextW(s_editGpuPower, b3, 32);

            int core = _wtoi(b1);
            int mem = _wtoi(b2);
            int pwr = _wtoi(b3);

            adv.gpu.coreClockOffsetMHz = core;
            adv.gpu.memoryClockOffsetMHz = mem;
            adv.gpu.powerLimitPercent = pwr;
            adv.gpu.powerProfile = 3; // Custom
            SendMessageW(s_comboGpuProfile, CB_SETCURSEL, 3, 0);
            adv.Save();

            std::string msg;
            bool ok = GpuControlManager::Instance().ApplyCustomSettings((size_t)selGpu, core, mem, pwr, msg);
            UpdateGpuDetailsUI(hwnd);

            MessageBoxW(hwnd, StringUtils::ToWide(msg).c_str(), L"Controle de GPU",
                        ok ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONWARNING));
            return 0;
        }

        if (id == ID_GPU_BTN_RESTORE) {
            int selGpu = (int)SendMessageW(s_comboGpuList, CB_GETCURSEL, 0, 0);
            if (selGpu < 0) selGpu = 0;

            std::string msg;
            GpuControlManager::Instance().RestoreDefaults((size_t)selGpu, msg);
            SetWindowTextW(s_editGpuCore, L"0");
            SetWindowTextW(s_editGpuMem, L"0");
            SetWindowTextW(s_editGpuPower, L"100");
            SendMessageW(s_comboGpuProfile, CB_SETCURSEL, 1, 0);

            adv.gpu.coreClockOffsetMHz = 0;
            adv.gpu.memoryClockOffsetMHz = 0;
            adv.gpu.powerLimitPercent = 100;
            adv.gpu.powerProfile = 1;
            adv.Save();

            UpdateGpuDetailsUI(hwnd);

            MessageBoxW(hwnd, StringUtils::ToWide(msg).c_str(), L"Controle de GPU", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            adv.gpu.graphicsPreference = (int)SendMessageW(s_comboGpuPref, CB_GETCURSEL, 0, 0);
            adv.gpu.dxgiLowLatency = (SendMessageW(s_chkGpuLatency, BM_GETCHECK, 0, 0) == BST_CHECKED);

            int selProf = (int)SendMessageW(s_comboGpuProfile, CB_GETCURSEL, 0, 0);
            if (selProf >= 0 && selProf <= 3) {
                adv.gpu.powerProfile = selProf;
            }

            wchar_t b1[32]{}, b2[32]{}, b3[32]{};
            GetWindowTextW(s_editGpuCore, b1, 32);
            GetWindowTextW(s_editGpuMem, b2, 32);
            GetWindowTextW(s_editGpuPower, b3, 32);
            adv.gpu.coreClockOffsetMHz = _wtoi(b1);
            adv.gpu.memoryClockOffsetMHz = _wtoi(b2);
            adv.gpu.powerLimitPercent = _wtoi(b3);

            int sel = (int)SendMessageW(s_comboGpuList, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)s_detectedGpus.size()) {
                adv.gpu.preferredAdapterName = s_detectedGpus[sel].description;
            }

            adv.Save();

            // Aplica os valores configurados na GPU
            int selGpu = (sel >= 0) ? sel : 0;
            std::string gMsg;
            if (adv.gpu.powerProfile == 3) {
                GpuControlManager::Instance().ApplyCustomSettings((size_t)selGpu, adv.gpu.coreClockOffsetMHz, adv.gpu.memoryClockOffsetMHz, adv.gpu.powerLimitPercent, gMsg);
            } else {
                GpuPowerProfile prof = (adv.gpu.powerProfile == 0) ? GpuPowerProfile::Eco :
                                       (adv.gpu.powerProfile == 2) ? GpuPowerProfile::HighPerformance : GpuPowerProfile::Balanced;
                GpuControlManager::Instance().ApplyProfile((size_t)selGpu, prof, gMsg);
            }

            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Configuracoes de GPU salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 10. DIALOGO: ADICIONAR / EDITAR PERFIL DE JOGO
// =========================================================================
static std::string s_editingGameExe;
static HWND s_editGameName = nullptr;
static HWND s_editGameExePath = nullptr;
static HWND s_lblGamePathDisplay = nullptr;
static HWND s_comboGameCpuProf = nullptr;
static HWND s_editGameCpuMin = nullptr;
static HWND s_editGameCpuMax = nullptr;
static HWND s_comboGameGpuProf = nullptr;
static HWND s_editGameGpuPower = nullptr;
static HWND s_editGameGpuCore = nullptr;
static HWND s_editGameGpuMem = nullptr;
static HWND s_comboGameScheme = nullptr;
static HWND s_chkGameDisk = nullptr;
static HWND s_chkGameSleep = nullptr;
static std::vector<PowerScheme> s_gameSchemes;

static LRESULT CALLBACK GameProfileWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Parametros e Politicas de Desempenho do Jogo ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 15, 10, 620, 480, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND l1 = CreateWindowW(L"STATIC", L"Nome Amigavel do Jogo:", WS_CHILD | WS_VISIBLE, 30, 32, 160, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(l1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 30, 50, 280, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameName, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND l2 = CreateWindowW(L"STATIC", L"Executavel Principal (.exe):", WS_CHILD | WS_VISIBLE, 330, 32, 200, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(l2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameExePath = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 330, 50, 190, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameExePath, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnBrowse = CreateWindowW(L"BUTTON", L"Procurar...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       530, 49, 90, 24, hwnd, (HMENU)(INT_PTR)ID_GAME_BTN_BROWSE, hInst, nullptr);
        SendMessageW(btnBrowse, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_lblGamePathDisplay = CreateWindowW(L"STATIC", L"Caminho: (Nenhum executavel selecionado)", WS_CHILD | WS_VISIBLE | SS_LEFT, 30, 78, 590, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblGamePathDisplay, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Bloco CPU
        HWND grpCpu = CreateWindowW(L"BUTTON", L" Configuracoes de CPU para este Jogo (Escala 0 a 100) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    30, 102, 590, 95, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpCpu, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lCpuProf = CreateWindowW(L"STATIC", L"Perfil de CPU:", WS_CHILD | WS_VISIBLE, 45, 126, 120, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lCpuProf, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboGameCpuProf = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 45, 144, 200, 120, hwnd, (HMENU)(INT_PTR)ID_GAME_COMBO_CPU_PROF, hInst, nullptr);
        SendMessageW(s_comboGameCpuProf, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboGameCpuProf, CB_ADDSTRING, 0, (LPARAM)L"Economia (CPU Cap 60%)");
        SendMessageW(s_comboGameCpuProf, CB_ADDSTRING, 0, (LPARAM)L"Balanceado (Padrao 100%)");
        SendMessageW(s_comboGameCpuProf, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (Maximo)");
        SendMessageW(s_comboGameCpuProf, CB_ADDSTRING, 0, (LPARAM)L"Personalizado (Ajuste Manual)");
        SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 2, 0);

        HWND lMinCpu = CreateWindowW(L"STATIC", L"Minimo (% 0-100):", WS_CHILD | WS_VISIBLE, 270, 126, 140, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lMinCpu, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameCpuMin = CreateWindowW(L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 270, 144, 100, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameCpuMin, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lMaxCpu = CreateWindowW(L"STATIC", L"Maximo (% 0-100):", WS_CHILD | WS_VISIBLE, 400, 126, 140, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lMaxCpu, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameCpuMax = CreateWindowW(L"EDIT", L"100", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 400, 144, 100, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameCpuMax, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Bloco GPU
        HWND grpGpu = CreateWindowW(L"BUTTON", L" Configuracoes de GPU para este Jogo ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    30, 205, 590, 105, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpGpu, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lGpuProf = CreateWindowW(L"STATIC", L"Perfil de GPU:", WS_CHILD | WS_VISIBLE, 45, 228, 120, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lGpuProf, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboGameGpuProf = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 45, 246, 200, 120, hwnd, (HMENU)(INT_PTR)ID_GAME_COMBO_GPU_PROF, hInst, nullptr);
        SendMessageW(s_comboGameGpuProf, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboGameGpuProf, CB_ADDSTRING, 0, (LPARAM)L"Economia (Power Limit Reduzido)");
        SendMessageW(s_comboGameGpuProf, CB_ADDSTRING, 0, (LPARAM)L"Balanceado (Padrao 100% TDP)");
        SendMessageW(s_comboGameGpuProf, CB_ADDSTRING, 0, (LPARAM)L"Alto Desempenho (Maximo TDP Seguro)");
        SendMessageW(s_comboGameGpuProf, CB_ADDSTRING, 0, (LPARAM)L"Personalizado (Ajuste Manual)");
        SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 2, 0);

        HWND lGpuPwr = CreateWindowW(L"STATIC", L"Power Limit (%):", WS_CHILD | WS_VISIBLE, 270, 228, 110, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lGpuPwr, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameGpuPower = CreateWindowW(L"EDIT", L"100", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 270, 246, 75, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameGpuPower, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lGpuCore = CreateWindowW(L"STATIC", L"Clock Nucleo (MHz):", WS_CHILD | WS_VISIBLE, 360, 228, 120, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lGpuCore, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameGpuCore = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER, 360, 246, 75, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameGpuCore, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lGpuMem = CreateWindowW(L"STATIC", L"Clock Mem (MHz):", WS_CHILD | WS_VISIBLE, 490, 228, 110, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lGpuMem, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editGameGpuMem = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER, 490, 246, 75, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editGameGpuMem, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Bloco Energia e Proteção
        HWND lScheme = CreateWindowW(L"STATIC", L"Plano de Energia do Windows a Ativar ao Abrir:", WS_CHILD | WS_VISIBLE, 30, 318, 350, 16, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lScheme, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboGameScheme = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 336, 590, 150, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboGameScheme, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        PowerManager pm;
        s_gameSchemes = pm.ListSchemes();
        SendMessageW(s_comboGameScheme, CB_ADDSTRING, 0, (LPARAM)L"[Automatico] Ativar Plano de Alto Desempenho do Sistema");
        for (const auto& s : s_gameSchemes) {
            SendMessageW(s_comboGameScheme, CB_ADDSTRING, 0, (LPARAM)StringUtils::ToWide(s.name).c_str());
        }
        SendMessageW(s_comboGameScheme, CB_SETCURSEL, 0, 0);

        s_chkGameDisk = CreateWindowW(L"BUTTON", L"Proteger HDs/discos contra desligamento acidental durante a partida (timeout=0)",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 375, 590, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkGameDisk, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkGameDisk, BM_SETCHECK, BST_CHECKED, 0);

        s_chkGameSleep = CreateWindowW(L"BUTTON", L"Impedir suspensao automatica do sistema durante o jogo (SetThreadExecutionState)",
                                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 402, 590, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkGameSleep, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkGameSleep, BM_SETCHECK, BST_CHECKED, 0);

        // Se estiver editando um jogo existente, carrega dados salvos
        if (!s_editingGameExe.empty()) {
            GameProfileRecord prof;
            if (DatabaseManager::Instance().GetGameProfile(s_editingGameExe, prof)) {
                SetWindowTextW(s_editGameName, StringUtils::ToWide(prof.gameName).c_str());
                SetWindowTextW(s_editGameExePath, StringUtils::ToWide(prof.exeName).c_str());
                if (!prof.fullPath.empty()) {
                    SetWindowTextW(s_lblGamePathDisplay, (L"Caminho: " + StringUtils::ToWide(prof.fullPath)).c_str());
                }
                SetWindowTextW(s_editGameCpuMin, std::to_wstring(prof.cpuMinPercent).c_str());
                SetWindowTextW(s_editGameCpuMax, std::to_wstring(prof.cpuMaxPercent).c_str());
                SetWindowTextW(s_editGameGpuPower, std::to_wstring(prof.gpuPowerLimitPercent).c_str());
                SetWindowTextW(s_editGameGpuCore, std::to_wstring(prof.gpuCoreOffsetMHz).c_str());
                SetWindowTextW(s_editGameGpuMem, std::to_wstring(prof.gpuMemOffsetMHz).c_str());

                SendMessageW(s_chkGameDisk, BM_SETCHECK, prof.preventDiskSleep ? BST_CHECKED : BST_UNCHECKED, 0);
                SendMessageW(s_chkGameSleep, BM_SETCHECK, prof.preventSystemSleep ? BST_CHECKED : BST_UNCHECKED, 0);

                if (prof.cpuProfile == "Eco") SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 0, 0);
                else if (prof.cpuProfile == "Balanced") SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 1, 0);
                else if (prof.cpuProfile == "Custom") SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 3, 0);
                else SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 2, 0);

                if (prof.gpuProfile == "Eco") SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 0, 0);
                else if (prof.gpuProfile == "Balanced") SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 1, 0);
                else if (prof.gpuProfile == "Custom") SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 3, 0);
                else SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 2, 0);
            }
        }

        // Botão Potência Máxima do Silício (Stock / Sem Overclock)
        HWND btnMaxSilicon = CreateWindowW(L"BUTTON", L"⚡ Potencia Maxima do Silicio (Stock / Sem Overclock)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           30, 500, 375, 32, hwnd, (HMENU)(INT_PTR)ID_GAME_BTN_MAX_SILICON, hInst, nullptr);
        SendMessageW(btnMaxSilicon, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        ui::Theme::ApplyDarkExplorerTheme(btnMaxSilicon);

        // Botões Salvar e Cancelar
        HWND btnSave = CreateWindowW(L"BUTTON", L"Salvar Perfil", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                     420, 500, 110, 32, hwnd, (HMENU)(INT_PTR)ID_GAME_BTN_SAVE, hInst, nullptr);
        SendMessageW(btnSave, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        ui::Theme::ApplyDarkExplorerTheme(btnSave);

        HWND btnCancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       540, 500, 95, 32, hwnd, (HMENU)(INT_PTR)ID_GAME_BTN_CANCEL, hInst, nullptr);
        SendMessageW(btnCancel, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        ui::Theme::ApplyDarkExplorerTheme(btnCancel);

        EnumChildWindows(hwnd, [](HWND hChild, LPARAM) -> BOOL {
            ui::Theme::ApplyDarkExplorerTheme(hChild);
            return TRUE;
        }, 0);

        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_GAME_BTN_BROWSE) {
            wchar_t szFile[MAX_PATH] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Executaveis (*.exe)\0*.exe\0Todos os Arquivos (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn)) {
                std::wstring fullPath(szFile);
                size_t slash = fullPath.find_last_of(L"\\/");
                std::wstring exeName = (slash != std::wstring::npos) ? fullPath.substr(slash + 1) : fullPath;

                SetWindowTextW(s_editGameExePath, exeName.c_str());
                SetWindowTextW(s_lblGamePathDisplay, (L"Caminho: " + fullPath).c_str());

                // Se o nome do jogo estiver vazio, preenche com base no exe
                wchar_t curName[128]{};
                GetWindowTextW(s_editGameName, curName, 128);
                if (wcslen(curName) == 0) {
                    size_t dot = exeName.find_last_of(L".");
                    std::wstring base = (dot != std::wstring::npos) ? exeName.substr(0, dot) : exeName;
                    SetWindowTextW(s_editGameName, base.c_str());
                }
            }
            return 0;
        }

        if (id == ID_GAME_COMBO_CPU_PROF && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(s_comboGameCpuProf, CB_GETCURSEL, 0, 0);
            if (sel == 0) { SetWindowTextW(s_editGameCpuMin, L"0"); SetWindowTextW(s_editGameCpuMax, L"60"); }
            else if (sel == 1) { SetWindowTextW(s_editGameCpuMin, L"5"); SetWindowTextW(s_editGameCpuMax, L"100"); }
            else if (sel == 2) { SetWindowTextW(s_editGameCpuMin, L"5"); SetWindowTextW(s_editGameCpuMax, L"100"); }
            return 0;
        }

        if (id == ID_GAME_COMBO_GPU_PROF && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(s_comboGameGpuProf, CB_GETCURSEL, 0, 0);
            if (sel == 0) { SetWindowTextW(s_editGameGpuPower, L"70"); SetWindowTextW(s_editGameGpuCore, L"-50"); }
            else if (sel == 1) { SetWindowTextW(s_editGameGpuPower, L"100"); SetWindowTextW(s_editGameGpuCore, L"0"); }
            else if (sel == 2) { SetWindowTextW(s_editGameGpuPower, L"110"); SetWindowTextW(s_editGameGpuCore, L"50"); }
            return 0;
        }

        if (id == ID_GAME_BTN_MAX_SILICON) {
            // 1. Configurar CPU para capacidade nativa total sem overclock (evita latência de escalonamento de clock)
            SendMessageW(s_comboGameCpuProf, CB_SETCURSEL, 2, 0); // Alto Desempenho
            SetWindowTextW(s_editGameCpuMin, L"100");
            SetWindowTextW(s_editGameCpuMax, L"100");

            // 2. Configurar GPU para máximo TDP de fábrica estável (100% Power Limit, 0 MHz offsets stock)
            SendMessageW(s_comboGameGpuProf, CB_SETCURSEL, 2, 0); // Alto Desempenho
            SetWindowTextW(s_editGameGpuPower, L"100");
            SetWindowTextW(s_editGameGpuCore, L"0");
            SetWindowTextW(s_editGameGpuMem, L"0");

            // 3. Proteger discos e impedir suspensão do sistema durante gameplay
            SendMessageW(s_chkGameDisk, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(s_chkGameSleep, BM_SETCHECK, BST_CHECKED, 0);

            // 4. Selecionar plano de alto desempenho
            SendMessageW(s_comboGameScheme, CB_SETCURSEL, 0, 0);

            MessageBoxW(hwnd,
                L"Parametros configurados com sucesso para Potencia Maxima do Silicio!\n\n"
                L"• CPU: Min 100% / Max 100% (Sem flutuacao ou travamento por clock)\n"
                L"• GPU: 100% Power Limit nativo / 0 MHz Offset (Silicio estavel de fabrica)\n"
                L"• Energia: Modo Desempenho Maximo com protecao ativa de discos e suspensao\n"
                L"• Sem overclock, sem sobretensao e com 0 risco de instabilidade.",
                L"Potencia Maxima do Silicio (Stock) — Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        if (id == ID_GAME_BTN_SAVE) {
            wchar_t wName[128]{}, wExe[128]{}, wMinCpu[32]{}, wMaxCpu[32]{};
            wchar_t wGpuPwr[32]{}, wGpuCore[32]{}, wGpuMem[32]{};

            GetWindowTextW(s_editGameName, wName, 128);
            GetWindowTextW(s_editGameExePath, wExe, 128);
            GetWindowTextW(s_editGameCpuMin, wMinCpu, 32);
            GetWindowTextW(s_editGameCpuMax, wMaxCpu, 32);
            GetWindowTextW(s_editGameGpuPower, wGpuPwr, 32);
            GetWindowTextW(s_editGameGpuCore, wGpuCore, 32);
            GetWindowTextW(s_editGameGpuMem, wGpuMem, 32);

            std::string exe = StringUtils::ToLower(StringUtils::Trim(StringUtils::ToUtf8(wExe)));
            if (exe.empty()) {
                MessageBoxW(hwnd, L"Informe o nome do executavel do jogo (ex: jogo.exe)!", L"Validacao", MB_OK | MB_ICONWARNING);
                return 0;
            }

            uint32_t minCpu = _wtoi(wMinCpu);
            uint32_t maxCpu = _wtoi(wMaxCpu);

            // Validação estrita: Minimo superior ao Maximo deve ser rejeitado sem alterar salvamento
            if (minCpu > 100 || maxCpu > 100) {
                MessageBoxW(hwnd, L"Os valores de CPU devem estar na escala normalizada de 0 a 100%!", L"Validacao de CPU", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (minCpu > maxCpu) {
                MessageBoxW(hwnd, L"Conflito de CPU: O valor Minimo nao pode ser maior que o valor Maximo!", L"Conflito de Configuracao", MB_OK | MB_ICONWARNING);
                return 0;
            }

            GameProfileRecord prof;
            prof.exeName = exe;
            prof.gameName = StringUtils::ToUtf8(wName);
            prof.cpuMinPercent = minCpu;
            prof.cpuMaxPercent = maxCpu;

            int cpuIdx = (int)SendMessageW(s_comboGameCpuProf, CB_GETCURSEL, 0, 0);
            if (cpuIdx == 0) prof.cpuProfile = "Eco";
            else if (cpuIdx == 1) prof.cpuProfile = "Balanced";
            else if (cpuIdx == 3) prof.cpuProfile = "Custom";
            else prof.cpuProfile = "HighPerformance";

            int gpuIdx = (int)SendMessageW(s_comboGameGpuProf, CB_GETCURSEL, 0, 0);
            if (gpuIdx == 0) prof.gpuProfile = "Eco";
            else if (gpuIdx == 1) prof.gpuProfile = "Balanced";
            else if (gpuIdx == 3) prof.gpuProfile = "Custom";
            else prof.gpuProfile = "HighPerformance";

            prof.gpuPowerLimitPercent = _wtoi(wGpuPwr);
            prof.gpuCoreOffsetMHz = _wtoi(wGpuCore);
            prof.gpuMemOffsetMHz = _wtoi(wGpuMem);

            int schIdx = (int)SendMessageW(s_comboGameScheme, CB_GETCURSEL, 0, 0);
            if (schIdx > 0 && schIdx - 1 < (int)s_gameSchemes.size()) {
                prof.powerSchemeGuid = s_gameSchemes[schIdx - 1].guid;
            }

            prof.preventDiskSleep = (SendMessageW(s_chkGameDisk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            prof.preventSystemSleep = (SendMessageW(s_chkGameSleep, BM_GETCHECK, 0, 0) == BST_CHECKED);
            prof.enabled = true;

            // Salva no banco de dados e sincroniza com o GameDetector
            DatabaseManager::Instance().SaveGameProfile(prof);
            CoreRuntime::Instance().GetGameDetector().AddGame(prof.exeName);
            Config::Instance().AddCustomGame(prof.exeName);

            MessageBoxW(hwnd, L"Perfil de jogo salvo com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == ID_GAME_BTN_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 6. DIÁLOGO: VENTOINHAS (ARQUITETURA MODULAR EXTENSÍVEL)
// =========================================================================
static HWND s_comboFanDev = nullptr;
static HWND s_lblFanDevDetails = nullptr;
static HWND s_lblFanSensors = nullptr;
static HWND s_comboFanDevMode = nullptr;
static HWND s_editFanManualSpeed = nullptr;
static HWND s_btnFanApplySpeed = nullptr;
static HWND s_btnFanRestoreFirmware = nullptr;
static HWND s_editFanCurveHyst = nullptr;
static HWND s_editFanCurveEmerg = nullptr;
static HWND s_lblFanDiagnostics = nullptr;
static std::vector<FanDeviceInfo> s_fanDevicesList;

static void RefreshFanDeviceUI(HWND hwnd) {
    auto& mgr = FanControlManager::Instance();
    s_fanDevicesList = mgr.GetDevices();
    if (s_fanDevicesList.empty()) {
        SetWindowTextW(s_lblFanDevDetails, L"Nenhum controlador de ventilador detectado.");
        SetWindowTextW(s_lblFanSensors, L"Sensores termicos indisponiveis.");
        SetWindowTextW(s_lblFanDiagnostics, L"Sem hardware compativel.");
        EnableWindow(s_editFanManualSpeed, FALSE);
        EnableWindow(s_btnFanApplySpeed, FALSE);
        EnableWindow(s_btnFanRestoreFirmware, FALSE);
        return;
    }

    int sel = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)s_fanDevicesList.size()) sel = 0;

    const auto& dev = s_fanDevicesList[sel];

    std::wstring devInfo = L"Dispositivo: " + StringUtils::ToWide(dev.name) + L" | Fabricante: " + StringUtils::ToWide(dev.vendor) +
                          L" (" + StringUtils::ToWide(FanDeviceTypeToString(dev.deviceType)) + L")\r\n" +
                          L"Capacidades: " +
                          (dev.capabilities.canReadTemperature ? L"[Leitura Temp] " : L"") +
                          (dev.capabilities.canReadRpm ? L"[Tacometro RPM] " : L"") +
                          (dev.capabilities.canControlSpeed ? L"[Controle Velocidade %] " : L"[Apenas Monitoramento]") +
                          (dev.capabilities.canSetCurve ? L"[Curva Personalizada]" : L"");
    SetWindowTextW(s_lblFanDevDetails, devInfo.c_str());

    // Sensores
    std::wstring sensorsInfo = L"Temperatura: ";
    if (dev.latestReading.hasTemperature) {
        sensorsInfo += std::to_wstring(static_cast<int>(dev.latestReading.temperatureC)) + L" °C";
    } else {
        sensorsInfo += L"N/D";
    }

    sensorsInfo += L" | Rotacao Atual: ";
    if (dev.latestReading.hasRpm) {
        sensorsInfo += std::to_wstring(dev.latestReading.rpm) + L" RPM";
    } else {
        sensorsInfo += L"Tacometro N/D (Sem sensor RPM)";
    }

    if (dev.latestReading.hasReportedSpeedPercent) {
        sensorsInfo += L" (" + std::to_wstring(dev.latestReading.reportedSpeedPercent) + L"%)";
    }

    sensorsInfo += L"\r\nFonte: " + StringUtils::ToWide(dev.latestReading.sensorSource);
    if (dev.latestReading.isStale) {
        sensorsInfo += L" [Amostra Antiga/Pausada]";
    } else {
        sensorsInfo += L" [Tempo Real]";
    }
    SetWindowTextW(s_lblFanSensors, sensorsInfo.c_str());

    // Modo
    SendMessageW(s_comboFanDevMode, CB_SETCURSEL, static_cast<int>(dev.activeMode), 0);
    SetWindowTextW(s_editFanManualSpeed, std::to_wstring(dev.manualTargetSpeedPercent).c_str());
    SetWindowTextW(s_editFanCurveHyst, std::to_wstring(dev.curveConfig.hysteresisC).c_str());
    SetWindowTextW(s_editFanCurveEmerg, std::to_wstring(static_cast<int>(dev.curveConfig.emergencyTempC)).c_str());

    // Habilitação de controles
    BOOL canCtrl = dev.capabilities.canControlSpeed ? TRUE : FALSE;
    EnableWindow(s_comboFanDevMode, canCtrl);
    EnableWindow(s_editFanManualSpeed, canCtrl && dev.activeMode == FanControlMode::ManualFixed);
    EnableWindow(s_btnFanApplySpeed, canCtrl);
    EnableWindow(s_btnFanRestoreFirmware, canCtrl);

    // Diagnóstico
    std::wstring diag = L"API / Provedor: ";
    if (dev.id == "LHM_0" || dev.vendor == "LibreHardwareMonitor") {
        diag += L"LibreHardwareMonitor Bridge (.NET / PawnIO). Monitoramento em tempo real de temperatura e rotacao (RPM) dos sensores do hardware.";
    } else if (dev.deviceType == FanDeviceType::GpuNvidia) {
        diag += L"NVIDIA NVAPI nativo (nvapi64.dll). Controle e leitura de cooler suportados pela VBIOS da GPU.";
    } else if (dev.deviceType == FanDeviceType::GpuAmd) {
        diag += L"AMD ADL Overdrive (atiadlxx.dll). Controle de velocidade e sensor termico suportados pelo driver.";
    } else if (dev.deviceType == FanDeviceType::CpuMotherboard) {
        diag += L"Windows ACPI / WMI (MSAcpi_ThermalZoneTemperature). Controle direto de PWM em placa-mae exige driver Ring 0 com risco de BSOD. Gerenciado com seguranca pelo firmware do BIOS.";
    } else if (dev.deviceType == FanDeviceType::GpuIntel) {
        diag += L"Intel Integrated/Arc Graphics. GPU integrada compartilha refrigeracao da CPU sem ventoinha propria independente.";
    } else if (dev.deviceType == FanDeviceType::LaptopEC) {
        diag += L"ACPI Embedded Controller (EC). Controle protegido por firmware de notebook do fabricante.";
    } else {
        diag += L"Emulador Virtual de Testes de Seguranca (Mock).";
    }

    if (!dev.latestReading.lastErrorMessage.empty()) {
        diag += L"\r\nStatus: " + StringUtils::ToWide(dev.latestReading.lastErrorMessage);
    }
    SetWindowTextW(s_lblFanDiagnostics, diag.c_str());
}

static LRESULT CALLBACK FansWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();
    auto& mgr = FanControlManager::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Modulo Avancado de Ventoinhas: Hardware, Sensores e Controle ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 620, 435, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // 1. Seção Dispositivos
        HWND lblDev = CreateWindowW(L"STATIC", L"1. Dispositivo / Controlador Detectado:", WS_CHILD | WS_VISIBLE, 35, 30, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblDev, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_comboFanDev = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      35, 50, 480, 150, hwnd, (HMENU)(INT_PTR)ID_FAN_COMBO_DEV, hInst, nullptr);
        SendMessageW(s_comboFanDev, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnRefresh = CreateWindowW(L"BUTTON", L"Atualizar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        525, 48, 95, 26, hwnd, (HMENU)(INT_PTR)ID_FAN_BTN_REFRESH, hInst, nullptr);
        SendMessageW(btnRefresh, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_lblFanDevDetails = CreateWindowW(L"STATIC", L"Identificando hardware...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           35, 80, 585, 36, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblFanDevDetails, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // 2. Seção Sensores
        HWND grpSensor = CreateWindowW(L"BUTTON", L" 2. Telemetria de Sensores ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      35, 120, 585, 60, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpSensor, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_lblFanSensors = CreateWindowW(L"STATIC", L"Aguardando leitura de telemetria...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        50, 138, 555, 36, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblFanSensors, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // 3. Seção Controle
        HWND grpCtrl = CreateWindowW(L"BUTTON", L" 3. Modo de Operacao e Controle de Velocidade ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                     35, 185, 585, 125, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpCtrl, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblMode = CreateWindowW(L"STATIC", L"Modo de Operacao:", WS_CHILD | WS_VISIBLE, 50, 208, 140, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMode, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboFanDevMode = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                          195, 205, 240, 120, hwnd, (HMENU)(INT_PTR)ID_FAN_COMBO_MODE, hInst, nullptr);
        SendMessageW(s_comboFanDevMode, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboFanDevMode, CB_ADDSTRING, 0, (LPARAM)L"Automatico (Firmware / BIOS)");
        SendMessageW(s_comboFanDevMode, CB_ADDSTRING, 0, (LPARAM)L"Velocidade Fixa (Manual %)");
        SendMessageW(s_comboFanDevMode, CB_ADDSTRING, 0, (LPARAM)L"Curva Personalizada (Auto)");
        SendMessageW(s_comboFanDevMode, CB_SETCURSEL, std::clamp(adv.fans.controlMode, 0, 2), 0);

        s_btnFanRestoreFirmware = CreateWindowW(L"BUTTON", L"Restaurar BIOS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                445, 204, 160, 26, hwnd, (HMENU)(INT_PTR)ID_FAN_BTN_RESTORE_FIRMWARE, hInst, nullptr);
        SendMessageW(s_btnFanRestoreFirmware, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblSpeed = CreateWindowW(L"STATIC", L"Velocidade Fixa (%):", WS_CHILD | WS_VISIBLE, 50, 242, 140, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblSpeed, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_editFanManualSpeed = CreateWindowW(L"EDIT", L"50", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                             195, 239, 60, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editFanManualSpeed, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_btnFanApplySpeed = CreateWindowW(L"BUTTON", L"Aplicar % Manual", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           265, 238, 170, 26, hwnd, (HMENU)(INT_PTR)ID_FAN_BTN_APPLY_SPEED, hInst, nullptr);
        SendMessageW(s_btnFanApplySpeed, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblHyst = CreateWindowW(L"STATIC", L"Histerese (°C):", WS_CHILD | WS_VISIBLE, 50, 276, 95, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblHyst, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editFanCurveHyst = CreateWindowW(L"EDIT", L"3", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                           150, 273, 50, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editFanCurveHyst, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblEmerg = CreateWindowW(L"STATIC", L"Temp. Emergencia (°C):", WS_CHILD | WS_VISIBLE, 215, 276, 150, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblEmerg, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editFanCurveEmerg = CreateWindowW(L"EDIT", L"90", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                            370, 273, 50, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editFanCurveEmerg, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnGraph = CreateWindowW(L"BUTTON", L"📈 Grafico da Curva...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      435, 271, 170, 28, hwnd, (HMENU)(INT_PTR)ID_FAN_BTN_GRAPH_EDITOR, hInst, nullptr);
        SendMessageW(btnGraph, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // 4. Seção Diagnóstico e Transparência
        HWND grpDiag = CreateWindowW(L"BUTTON", L" 4. Diagnostico de Hardware e Conformidade ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                     35, 315, 585, 115, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpDiag, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_lblFanDiagnostics = CreateWindowW(L"STATIC", L"Inspecionando barramentos...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           50, 335, 555, 85, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblFanDiagnostics, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Preencher Combobox de Dispositivos (atualizando leitura antes de exibir)
        mgr.RefreshTelemetryOnly();
        s_fanDevicesList = mgr.GetDevices();
        int defaultSel = 0;
        bool foundControllable = false;
        for (size_t i = 0; i < s_fanDevicesList.size(); ++i) {
            std::wstring item = L"[" + std::to_wstring(i) + L"] " + StringUtils::ToWide(s_fanDevicesList[i].name);
            if (s_fanDevicesList[i].capabilities.canControlSpeed) {
                item += L" (Controle Ativo)";
                defaultSel = (int)i; // Prioriza o cooler com controle dinâmico (ex: GPU NVIDIA)
                foundControllable = true;
            } else {
                item += L" (Monitoramento)";
                if (!foundControllable && (s_fanDevicesList[i].latestReading.hasTemperature || s_fanDevicesList[i].latestReading.hasRpm)) {
                    defaultSel = (int)i; // Senão, prioriza dispositivo com sensores ativos (ex: LibreHardwareMonitor)
                }
            }
            SendMessageW(s_comboFanDev, CB_ADDSTRING, 0, (LPARAM)item.c_str());
        }
        SendMessageW(s_comboFanDev, CB_SETCURSEL, defaultSel, 0);

        RefreshFanDeviceUI(hwnd);
        SetTimer(hwnd, 1, 2000, nullptr);
        CreateStandardButtons(hwnd, 660, 500);
        return 0;
    }
    case WM_TIMER: {
        if (wp == 1) {
            mgr.RefreshTelemetryOnly();
            s_fanDevicesList = mgr.GetDevices();
            RefreshFanDeviceUI(hwnd);
        }
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_FAN_COMBO_DEV && code == CBN_SELCHANGE) {
            RefreshFanDeviceUI(hwnd);
            return 0;
        }

        if (id == ID_FAN_BTN_REFRESH) {
            mgr.RefreshTelemetryOnly();
            // Atualizar leitura dos dispositivos
            s_fanDevicesList = mgr.GetDevices();
            RefreshFanDeviceUI(hwnd);
            return 0;
        }

        if (id == ID_FAN_COMBO_MODE && code == CBN_SELCHANGE) {
            int selDev = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
            if (selDev >= 0 && selDev < (int)s_fanDevicesList.size()) {
                int selMode = (int)SendMessageW(s_comboFanDevMode, CB_GETCURSEL, 0, 0);
                mgr.SetDeviceMode(s_fanDevicesList[selDev].id, static_cast<FanControlMode>(selMode));
                RefreshFanDeviceUI(hwnd);
            }
            return 0;
        }

        if (id == ID_FAN_BTN_APPLY_SPEED) {
            int selDev = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
            if (selDev >= 0 && selDev < (int)s_fanDevicesList.size()) {
                wchar_t b[32]{};
                GetWindowTextW(s_editFanManualSpeed, b, 32);
                uint32_t speed = _wtoi(b);
                if (speed > 100) speed = 100;
                if (speed < 20) speed = 20;

                bool ok = mgr.SetManualSpeed(s_fanDevicesList[selDev].id, speed);
                RefreshFanDeviceUI(hwnd);
                if (ok) {
                    MessageBoxW(hwnd, L"Velocidade aplicada com sucesso!", L"Opit-Core Ventoinhas", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"Nao foi possivel aplicar a velocidade no dispositivo selecionado.", L"Aviso de Seguranca", MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }

        if (id == ID_FAN_BTN_RESTORE_FIRMWARE) {
            int selDev = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
            if (selDev >= 0 && selDev < (int)s_fanDevicesList.size()) {
                mgr.RestoreFirmwareControl(s_fanDevicesList[selDev].id);
                RefreshFanDeviceUI(hwnd);
                MessageBoxW(hwnd, L"Controle devolvido ao BIOS / Firmware do hardware.", L"Opit-Core Ventoinhas", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        if (id == ID_FAN_BTN_GRAPH_EDITOR) {
            int selDev = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
            std::string devId = (selDev >= 0 && selDev < (int)s_fanDevicesList.size()) ? s_fanDevicesList[selDev].id : "";
            FanCurveEditorDialog::Show(hwnd, devId);
            RefreshFanDeviceUI(hwnd);
            return 0;
        }

        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            adv.fans.controlMode = (int)SendMessageW(s_comboFanDevMode, CB_GETCURSEL, 0, 0);

            int selDev = (int)SendMessageW(s_comboFanDev, CB_GETCURSEL, 0, 0);
            if (selDev >= 0 && selDev < (int)s_fanDevicesList.size()) {
                wchar_t bh[32]{}, be[32]{};
                GetWindowTextW(s_editFanCurveHyst, bh, 32);
                GetWindowTextW(s_editFanCurveEmerg, be, 32);

                FanDeviceInfo info;
                if (mgr.GetDeviceInfo(s_fanDevicesList[selDev].id, info)) {
                    info.curveConfig.hysteresisC = _wtoi(bh);
                    info.curveConfig.emergencyTempC = _wtof(be);
                    info.curveConfig.Validate();
                    mgr.SetDeviceCurve(s_fanDevicesList[selDev].id, info.curveConfig);
                }
            }

            mgr.SaveConfigurations();
            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Configuracoes de Ventoinhas salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 7. DIÁLOGO: GUARDIÃO TÉRMICO
// =========================================================================
static HWND s_editThermAlert = nullptr;
static HWND s_editThermCrit = nullptr;
static HWND s_editThermHyst = nullptr;
static HWND s_comboThermCap = nullptr;
static HWND s_editThermConfirm = nullptr;
static HWND s_chkThermLog = nullptr;

static LRESULT CALLBACK ThermalWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Parametros de Protecao Termica e Histerese ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 420, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lbl1 = CreateWindowW(L"STATIC", L"Limite de Alerta / Inicio de Acao (°C):", WS_CHILD | WS_VISIBLE, 35, 45, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editThermAlert = CreateWindowW(L"EDIT", std::to_wstring((int)adv.thermal.alertLimitC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 42, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editThermAlert, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl2 = CreateWindowW(L"STATIC", L"Limite Critico de Emergencia (°C):", WS_CHILD | WS_VISIBLE, 35, 80, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editThermCrit = CreateWindowW(L"EDIT", std::to_wstring((int)adv.thermal.criticalLimitC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 77, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editThermCrit, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl3 = CreateWindowW(L"STATIC", L"Histerese de Restauracao (°C):", WS_CHILD | WS_VISIBLE, 35, 115, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editThermHyst = CreateWindowW(L"EDIT", std::to_wstring((int)adv.thermal.hysteresisRestoreC).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 112, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editThermHyst, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl4 = CreateWindowW(L"STATIC", L"Cap de CPU sob Throttle Protetor (%):", WS_CHILD | WS_VISIBLE, 35, 150, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl4, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_comboThermCap = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 340, 147, 180, 100, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboThermCap, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboThermCap, CB_ADDSTRING, 0, (LPARAM)L"70% (Padrao Seguro)");
        SendMessageW(s_comboThermCap, CB_ADDSTRING, 0, (LPARAM)L"60% (Arrefecimento Rapido)");
        SendMessageW(s_comboThermCap, CB_ADDSTRING, 0, (LPARAM)L"80% (Mais Desempenho)");
        SendMessageW(s_comboThermCap, CB_SETCURSEL, 0, 0);

        HWND lbl5 = CreateWindowW(L"STATIC", L"Tempo minimo de confirmacao (ms):", WS_CHILD | WS_VISIBLE, 35, 185, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl5, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editThermConfirm = CreateWindowW(L"EDIT", std::to_wstring(adv.thermal.confirmationTimeMs).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 340, 182, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editThermConfirm, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_chkThermLog = CreateWindowW(L"BUTTON", L"Registrar incidentes termicos no banco de auditoria SQLite",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 225, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkThermLog, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkThermLog, BM_SETCHECK, adv.thermal.logThermalIncidents ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblNotice = CreateWindowW(L"STATIC",
            L"Inviolabilidade: A protecao termica tem precedencia absoluta sobre qualquer perfil de jogo ou desempenho. Se o limite critico for atingido, a potencia e reduzida imediatamente para resfriamento.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 35, 260, 560, 50, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblNotice, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        CreateStandardButtons(hwnd, 640, 490);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            wchar_t b1[32]{}, b2[32]{}, b3[32]{}, b4[32]{};
            GetWindowTextW(s_editThermAlert, b1, 32);
            GetWindowTextW(s_editThermCrit, b2, 32);
            GetWindowTextW(s_editThermHyst, b3, 32);
            GetWindowTextW(s_editThermConfirm, b4, 32);

            adv.thermal.alertLimitC = _wtof(b1);
            adv.thermal.criticalLimitC = _wtof(b2);
            adv.thermal.hysteresisRestoreC = _wtof(b3);
            adv.thermal.confirmationTimeMs = _wtoi(b4);
            adv.thermal.logThermalIncidents = (SendMessageW(s_chkThermLog, BM_GETCHECK, 0, 0) == BST_CHECKED);

            int selCap = (int)SendMessageW(s_comboThermCap, CB_GETCURSEL, 0, 0);
            if (selCap == 1) adv.thermal.throttleActionCap = 60;
            else if (selCap == 2) adv.thermal.throttleActionCap = 80;
            else adv.thermal.throttleActionCap = 70;

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Parametros do Guardiao Termico salvos com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 8. DIÁLOGO: ESTABILIDADE
// =========================================================================
static HWND s_editStabSamples = nullptr;
static HWND s_editStabMinGain = nullptr;
static HWND s_chkStabAutoRevert = nullptr;
static HWND s_chkStabDpc = nullptr;
static HWND s_editStabNoise = nullptr;

static LRESULT CALLBACK StabilityWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        HWND grp = CreateWindowW(L"BUTTON", L" Parametros de Diagnostico de Estabilidade e DPC ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 20, 10, 600, 420, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lbl1 = CreateWindowW(L"STATIC", L"Janela de Amostras para Diagnostico (3 a 20):", WS_CHILD | WS_VISIBLE, 35, 45, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editStabSamples = CreateWindowW(L"EDIT", std::to_wstring(adv.stability.observationWindowSamples).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 360, 42, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editStabSamples, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl2 = CreateWindowW(L"STATIC", L"Melhoria Minima para Validar Experimento (%):", WS_CHILD | WS_VISIBLE, 35, 80, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editStabMinGain = CreateWindowW(L"EDIT", std::to_wstring((int)adv.stability.minImprovementPercent).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 360, 77, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editStabMinGain, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lbl3 = CreateWindowW(L"STATIC", L"Tolerancia a Ruido Natural / Variacao (%):", WS_CHILD | WS_VISIBLE, 35, 115, 320, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        s_editStabNoise = CreateWindowW(L"EDIT", std::to_wstring((int)adv.stability.noiseTolerancePercent).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 360, 112, 80, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editStabNoise, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_chkStabAutoRevert = CreateWindowW(L"BUTTON", L"Reverter imediatamente qualquer alteracao que cause regressao",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 160, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkStabAutoRevert, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkStabAutoRevert, BM_SETCHECK, adv.stability.autoRevertOnRegression ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkStabDpc = CreateWindowW(L"BUTTON", L"Monitorar variacoes abruptas de CPU e picos de latencia de apresentacao",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 190, 560, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkStabDpc, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkStabDpc, BM_SETCHECK, adv.stability.monitorDpcLatency ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblNotice = CreateWindowW(L"STATIC",
            L"Metodologia: O orquestrador mede a linha de base antes de qualquer ajuste. Se o delta de pontuacao for menor que o limiar de ganho ou gerar micro-stutter, o rollback e disparado automaticamente pelo Diario de Recuperacao.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 35, 230, 560, 75, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblNotice, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        CreateStandardButtons(hwnd, 640, 490);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            wchar_t b1[32]{}, b2[32]{}, b3[32]{};
            GetWindowTextW(s_editStabSamples, b1, 32);
            GetWindowTextW(s_editStabMinGain, b2, 32);
            GetWindowTextW(s_editStabNoise, b3, 32);

            adv.stability.observationWindowSamples = _wtoi(b1);
            adv.stability.minImprovementPercent = _wtof(b2);
            adv.stability.noiseTolerancePercent = _wtof(b3);
            adv.stability.autoRevertOnRegression = (SendMessageW(s_chkStabAutoRevert, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.stability.monitorDpcLatency = (SendMessageW(s_chkStabDpc, BM_GETCHECK, 0, 0) == BST_CHECKED);

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Parametros de Estabilidade salvos com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// 9. DIÁLOGO: SERVIÇOS WINDOWS
// =========================================================================
#define ID_SVC_BTN_RESTORE_NOW 8150

static HWND s_chkSvcPauseGame = nullptr;
static HWND s_chkSvcPauseEco = nullptr;
static HWND s_chkSvcPauseHighCpu = nullptr;
static HWND s_editSvcHighCpuThresh = nullptr;
static HWND s_chkSvcBloatPerm = nullptr;
static HWND s_comboSvcAiAutonomy = nullptr;
static HWND s_chkSvcWSearch = nullptr;
static HWND s_chkSvcSysMain = nullptr;
static HWND s_chkSvcSpooler = nullptr;
static HWND s_chkSvcDiagTrack = nullptr;
static HWND s_chkSvcThirdParty = nullptr;
static HWND s_chkSvcRestoreExit = nullptr;
static HWND s_chkSvcCheckDeps = nullptr;
static HWND s_chkSvcTruePause = nullptr;
static HWND s_chkSvcIdleSweep = nullptr;
static HWND s_editSvcIdleSecs = nullptr;
static HWND s_editSvcMinGain = nullptr;
static HWND s_editSvcMaintHour = nullptr;
static HWND s_editSvcMaintMins = nullptr;
static HWND s_editSvcUserExcl = nullptr;
static HWND s_editSvcExcluded = nullptr;
static HWND s_btnSvcRestoreNow = nullptr;

static LRESULT CALLBACK ServicesWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    auto& adv = ModuleAdvancedSettings::Instance();

    switch (msg) {
    case WM_CREATE: {
        // --- 1. GATILHOS DE ATIVAÇÃO ---
        HWND grp1 = CreateWindowW(L"BUTTON", L" 1. Gatilhos de Ativacao (Quando Pausar / Retomar) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  25, 10, 630, 122, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp1, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_chkSvcPauseGame = CreateWindowW(L"BUTTON", L"Ao detectar Jogos / Modo Gamer (pausa imediata, restaura ao fechar)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 30, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcPauseGame, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcPauseGame, BM_SETCHECK, adv.services.pauseOnGame ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcPauseEco = CreateWindowW(L"BUTTON", L"Em Modo Bateria ou Perfil Economico (reduz consumo em segundo plano)",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 52, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcPauseEco, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcPauseEco, BM_SETCHECK, adv.services.pauseOnEco ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcPauseHighCpu = CreateWindowW(L"BUTTON", L"Sob Alta Carga de Hardware (se uso da CPU ultrapassar o limiar):",
                                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 74, 430, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcPauseHighCpu, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcPauseHighCpu, BM_SETCHECK, adv.services.pauseOnHighCpu ? BST_CHECKED : BST_UNCHECKED, 0);

        std::wstring wThresh = std::to_wstring(adv.services.highCpuThreshold);
        s_editSvcHighCpuThresh = CreateWindowW(L"EDIT", wThresh.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                              475, 73, 40, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcHighCpuThresh, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblPct = CreateWindowW(L"STATIC", L"%", WS_CHILD | WS_VISIBLE, 520, 75, 25, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblPct, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_chkSvcBloatPerm = CreateWindowW(L"BUTTON", L"Manter bloatwares e telemetrias secundarias sempre pausados",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 96, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcBloatPerm, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcBloatPerm, BM_SETCHECK, adv.services.pauseBloatwarePermanent ? BST_CHECKED : BST_UNCHECKED, 0);

        // --- 2. NÍVEL DE AUTONOMIA DA IA ---
        HWND grp2 = CreateWindowW(L"BUTTON", L" 2. Nivel de Autonomia da Inteligencia Artificial (IA) ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  25, 138, 630, 75, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp2, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND lblAi = CreateWindowW(L"STATIC", L"Grau de intervencao da IA na gestao de servicos:", WS_CHILD | WS_VISIBLE, 40, 158, 600, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblAi, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboSvcAiAutonomy = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 40, 178, 600, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboSvcAiAutonomy, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboSvcAiAutonomy, CB_ADDSTRING, 0, (LPARAM)L"0 - Manual (Apenas regras fixas do usuario; IA nao altera servicos)");
        SendMessageW(s_comboSvcAiAutonomy, CB_ADDSTRING, 0, (LPARAM)L"1 - Consultivo (Recomendado: IA analisa, orienta no chat e pede confirmacao)");
        SendMessageW(s_comboSvcAiAutonomy, CB_ADDSTRING, 0, (LPARAM)L"2 - Automatico Inteligente (IA orquestra dinamicamente conforme o objetivo)");
        int aiSel = std::clamp(adv.services.aiAutonomyLevel, 0, 2);
        SendMessageW(s_comboSvcAiAutonomy, CB_SETCURSEL, aiSel, 0);

        // --- 3. CATEGORIAS E SERVIÇOS ELEGÍVEIS ---
        HWND grp3 = CreateWindowW(L"BUTTON", L" 3. Categorias e Servicos Elegiveis ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  25, 220, 630, 95, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp3, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_chkSvcWSearch = CreateWindowW(L"BUTTON", L"Windows Search (WSearch - Indexacao de arquivos)",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 240, 290, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcWSearch, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcWSearch, BM_SETCHECK, adv.services.manageWSearch ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcSysMain = CreateWindowW(L"BUTTON", L"SysMain (Superfetch - Pre-carregamento)",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 263, 290, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcSysMain, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcSysMain, BM_SETCHECK, adv.services.manageSysMain ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcSpooler = CreateWindowW(L"BUTTON", L"Spooler de Impressao (sem fila ativa)",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 286, 290, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcSpooler, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcSpooler, BM_SETCHECK, adv.services.manageSpooler ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcDiagTrack = CreateWindowW(L"BUTTON", L"Telemetria do Windows (DiagTrack)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 340, 240, 300, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcDiagTrack, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcDiagTrack, BM_SETCHECK, adv.services.manageDiagTrack ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcThirdParty = CreateWindowW(L"BUTTON", L"Atualizadores (Google, Edge, Adobe, etc.)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 340, 263, 300, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcThirdParty, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcThirdParty, BM_SETCHECK, adv.services.manageThirdPartyUpdaters ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcIdleSweep = CreateWindowW(L"BUTTON", L"Varredura de ociosidade (pausa e volta ao usar)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 340, 286, 300, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcIdleSweep, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcIdleSweep, BM_SETCHECK, adv.services.pauseWhenIdle ? BST_CHECKED : BST_UNCHECKED, 0);

        // --- 4. PROTEÇÃO, DEPENDÊNCIAS E EXCEÇÕES ---
        HWND grp4 = CreateWindowW(L"BUTTON", L" 4. Protecao, Dependencias e Excecoes ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  25, 322, 630, 200, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grp4, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        s_chkSvcRestoreExit = CreateWindowW(L"BUTTON", L"Restaurar obrigatoriamente todos os servicos modificados ao sair do Opit-Core",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 342, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcRestoreExit, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcRestoreExit, BM_SETCHECK, adv.services.restoreOnExit ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcCheckDeps = CreateWindowW(L"BUTTON", L"Verificar dependencias no SCM antes de pausar (seguranca contra falhas)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 364, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcCheckDeps, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcCheckDeps, BM_SETCHECK, adv.services.checkDependencies ? BST_CHECKED : BST_UNCHECKED, 0);

        s_chkSvcTruePause = CreateWindowW(L"BUTTON", L"Usar PAUSE do SCM quando o servico aceitar (volta mais rapido, sem perder estado)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 40, 386, 600, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkSvcTruePause, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_chkSvcTruePause, BM_SETCHECK, adv.services.allowTruePause ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblUserExcl = CreateWindowW(L"STATIC", L"Excecoes do Usuario (Servicos que NUNCA devem ser pausados - separados por ';'):", WS_CHILD | WS_VISIBLE, 40, 412, 600, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblUserExcl, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        std::wstring wUserExcl = StringUtils::ToWide(adv.services.customUserExcludedServices);
        s_editSvcUserExcl = CreateWindowW(L"EDIT", wUserExcl.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 40, 432, 600, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcUserExcl, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblCrit = CreateWindowW(L"STATIC", L"Servicos essenciais bloqueados pelo sistema (inviolaveis):", WS_CHILD | WS_VISIBLE, 40, 464, 600, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblCrit, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        std::wstring wExcl = StringUtils::ToWide(adv.services.customExcludedServices);
        s_editSvcExcluded = CreateWindowW(L"EDIT", wExcl.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED, 40, 484, 600, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcExcluded, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Limites medidos da automacao: quanto tempo de ociosidade vale encostar, quanto de memoria precisa
        // valer a pena e quando os servicos "adormecidos" acordam para atualizar.
        HWND lblIdleS = CreateWindowW(L"STATIC", L"Ocioso apos", WS_CHILD | WS_VISIBLE, 40, 496, 70, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblIdleS, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        std::wstring wIdleSecs = std::to_wstring(adv.services.idleSecondsBeforePause);
        s_editSvcIdleSecs = CreateWindowW(L"EDIT", wIdleSecs.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 112, 494, 45, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcIdleSecs, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        HWND lblIdleS2 = CreateWindowW(L"STATIC", L"s   ·   ganho minimo", WS_CHILD | WS_VISIBLE, 162, 496, 118, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblIdleS2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        std::wstring wMinGain = std::to_wstring(adv.services.minGainMB);
        s_editSvcMinGain = CreateWindowW(L"EDIT", wMinGain.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 284, 494, 45, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcMinGain, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        HWND lblIdleS3 = CreateWindowW(L"STATIC", L"MB   ·   manutencao as", WS_CHILD | WS_VISIBLE, 334, 496, 132, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblIdleS3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        std::wstring wMaintH = std::to_wstring(adv.services.maintenanceHour);
        s_editSvcMaintHour = CreateWindowW(L"EDIT", wMaintH.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 470, 494, 35, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcMaintHour, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        HWND lblIdleS4 = CreateWindowW(L"STATIC", L"h por", WS_CHILD | WS_VISIBLE, 510, 496, 40, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblIdleS4, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        std::wstring wMaintM = std::to_wstring(adv.services.maintenanceMinutes);
        s_editSvcMaintMins = CreateWindowW(L"EDIT", wMaintM.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 552, 494, 40, 22, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editSvcMaintMins, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        HWND lblIdleS5 = CreateWindowW(L"STATIC", L"min", WS_CHILD | WS_VISIBLE, 597, 496, 35, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblIdleS5, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // --- BOTÃO DE AÇÃO DIRETA NO RODAPÉ ---
        s_btnSvcRestoreNow = CreateWindowW(L"BUTTON", L"Restaurar Servicos Agora", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           25, 536, 180, 28, hwnd, (HMENU)(UINT_PTR)ID_SVC_BTN_RESTORE_NOW, hInst, nullptr);
        SendMessageW(s_btnSvcRestoreNow, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        CreateStandardButtons(hwnd, 680, 580);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == ID_SVC_BTN_RESTORE_NOW) {
            ServiceManager::Instance().RestoreAllServicesNow();
            MessageBoxW(hwnd, L"Todos os servicos foram restaurados com sucesso ao padrao do Windows!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            return 0;
        } else if (id == ID_ADV_OK || id == ID_ADV_SAVE) {
            adv.services.pauseOnGame = (SendMessageW(s_chkSvcPauseGame, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.pauseOnEco = (SendMessageW(s_chkSvcPauseEco, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.pauseOnHighCpu = (SendMessageW(s_chkSvcPauseHighCpu, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t bThresh[32]{};
            GetWindowTextW(s_editSvcHighCpuThresh, bThresh, 32);
            int thresh = _wtoi(bThresh);
            if (thresh > 0) adv.services.highCpuThreshold = static_cast<uint32_t>(thresh);

            adv.services.pauseBloatwarePermanent = (SendMessageW(s_chkSvcBloatPerm, BM_GETCHECK, 0, 0) == BST_CHECKED);

            int aiSel = (int)SendMessageW(s_comboSvcAiAutonomy, CB_GETCURSEL, 0, 0);
            if (aiSel >= 0 && aiSel <= 2) adv.services.aiAutonomyLevel = aiSel;

            adv.services.manageWSearch = (SendMessageW(s_chkSvcWSearch, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.manageSysMain = (SendMessageW(s_chkSvcSysMain, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.manageSpooler = (SendMessageW(s_chkSvcSpooler, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.manageDiagTrack = (SendMessageW(s_chkSvcDiagTrack, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.manageThirdPartyUpdaters = (SendMessageW(s_chkSvcThirdParty, BM_GETCHECK, 0, 0) == BST_CHECKED);

            adv.services.restoreOnExit = (SendMessageW(s_chkSvcRestoreExit, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.checkDependencies = (SendMessageW(s_chkSvcCheckDeps, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.allowTruePause = (SendMessageW(s_chkSvcTruePause, BM_GETCHECK, 0, 0) == BST_CHECKED);
            adv.services.pauseWhenIdle = (SendMessageW(s_chkSvcIdleSweep, BM_GETCHECK, 0, 0) == BST_CHECKED);

            wchar_t bNum[32]{};
            GetWindowTextW(s_editSvcIdleSecs, bNum, 32);
            int idleSecs = _wtoi(bNum);
            if (idleSecs > 0) adv.services.idleSecondsBeforePause = static_cast<uint32_t>(idleSecs);
            GetWindowTextW(s_editSvcMinGain, bNum, 32);
            int minGain = _wtoi(bNum);
            if (minGain >= 0) adv.services.minGainMB = static_cast<uint32_t>(minGain);
            GetWindowTextW(s_editSvcMaintHour, bNum, 32);
            int maintH = _wtoi(bNum);
            if (maintH >= 0 && maintH <= 23) adv.services.maintenanceHour = maintH;
            GetWindowTextW(s_editSvcMaintMins, bNum, 32);
            int maintM = _wtoi(bNum);
            if (maintM > 0) adv.services.maintenanceMinutes = maintM;

            wchar_t bUserExcl[512]{};
            GetWindowTextW(s_editSvcUserExcl, bUserExcl, 512);
            adv.services.customUserExcludedServices = StringUtils::ToUtf8(std::wstring(bUserExcl));

            adv.Save();
            CoreRuntime::Instance().ApplyUnifiedSettings();

            if (id == ID_ADV_OK) {
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"Configuracoes de Servicos Windows salvas com sucesso!", L"Opit-Core", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        } else if (id == ID_ADV_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =========================================================================
// ROTEADOR CENTRAL DOS 9 PAINÉIS AVANÇADOS
// =========================================================================
bool AdvancedModuleDialogs::ShowDialog(int moduleIndex, HWND hParent) {
    switch (moduleIndex) {
        case 0: ShowPowerPlansDialog(hParent); return true;
        case 1: ShowCpuMinMaxDialog(hParent); return true;
        case 2: ShowCoreParkingDialog(hParent); return true;
        case 3: ShowProcessesDialog(hParent); return true;
        case 4: ShowGpuDialog(hParent); return true;
        case 5: ShowFansDialog(hParent); return true;
        case 6: ShowThermalDialog(hParent); return true;
        case 7: ShowStabilityDialog(hParent); return true;
        case 8: ShowServicesDialog(hParent); return true;
        default: return false;
    }
}

void AdvancedModuleDialogs::ShowPowerPlansDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Planos de Energia", 640, 490, PowerPlansWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowCpuMinMaxDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: CPU Min/Max", 640, 520, CpuMinMaxWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowCoreParkingDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Core Parking", 640, 490, CoreParkingWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowProcessesDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Controlo de Processos", 640, 490, ProcessesWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowGpuDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Controlo de GPU", 680, 560, GpuWndProc);
    RunModalLoop(dlg, hParent);
}

bool AdvancedModuleDialogs::ShowGameProfileDialog(HWND hParent, const std::string& exeName) {
    s_editingGameExe = exeName;
    std::wstring title = exeName.empty() ? L"Adicionar Novo Perfil de Jogo" :
                         (L"Editar Perfil de Jogo: " + StringUtils::ToWide(exeName));
    HWND dlg = CreateModalWindow(hParent, title.c_str(), 660, 560, GameProfileWndProc);
    RunModalLoop(dlg, hParent);
    return true;
}

void AdvancedModuleDialogs::ShowFansDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Modulo de Ventoinhas e Curvas", 660, 500, FansWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowThermalDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Guardiao Termico", 640, 490, ThermalWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowStabilityDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Estabilidade do Sistema", 640, 490, StabilityWndProc);
    RunModalLoop(dlg, hParent);
}

void AdvancedModuleDialogs::ShowServicesDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Configuracao Avancada: Servicos Windows", 680, 580, ServicesWndProc);
    RunModalLoop(dlg, hParent);
}

static HWND s_comboAiProv = nullptr;
static HWND s_comboAiModel = nullptr;
static HWND s_editAiKey = nullptr;
static HWND s_editAiEndpoint = nullptr;
static HWND s_editAiPrompt = nullptr;
static HWND s_editAiOutput = nullptr;
static HWND s_lblAiStatus = nullptr;
static HWND s_btnAiAnalyze = nullptr;
static HWND s_btnAiAsk = nullptr;
static HWND s_btnAiFetch = nullptr;
static HWND s_chkAiPeriodic = nullptr;
static HWND s_comboAiInterval = nullptr;
static HWND s_btnAiApplyAction = nullptr;
static HWND s_btnAiRollback = nullptr;
static HWND s_chkAiNotify = nullptr;
static HWND s_lblAiAskQuestion = nullptr;
static HWND s_btnAiAskOpt[4] = {nullptr, nullptr, nullptr, nullptr};

static const UINT ID_AI_BTN_SAVE_KEY = 1501;
static const UINT ID_AI_BTN_ANALYZE = 1502;
static const UINT ID_AI_BTN_ASK = 1503;
static const UINT ID_AI_BTN_CLOSE = 1504;
static const UINT ID_AI_BTN_FETCH_MODELS = 1505;
static const UINT ID_AI_COMBO_PROV = 1506;
static const UINT ID_AI_BTN_APPLY_ACTION = 1507;
static const UINT ID_AI_BTN_ROLLBACK = 1508;
static const UINT ID_AI_CHK_NOTIFY = 1509;
static const UINT ID_AI_ASK_OPT_BASE = 1520;

static std::wstring FormatAiResponseForDisplay(const std::string& rawUtf8) {
    if (rawUtf8.empty()) return L"";

    std::wstring input = StringUtils::ToWide(rawUtf8);

    // Normaliza quebras de linha para \n
    std::wstring normalized;
    normalized.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == L'\r') {
            if (i + 1 < input.size() && input[i + 1] == L'\n') {
                i++;
            }
            normalized += L'\n';
        } else {
            normalized += input[i];
        }
    }

    // Separa as linhas
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < normalized.size()) {
        size_t end = normalized.find(L'\n', start);
        if (end == std::wstring::npos) {
            lines.push_back(normalized.substr(start));
            break;
        }
        lines.push_back(normalized.substr(start, end - start));
        start = end + 1;
    }

    std::wstring result;
    result.reserve(normalized.size() * 3 / 2);

    auto trimLeft = [](const std::wstring& s) -> std::wstring {
        size_t p = s.find_first_not_of(L" \t");
        return (p == std::wstring::npos) ? L"" : s.substr(p);
    };

    auto stripMarkdownBold = [](const std::wstring& s) -> std::wstring {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (i + 1 < s.size() && s[i] == L'*' && s[i + 1] == L'*') {
                i++; // ignora **
            } else if (i + 1 < s.size() && s[i] == L'_' && s[i + 1] == L'_') {
                i++; // ignora __
            } else {
                out += s[i];
            }
        }
        return out;
    };

    bool prevWasHeader = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = lines[i];
        std::wstring trimmed = trimLeft(line);

        // Remove cercas de codigo
        if (trimmed.rfind(L"```", 0) == 0) {
            continue;
        }

        // Divisores horizontais
        if (trimmed == L"---" || trimmed == L"***" || trimmed == L"___" ||
            (trimmed.rfind(L"---", 0) == 0 && trimmed.size() >= 3 && trimmed.find_first_not_of(L"-") == std::wstring::npos)) {
            result += L"\r\n--------------------------------------------------------------------------------\r\n\r\n";
            prevWasHeader = false;
            continue;
        }

        // Titulo Nivel 1
        if (trimmed.rfind(L"# ", 0) == 0) {
            std::wstring title = stripMarkdownBold(trimmed.substr(2));
            result += L"\r\n================================================================================\r\n";
            result += L"  " + title + L"\r\n";
            result += L"================================================================================\r\n\r\n";
            prevWasHeader = true;
            continue;
        }

        // Titulo Nivel 2
        if (trimmed.rfind(L"## ", 0) == 0) {
            std::wstring title = stripMarkdownBold(trimmed.substr(3));
            result += L"\r\n────────────────────────────────────────────────────────────────────────────────\r\n";
            result += L"■ " + title + L"\r\n";
            result += L"────────────────────────────────────────────────────────────────────────────────\r\n\r\n";
            prevWasHeader = true;
            continue;
        }

        // Titulo Nivel 3
        if (trimmed.rfind(L"### ", 0) == 0) {
            std::wstring title = stripMarkdownBold(trimmed.substr(4));
            result += L"\r\n► " + title + L"\r\n";
            result += L"──────────────────────────────────────────────────────\r\n";
            prevWasHeader = true;
            continue;
        }

        // Titulo Nivel 4
        if (trimmed.rfind(L"#### ", 0) == 0) {
            std::wstring title = stripMarkdownBold(trimmed.substr(5));
            result += L"\r\n  ▪ " + title + L"\r\n";
            prevWasHeader = true;
            continue;
        }

        // Marcadores de lista simples
        if (trimmed.rfind(L"* ", 0) == 0 || trimmed.rfind(L"- ", 0) == 0 || trimmed.rfind(L"+ ", 0) == 0) {
            std::wstring item = stripMarkdownBold(trimmed.substr(2));
            result += L"  • " + item + L"\r\n";
            prevWasHeader = false;
            continue;
        }

        // Sub-itens indentados
        if (line.rfind(L"  * ", 0) == 0 || line.rfind(L"  - ", 0) == 0 || line.rfind(L"\t* ", 0) == 0 || line.rfind(L"\t- ", 0) == 0) {
            std::wstring item = stripMarkdownBold(trimLeft(line.substr(4)));
            result += L"      - " + item + L"\r\n";
            prevWasHeader = false;
            continue;
        }

        // Linha em branco
        if (trimmed.empty()) {
            if (!prevWasHeader) {
                result += L"\r\n";
            }
            prevWasHeader = false;
            continue;
        }

        // Texto regular limpo de markdown
        std::wstring cleanLine = stripMarkdownBold(line);
        result += cleanLine + L"\r\n";
        prevWasHeader = false;
    }

    return result;
}

// Mostra/esconde a faixa de pergunta da IA (protocolo opit_ask): enunciado + opcoes clicaveis.
static void UpdateAiAskUI(HWND hwnd) {
    auto& ai = AiAssistantManager::Instance();
    if (!ai.HasPendingAiAsk()) {
        ShowWindow(s_lblAiAskQuestion, SW_HIDE);
        for (HWND b : s_btnAiAskOpt) ShowWindow(b, SW_HIDE);
        return;
    }
    AiAsk ask = ai.GetPendingAiAsk();
    std::wstring q = L"Pergunta da IA para voce: " + StringUtils::ToWide(ask.question);
    SetWindowTextW(s_lblAiAskQuestion, q.c_str());
    ShowWindow(s_lblAiAskQuestion, SW_SHOW);
    for (int i = 0; i < 4; ++i) {
        if (i < static_cast<int>(ask.options.size())) {
            SetWindowTextW(s_btnAiAskOpt[i], StringUtils::ToWide(ask.options[i]).c_str());
            ShowWindow(s_btnAiAskOpt[i], SW_SHOW);
        } else {
            ShowWindow(s_btnAiAskOpt[i], SW_HIDE);
        }
    }
    (void)hwnd;
}

static LRESULT CALLBACK AiAssistantWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    switch (msg) {
    case WM_CREATE: {
        auto& ai = AiAssistantManager::Instance();
        ai.ClearPendingAiAsk();

        HWND grpProv = CreateWindowW(L"BUTTON", L" Configuracao de Inteligencia Artificial Multi-Provedor (BYOK) ",
                                     WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                     20, 10, 705, 200, hwnd, nullptr, hInst, nullptr);
        SendMessageW(grpProv, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Provedor
        HWND lbl1 = CreateWindowW(L"STATIC", L"Provedor:", WS_CHILD | WS_VISIBLE, 35, 34, 65, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl1, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboAiProv = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      105, 31, 230, 200, hwnd, (HMENU)(UINT_PTR)ID_AI_COMBO_PROV, hInst, nullptr);
        SendMessageW(s_comboAiProv, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"Google Gemini");
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"OpenAI (ChatGPT)");
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"Anthropic (Claude)");
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"DeepSeek");
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"Groq Cloud");
        SendMessageW(s_comboAiProv, CB_ADDSTRING, 0, (LPARAM)L"Personalizado / Local (Ollama, etc)");

        int provIndex = static_cast<int>(ai.GetProvider());
        if (provIndex < 0 || provIndex > 5) provIndex = 0;
        SendMessageW(s_comboAiProv, CB_SETCURSEL, provIndex, 0);

        // Modelo
        HWND lblMod = CreateWindowW(L"STATIC", L"Modelo:", WS_CHILD | WS_VISIBLE, 350, 34, 55, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblMod, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboAiModel = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | CBS_AUTOHSCROLL,
                                       410, 31, 300, 220, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboAiModel, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        std::string curMod = ai.GetSelectedModel();
        if (curMod.find("argon") != std::string::npos) {
            curMod = "gemini-2.0-flash";
            ai.SetSelectedModel(curMod);
        }
        if (!curMod.empty()) {
            std::wstring wMod = StringUtils::ToWide(curMod);
            SendMessageW(s_comboAiModel, CB_ADDSTRING, 0, (LPARAM)wMod.c_str());
            SetWindowTextW(s_comboAiModel, wMod.c_str());
        }

        // Chave API
        HWND lbl2 = CreateWindowW(L"STATIC", L"Chave API:", WS_CHILD | WS_VISIBLE, 35, 69, 65, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl2, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        std::wstring curKey = StringUtils::ToWide(ai.GetApiKey());
        s_editAiKey = CreateWindowW(L"EDIT", curKey.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                                    105, 66, 320, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editAiKey, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_btnAiFetch = CreateWindowW(L"BUTTON", L"Buscar Modelos", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     435, 65, 140, 26, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_FETCH_MODELS, hInst, nullptr);
        SendMessageW(s_btnAiFetch, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        HWND btnSaveKey = CreateWindowW(L"BUTTON", L"Salvar Tudo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        585, 65, 125, 26, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_SAVE_KEY, hInst, nullptr);
        SendMessageW(btnSaveKey, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Custom Endpoint
        HWND lblEnd = CreateWindowW(L"STATIC", L"Endpoint URL:", WS_CHILD | WS_VISIBLE, 35, 104, 85, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblEnd, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        std::wstring curEnd = StringUtils::ToWide(ai.GetCustomEndpoint());
        s_editAiEndpoint = CreateWindowW(L"EDIT", curEnd.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                         125, 101, 380, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editAiEndpoint, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND lblEndHint = CreateWindowW(L"STATIC", L"(Usado p/ Provedor Personalizado/Local)", WS_CHILD | WS_VISIBLE,
                                        515, 104, 195, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblEndHint, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        // Otimizacao Estrategica Periodica por IA
        s_chkAiPeriodic = CreateWindowW(L"BUTTON", L" Otimizacao Estrategica Periodica por IA em segundo plano",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        35, 137, 440, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_chkAiPeriodic, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        SendMessageW(s_chkAiPeriodic, BM_SETCHECK, ai.IsPeriodicEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND lblInt = CreateWindowW(L"STATIC", L"Intervalo:", WS_CHILD | WS_VISIBLE, 485, 166, 60, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lblInt, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_comboAiInterval = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                          550, 163, 160, 120, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_comboAiInterval, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_comboAiInterval, CB_ADDSTRING, 0, (LPARAM)L"A cada 15 min");
        SendMessageW(s_comboAiInterval, CB_ADDSTRING, 0, (LPARAM)L"A cada 30 min (Padrao)");
        SendMessageW(s_comboAiInterval, CB_ADDSTRING, 0, (LPARAM)L"A cada 60 min");

        int curInt = ai.GetPeriodicIntervalMinutes();
        int selIntIdx = (curInt == 15) ? 0 : (curInt == 60 ? 2 : 1);
        SendMessageW(s_comboAiInterval, CB_SETCURSEL, selIntIdx, 0);

        // Baloes de aviso do tray quando o motor vê algo acontecer (jogo, servico, processo, recomendacao)
        s_chkAiNotify = CreateWindowW(L"BUTTON", L" Avisar eventos no tray (balao) quando o programa ver algo acontecer",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      35, 163, 440, 24, hwnd, (HMENU)(UINT_PTR)ID_AI_CHK_NOTIFY, hInst, nullptr);
        SendMessageW(s_chkAiNotify, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        SendMessageW(s_chkAiNotify, BM_SETCHECK,
                     NotificationManager::Instance().IsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

        // Diagnostico Button
        s_btnAiAnalyze = CreateWindowW(L"BUTTON", L" Gerar Diagnostico Completo do Hardware e Otimizacoes com IA ",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       20, 222, 705, 34, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_ANALYZE, hInst, nullptr);
        SendMessageW(s_btnAiAnalyze, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Prompt
        HWND lbl3 = CreateWindowW(L"STATIC", L"Pergunta tecnica sobre o hardware / otimizacao:", WS_CHILD | WS_VISIBLE,
                                  20, 264, 400, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl3, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_editAiPrompt = CreateWindowW(L"EDIT", L"Quais configuracoes ideais de CPU e GPU para maxima economia no meu sistema?",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                       20, 284, 575, 26, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editAiPrompt, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_btnAiAsk = CreateWindowW(L"BUTTON", L"Perguntar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   605, 283, 120, 28, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_ASK, hInst, nullptr);
        SendMessageW(s_btnAiAsk, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        // Resposta
        HWND lbl4 = CreateWindowW(L"STATIC", L"Parecer Tecnico e Resposta da IA:", WS_CHILD | WS_VISIBLE,
                                  20, 318, 300, 18, hwnd, nullptr, hInst, nullptr);
        SendMessageW(lbl4, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        s_editAiOutput = CreateWindowW(L"EDIT", L"Aguardando solicitacao... Insira sua chave de API e clique em 'Buscar Modelos' ou 'Gerar Diagnostico'.",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
                                       20, 338, 705, 245, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_editAiOutput, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        SendMessageW(s_editAiOutput, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));

        std::string lastRec = ai.GetLastRecommendation();
        if (!lastRec.empty()) {
            std::wstring initialText = L"[ Ultima recomendacao estrategica salva da IA ]\r\n\r\n" + FormatAiResponseForDisplay(lastRec);
            SetWindowTextW(s_editAiOutput, initialText.c_str());
        }

        s_btnAiApplyAction = CreateWindowW(L"BUTTON", L"⚡ Aplicar Otimizacoes Recomendadas",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          20, 642, 345, 30, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_APPLY_ACTION, hInst, nullptr);
        SendMessageW(s_btnAiApplyAction, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        EnableWindow(s_btnAiApplyAction, ai.HasPendingAiAction() ? TRUE : FALSE);

        s_btnAiRollback = CreateWindowW(L"BUTTON", L"↩️ Desfazer Alteracoes da IA",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       375, 642, 350, 30, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_ROLLBACK, hInst, nullptr);
        SendMessageW(s_btnAiRollback, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        EnableWindow(s_btnAiRollback, ai.CanRollbackAiAction() ? TRUE : FALSE);

        // Faixa de pergunta da IA (protocolo opit_ask): enunciado + ate 4 opcoes, ocultos ate chegar um ask.
        s_lblAiAskQuestion = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT,
                                           20, 588, 705, 20, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblAiAskQuestion, WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);

        static const int kAskOptX[4] = {20, 198, 376, 554};
        for (int i = 0; i < 4; ++i) {
            s_btnAiAskOpt[i] = CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_PUSHBUTTON,
                                             kAskOptX[i], 608, 170, 26, hwnd,
                                             (HMENU)(UINT_PTR)(ID_AI_ASK_OPT_BASE + (UINT)i), hInst, nullptr);
            SendMessageW(s_btnAiAskOpt[i], WM_SETFONT, (WPARAM)GetDlgFont(true), TRUE);
        }

        s_lblAiStatus = CreateWindowW(L"STATIC", ai.HasApiKey() ? L"Status: Chave de API configurada e pronta para uso." : L"Status: Nenhuma chave salva. Insira sua chave do Google Gemini, OpenAI, Claude, DeepSeek ou Groq.",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 682, 565, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(s_lblAiStatus, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);

        HWND btnClose = CreateWindowW(L"BUTTON", L"Fechar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      595, 678, 130, 30, hwnd, (HMENU)(UINT_PTR)ID_AI_BTN_CLOSE, hInst, nullptr);
        SendMessageW(btnClose, WM_SETFONT, (WPARAM)GetDlgFont(), TRUE);
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (id == ID_AI_COMBO_PROV && code == CBN_SELCHANGE) {
            int pSel = (int)SendMessageW(s_comboAiProv, CB_GETCURSEL, 0, 0);
            if (pSel == static_cast<int>(AiProvider::Custom)) {
                wchar_t epBuf[512]{};
                GetWindowTextW(s_editAiEndpoint, epBuf, 512);
                if (wcslen(epBuf) == 0) {
                    SetWindowTextW(s_editAiEndpoint, L"http://localhost:11434/v1");
                }
            }
            return 0;
        }

        if (id == ID_AI_BTN_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == ID_AI_CHK_NOTIFY) {
            bool on = (SendMessageW(s_chkAiNotify, BM_GETCHECK, 0, 0) == BST_CHECKED);
            NotificationManager::Instance().SetEnabled(on);
            return 0;
        }

        for (int i = 0; i < 4; ++i) {
            if (id == ID_AI_ASK_OPT_BASE + (UINT)i) {
                wchar_t optBuf[256]{};
                GetWindowTextW(s_btnAiAskOpt[i], optBuf, 256);
                SetWindowTextW(s_editAiPrompt, optBuf);
                AiAssistantManager::Instance().ClearPendingAiAsk();
                UpdateAiAskUI(hwnd);
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_AI_BTN_ASK, BN_CLICKED), 0);
                return 0;
            }
        }

        if (id == ID_AI_BTN_FETCH_MODELS) {
            wchar_t kBuf[512]{};
            GetWindowTextW(s_editAiKey, kBuf, 512);
            int pSel = (int)SendMessageW(s_comboAiProv, CB_GETCURSEL, 0, 0);
            wchar_t epBuf[512]{};
            GetWindowTextW(s_editAiEndpoint, epBuf, 512);

            std::string key = StringUtils::ToUtf8(kBuf);
            if (key.empty() && pSel != static_cast<int>(AiProvider::Custom)) {
                MessageBoxW(hwnd, L"Por favor insira a Chave de API para listar os modelos disponiveis.", L"Opit-Core IA", MB_OK | MB_ICONWARNING);
                return 0;
            }

            auto& ai = AiAssistantManager::Instance();
            ai.SetProvider(static_cast<AiProvider>(pSel));
            ai.SetApiKey(key);
            ai.SetCustomEndpoint(StringUtils::ToUtf8(epBuf));

            SetWindowTextW(s_lblAiStatus, L"Status: Consultando API do provedor para listar os modelos disponiveis...");
            EnableWindow(s_btnAiFetch, FALSE);

            std::thread([hwnd]() {
                std::vector<AiModelInfo> models;
                std::string err;
                bool ok = AiAssistantManager::Instance().FetchAvailableModels(models, err);

                if (ok && !models.empty()) {
                    SendMessageW(s_comboAiModel, CB_RESETCONTENT, 0, 0);
                    std::string curMod = AiAssistantManager::Instance().GetSelectedModel();
                    if (curMod.find("argon") != std::string::npos) {
                        curMod = "gemini-2.0-flash";
                    }
                    int selectIdx = 0;
                    for (size_t i = 0; i < models.size(); ++i) {
                        std::wstring wId = StringUtils::ToWide(models[i].id);
                        SendMessageW(s_comboAiModel, CB_ADDSTRING, 0, (LPARAM)wId.c_str());
                        if (!curMod.empty() && (models[i].id == curMod || models[i].displayName == curMod)) {
                            selectIdx = (int)i;
                        } else if (curMod.empty() && (models[i].id == "gemini-2.0-flash" || models[i].id == "gemini-1.5-flash")) {
                            selectIdx = (int)i;
                        }
                    }
                    SendMessageW(s_comboAiModel, CB_SETCURSEL, selectIdx, 0);
                    SetWindowTextW(s_comboAiModel, StringUtils::ToWide(models[selectIdx].id).c_str());
                    AiAssistantManager::Instance().SetSelectedModel(models[selectIdx].id);

                    std::wstring okMsg = L"Status: Sucesso! " + std::to_wstring(models.size()) + L" modelos encontrados e listados.";
                    SetWindowTextW(s_lblAiStatus, okMsg.c_str());
                    MessageBoxW(hwnd, (L"Modelos obtidos com sucesso! Foram encontrados " + std::to_wstring(models.size()) + L" modelos com a sua chave.").c_str(), L"Opit-Core IA", MB_OK | MB_ICONINFORMATION);
                } else {
                    std::wstring errMsg = L"Status: " + StringUtils::ToWide(err);
                    SetWindowTextW(s_lblAiStatus, errMsg.c_str());
                    MessageBoxW(hwnd, (L"Nao foi possivel listar os modelos:\r\n" + StringUtils::ToWide(err)).c_str(), L"Opit-Core IA", MB_OK | MB_ICONERROR);
                }
                EnableWindow(s_btnAiFetch, TRUE);
            }).detach();
            return 0;
        }

        if (id == ID_AI_BTN_SAVE_KEY) {
            wchar_t kBuf[512]{};
            GetWindowTextW(s_editAiKey, kBuf, 512);
            int pSel = (int)SendMessageW(s_comboAiProv, CB_GETCURSEL, 0, 0);
            wchar_t epBuf[512]{};
            GetWindowTextW(s_editAiEndpoint, epBuf, 512);
            wchar_t modBuf[256]{};
            GetWindowTextW(s_comboAiModel, modBuf, 256);

            auto& ai = AiAssistantManager::Instance();
            ai.SetProvider(static_cast<AiProvider>(pSel));
            ai.SetApiKey(StringUtils::ToUtf8(kBuf));
            ai.SetCustomEndpoint(StringUtils::ToUtf8(epBuf));
            ai.SetSelectedModel(StringUtils::ToUtf8(modBuf));

            bool periodicOn = (SendMessageW(s_chkAiPeriodic, BM_GETCHECK, 0, 0) == BST_CHECKED);
            int intIdx = (int)SendMessageW(s_comboAiInterval, CB_GETCURSEL, 0, 0);
            int minutes = (intIdx == 0) ? 15 : (intIdx == 2 ? 60 : 30);
            ai.SetPeriodicEnabled(periodicOn);
            ai.SetPeriodicIntervalMinutes(minutes);

            ai.SaveSettings();

            SetWindowTextW(s_lblAiStatus, L"Status: Configuracoes de IA salvas com sucesso!");
            MessageBoxW(hwnd, L"Chave de API, Provedor, Modelo e Otimizacao Periodica salvos com sucesso!", L"Opit-Core IA", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        if (id == ID_AI_BTN_ANALYZE) {
            // Sincroniza campos visiveis antes de enviar a consulta
            wchar_t kBuf[512]{};
            GetWindowTextW(s_editAiKey, kBuf, 512);
            int pSel = (int)SendMessageW(s_comboAiProv, CB_GETCURSEL, 0, 0);
            wchar_t epBuf[512]{};
            GetWindowTextW(s_editAiEndpoint, epBuf, 512);
            wchar_t modBuf[256]{};
            GetWindowTextW(s_comboAiModel, modBuf, 256);

            auto& ai = AiAssistantManager::Instance();
            ai.SetProvider(static_cast<AiProvider>(pSel));
            ai.SetApiKey(StringUtils::ToUtf8(kBuf));
            ai.SetCustomEndpoint(StringUtils::ToUtf8(epBuf));
            std::string modStr = StringUtils::ToUtf8(modBuf);
            if (!modStr.empty()) {
                ai.SetSelectedModel(modStr);
            }

            if (!ai.HasApiKey() && ai.GetProvider() != AiProvider::Custom) {
                MessageBoxW(hwnd, L"Por favor insira a sua Chave de API antes de gerar o diagnostico.", L"Opit-Core IA", MB_OK | MB_ICONWARNING);
                return 0;
            }

            SetWindowTextW(s_lblAiStatus, L"Status: Coletando telemetria em tempo real e consultando a IA...");
            SetWindowTextW(s_editAiOutput, L"Analisando telemetria do hardware e gerando diagnostico inteligente... Por favor aguarde.");
            EnableWindow(s_btnAiAnalyze, FALSE);

            std::thread([hwnd]() {
                SystemMetricsSnapshot snap = CoreRuntime::Instance().GetMetricsSnapshot();
                if (snap.ramTotalMB == 0) {
                    MEMORYSTATUSEX mem{};
                    mem.dwLength = sizeof(mem);
                    if (GlobalMemoryStatusEx(&mem)) {
                        snap.ramTotalMB = (uint32_t)(mem.ullTotalPhys / (1024 * 1024));
                        snap.ramUsedMB = (uint32_t)((mem.ullTotalPhys - mem.ullAvailPhys) / (1024 * 1024));
                        snap.ramUsagePercent = (float)mem.dwMemoryLoad;
                    }
                }

                std::string report, err;
                bool ok = AiAssistantManager::Instance().GenerateSystemOptimizationReport(snap, report, err);

                std::wstring outText = ok ? FormatAiResponseForDisplay(report) : (L"Falha na consulta da IA:\r\n\r\n" + StringUtils::ToWide(err));
                std::wstring statusText = ok ? L"Status: Diagnostico e recomendacoes concluidos com sucesso!" : (L"Status: " + StringUtils::ToWide(err));

                SetWindowTextW(s_editAiOutput, outText.c_str());
                SetWindowTextW(s_lblAiStatus, statusText.c_str());
                EnableWindow(s_btnAiAnalyze, TRUE);
                EnableWindow(s_btnAiApplyAction, AiAssistantManager::Instance().HasPendingAiAction() ? TRUE : FALSE);
                EnableWindow(s_btnAiRollback, AiAssistantManager::Instance().CanRollbackAiAction() ? TRUE : FALSE);
                UpdateAiAskUI(hwnd);
            }).detach();
            return 0;
        }

        if (id == ID_AI_BTN_ASK) {
            wchar_t kBuf[512]{};
            GetWindowTextW(s_editAiKey, kBuf, 512);
            int pSel = (int)SendMessageW(s_comboAiProv, CB_GETCURSEL, 0, 0);
            wchar_t epBuf[512]{};
            GetWindowTextW(s_editAiEndpoint, epBuf, 512);
            wchar_t modBuf[256]{};
            GetWindowTextW(s_comboAiModel, modBuf, 256);

            auto& ai = AiAssistantManager::Instance();
            ai.SetProvider(static_cast<AiProvider>(pSel));
            ai.SetApiKey(StringUtils::ToUtf8(kBuf));
            ai.SetCustomEndpoint(StringUtils::ToUtf8(epBuf));
            std::string modStr = StringUtils::ToUtf8(modBuf);
            if (!modStr.empty()) {
                ai.SetSelectedModel(modStr);
            }

            if (!ai.HasApiKey() && ai.GetProvider() != AiProvider::Custom) {
                MessageBoxW(hwnd, L"Por favor insira a sua Chave de API antes de enviar uma pergunta.", L"Opit-Core IA", MB_OK | MB_ICONWARNING);
                return 0;
            }

            wchar_t pBuf[1024]{};
            GetWindowTextW(s_editAiPrompt, pBuf, 1024);
            std::string promptStr = StringUtils::ToUtf8(pBuf);
            if (promptStr.empty()) {
                MessageBoxW(hwnd, L"Por favor digite uma pergunta antes de enviar.", L"Opit-Core IA", MB_OK | MB_ICONWARNING);
                return 0;
            }

            SetWindowTextW(s_lblAiStatus, L"Status: Aguardando resposta da IA...");
            SetWindowTextW(s_editAiOutput, L"Processando pergunta com a IA selecionada... Por favor aguarde.");
            EnableWindow(s_btnAiAsk, FALSE);

            std::thread([hwnd, promptStr]() {
                std::string response, err;
                bool ok = AiAssistantManager::Instance().Query(promptStr, response, err);

                std::wstring outText = ok ? FormatAiResponseForDisplay(response) : (L"Erro ao consultar a IA:\r\n\r\n" + StringUtils::ToWide(err));
                std::wstring statusText = ok ? L"Status: Resposta recebida com sucesso." : (L"Status: " + StringUtils::ToWide(err));

                SetWindowTextW(s_editAiOutput, outText.c_str());
                SetWindowTextW(s_lblAiStatus, statusText.c_str());
                EnableWindow(s_btnAiAsk, TRUE);
                EnableWindow(s_btnAiApplyAction, AiAssistantManager::Instance().HasPendingAiAction() ? TRUE : FALSE);
                EnableWindow(s_btnAiRollback, AiAssistantManager::Instance().CanRollbackAiAction() ? TRUE : FALSE);
                UpdateAiAskUI(hwnd);
            }).detach();
            return 0;
        }

        if (id == ID_AI_BTN_APPLY_ACTION) {
            std::string feedback;
            bool ok = AiAssistantManager::Instance().ExecuteAiAction(feedback);
            if (ok) {
                SetWindowTextW(s_lblAiStatus, L"Status: Otimizacoes da IA aplicadas com sucesso!");
                MessageBoxW(hwnd, StringUtils::ToWide(feedback).c_str(), L"Opit-Core IA - Otimizacao Aplicada", MB_OK | MB_ICONINFORMATION);
            } else {
                SetWindowTextW(s_lblAiStatus, (L"Status: " + StringUtils::ToWide(feedback)).c_str());
                MessageBoxW(hwnd, (L"Nao foi possivel aplicar as alteracoes:\r\n" + StringUtils::ToWide(feedback)).c_str(), L"Opit-Core IA", MB_OK | MB_ICONWARNING);
            }
            EnableWindow(s_btnAiApplyAction, AiAssistantManager::Instance().HasPendingAiAction() ? TRUE : FALSE);
            EnableWindow(s_btnAiRollback, AiAssistantManager::Instance().CanRollbackAiAction() ? TRUE : FALSE);
            return 0;
        }

        if (id == ID_AI_BTN_ROLLBACK) {
            std::string feedback;
            bool ok = AiAssistantManager::Instance().RollbackLastAiAction(feedback);
            if (ok) {
                SetWindowTextW(s_lblAiStatus, L"Status: Configuracoes anteriores restauradas com sucesso.");
                MessageBoxW(hwnd, StringUtils::ToWide(feedback).c_str(), L"Opit-Core IA - Reversao de Configuracoes", MB_OK | MB_ICONINFORMATION);
            } else {
                SetWindowTextW(s_lblAiStatus, (L"Status: " + StringUtils::ToWide(feedback)).c_str());
                MessageBoxW(hwnd, (L"Falha ao reverter alteracoes:\r\n" + StringUtils::ToWide(feedback)).c_str(), L"Opit-Core IA", MB_OK | MB_ICONWARNING);
            }
            EnableWindow(s_btnAiRollback, AiAssistantManager::Instance().CanRollbackAiAction() ? TRUE : FALSE);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool AdvancedModuleDialogs::ShowAiAssistantDialog(HWND hParent) {
    HWND dlg = CreateModalWindow(hParent, L"Opit-Core AI Copilot / Assistente de Otimizacao Multi-IA (BYOK)", 750, 720, AiAssistantWndProc, 104, L"opit_ai.ico");
    RunModalLoop(dlg, hParent);
    return true;
}

} // namespace opitcore
