#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "core/UnifiedSettingsModel.h"

namespace opitcore {

class UnifiedSettingsWindow {
public:
    static UnifiedSettingsWindow& Instance();

    bool ShowModal(HWND hParent);

private:
    UnifiedSettingsWindow();
    ~UnifiedSettingsWindow();

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HWND hwnd);
    void SyncModelToUI();
    void SyncUIToModel();
    void ShowAdvancedDialog(int moduleIndex);

    HWND m_hwnd = nullptr;
    HWND m_hParent = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_scrollY = 0;
    int m_contentHeight = 868;

    void UpdateScroll(int clientHeight);

    HFONT m_hFontNormal = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontDesc = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    // 1. Seção Ativação Global
    HWND m_chkEnableOrchestrator = nullptr;
    HWND m_chkSmartOrchestrator = nullptr;

    // 2. Seção de Módulos (Grid)
    // Coluna Esquerda
    HWND m_chkModPowerPlans = nullptr;
    HWND m_chkModCoreParking = nullptr;
    HWND m_chkModGpuControl = nullptr;
    HWND m_chkModThermalGuardian = nullptr;
    HWND m_chkModAdaptiveHardware = nullptr;
    HWND m_chkModWindowsServices = nullptr;
    HWND m_chkSubSmartServices = nullptr; // aninhado
    HWND m_chkModSystemStability = nullptr;
    HWND m_chkModPauseIndexingIdle = nullptr;
    HWND m_chkDisableTelemetry = nullptr;
    HWND m_chkSmartGamingServices = nullptr;
    HWND m_chkPauseIdleDevicesEco = nullptr;

    // Coluna Direita
    HWND m_chkModCpuMinMax = nullptr;
    HWND m_chkModProcessControl = nullptr;
    HWND m_chkModFanCurve = nullptr;
    HWND m_chkModDeepEco = nullptr;
    HWND m_chkModAppPriority = nullptr;

    // 3. Seção Nível por módulo
    struct ModuleRowControls {
        HWND lblName = nullptr;
        HWND comboLevel = nullptr;
        HWND btnAdvanced = nullptr;
        HWND chkAutoSmart = nullptr;
        HWND lblHint = nullptr;
    };
    std::vector<ModuleRowControls> m_moduleRows;

    // 5. Agrupamento svchost
    HWND m_comboSvchost = nullptr;
    HWND m_lblSvchostDesc = nullptr;

    // 6. Barra de Ações
    HWND m_btnOk = nullptr;
    HWND m_btnSave = nullptr;
    HWND m_btnCancel = nullptr;
};

} // namespace opitcore
