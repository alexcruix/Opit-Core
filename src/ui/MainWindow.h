#pragma once
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "Theme.h"

namespace opitcore {

class MainWindow {
public:
    static MainWindow& Instance();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int RunMessageLoop();
    void Show(bool show);

    // Balao de aviso do tray para os eventos que o motor observa (NotificationManager).
    void ShowTrayBalloon(const std::wstring& title, const std::wstring& message);

private:
    MainWindow();
    ~MainWindow();

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void CreateSidebar(HWND hwnd);
    void LayoutControls(int width, int height);

    void CreateTabControls(HWND hwnd);
    void SwitchTab(int tabIndex);
    void UpdatePeriodicData();
    void RefreshProcessList();
    void RefreshServicesList(const std::string& filter = "");
    void RefreshGameList();
    void RefreshLogs();
    void RefreshMonitoringHistory();
    void PopulatePowerSchemes();
    void PopulateHardwareInfo();

    enum class TrayVisualMode {
        Balanced = 0,
        Eco = 1,
        Performance = 2,
        AI = 3
    };

    void UpdateTrayVisual(TrayVisualMode mode, const std::wstring& customTooltip = L"");
    void LoadAppIcons();
    void FreeAppIcons();

    // Controles principais
    HWND m_hwnd = nullptr;
    HWND m_tab = nullptr;
    HWND m_sidebarHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    NOTIFYICONDATAW m_nid{};
    HICON m_hIconMain = nullptr;
    HICON m_hIconTrayBalanced = nullptr;
    HICON m_hIconTrayEco = nullptr;
    HICON m_hIconTrayPerf = nullptr;
    HICON m_hIconTrayAi = nullptr;
    TrayVisualMode m_currentTrayMode = TrayVisualMode::Balanced;
    bool m_visible = true;
    // Unica fonte de verdade para encerrar o loop de mensagens: uma WM_QUIT pode subir ate aqui
    // sem que a usuaria tenha pedido para sair (toda janela modal chama PostQuitMessage ao fechar),
    // e o motor so desliga quando um comando de saida da propria janela principal acende isto.
    bool m_quitRequested = false;
    int m_currentTab = 0;
    int m_hoveredTab = -1;

    // Recursos visuais GDI
    HFONT m_hFontNormal = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontMono = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    // Estatísticas de sessão e histórico em memória
    std::vector<double> m_cpuHistory;
    double m_minCpu = 100.0;
    double m_maxCpu = 0.0;
    double m_sumCpu = 0.0;
    int m_countCpu = 0;

    double m_minRam = 100.0;
    double m_maxRam = 0.0;
    double m_sumRam = 0.0;

    // ==========================================
    // Controles por aba:
    // ==========================================

    // Tab 0: Painel Geral
    HWND m_lblDashCpu = nullptr;
    HWND m_progDashCpu = nullptr;
    HWND m_lblDashRam = nullptr;
    HWND m_progDashRam = nullptr;
    HWND m_lblDashGpu = nullptr;
    HWND m_lblDashPower = nullptr;
    HWND m_lblDashProfile = nullptr;
    HWND m_lblDashTemp = nullptr;
    HWND m_lblDashGame = nullptr;
    HWND m_lblDashDocCpu = nullptr;
    HWND m_lblDashStatus = nullptr;
    HWND m_btnDashDeepEco = nullptr;
    HWND m_btnDashGameMode = nullptr;
    HWND m_lblDashCoordinator = nullptr;
    std::vector<HWND> m_tab0Controls;

    // Tab 1: Perfis
    HWND m_comboProfiles = nullptr;
    HWND m_lblProfileDesc = nullptr;
    HWND m_btnApplyProfile = nullptr;
    HWND m_lblProfileStatus = nullptr;
    std::vector<HWND> m_tab1Controls;

    // Tab 2: CPU
    HWND m_lblCpuInfo = nullptr;
    HWND m_lblCpuCores = nullptr;
    HWND m_lblCpuArch = nullptr;
    HWND m_lblCpuDocSpecs = nullptr;
    HWND m_lblCpuUsage = nullptr;
    HWND m_progCpuUsage = nullptr;
    HWND m_editCpuThrottle = nullptr;
    HWND m_btnApplyThrottle = nullptr;
    HWND m_btnCpuAdvanced = nullptr;
    HWND m_lblThrottleStatus = nullptr;
    std::vector<HWND> m_tab2Controls;

    // Tab 3: GPU
    HWND m_listGpu = nullptr;
    HWND m_lblGpuDocSpecs = nullptr;
    HWND m_btnGpuAdvanced = nullptr;
    std::vector<HWND> m_tab3Controls;

    // Tab 4: RAM
    HWND m_lblRamTotal = nullptr;
    HWND m_lblRamUsed = nullptr;
    HWND m_lblRamFree = nullptr;
    HWND m_lblRamLoad = nullptr;
    HWND m_progRamLoad = nullptr;
    HWND m_btnOptimizeRam = nullptr;
    HWND m_lblRamOptStatus = nullptr;
    HWND m_lblBoardInfo = nullptr;
    HWND m_lblBiosInfo = nullptr;
    HWND m_lblRamSticks = nullptr;
    std::vector<HWND> m_tab4Controls;

    // Tab 5: Energia
    HWND m_comboPowerSchemes = nullptr;
    HWND m_lblActivePowerScheme = nullptr;
    HWND m_lblBatteryStatus = nullptr;
    HWND m_btnSetPowerScheme = nullptr;
    HWND m_lblPowerStatus = nullptr;
    std::vector<HWND> m_tab5Controls;

    // Tab 6: Processos
    HWND m_listProcesses = nullptr;
    HWND m_btnRefreshProc = nullptr;
    HWND m_btnOpenServices = nullptr;
    HWND m_lblProcPriority = nullptr;
    HWND m_comboPriority = nullptr;
    HWND m_btnSetPriority = nullptr;
    HWND m_lblProcAffinity = nullptr;
    HWND m_comboAffinity = nullptr;
    HWND m_btnSetAffinity = nullptr;
    HWND m_btnKillProc = nullptr;
    HWND m_lblProcStatus = nullptr;
    std::vector<HWND> m_tab6Controls;

    // Tab 7: Serviços do Windows
    HWND m_editSvcSearch = nullptr;
    HWND m_btnRefreshSvcList = nullptr;
    HWND m_btnConfigSvcTriggers = nullptr;
    HWND m_listServices = nullptr;
    HWND m_lblSvcRulePrompt = nullptr;
    HWND m_comboSvcRule = nullptr;
    HWND m_btnApplySvcRule = nullptr;
    HWND m_btnApplyAllSvcRules = nullptr;
    HWND m_btnPauseSvcNow = nullptr;
    HWND m_btnResumeSvcNow = nullptr;
    HWND m_btnRestoreAllSvcs = nullptr;
    HWND m_lblSvcStatus = nullptr;
    std::vector<HWND> m_tab7Controls;

    // Tab 8: Jogos
    HWND m_listGames = nullptr;
    HWND m_editGameExe = nullptr;
    HWND m_btnAddGame = nullptr;
    HWND m_btnEditGame = nullptr;
    HWND m_btnRemoveGame = nullptr;
    HWND m_btnToggleGameMode = nullptr;
    HWND m_chkAutoGame = nullptr;
    HWND m_lblGameStatus = nullptr;
    std::vector<HWND> m_tab8Controls;

    // Tab 9: Monitoramento
    HWND m_lblMonRealtime = nullptr;
    HWND m_lblMonCpuStats = nullptr;
    HWND m_lblMonRamStats = nullptr;
    HWND m_lblMonTrend = nullptr;
    HWND m_listMonHistory = nullptr;
    HWND m_btnRefreshMonHistory = nullptr;
    HWND m_btnClearMonHistory = nullptr;
    HWND m_lblMonStatus = nullptr;
    std::vector<HWND> m_tab9Controls;

    // Tab 10: Térmica
    HWND m_lblThermalCurrent = nullptr;
    HWND m_lblThermalSensorStatus = nullptr;
    HWND m_editThermalLimit = nullptr;
    HWND m_chkThermalEnabled = nullptr;
    HWND m_btnApplyThermal = nullptr;
    HWND m_btnOpenFansGraph = nullptr;
    HWND m_btnOpenFansConfig = nullptr;
    HWND m_lblThermalActionStatus = nullptr;
    std::vector<HWND> m_tab10Controls;

    // Tab 11: Configurações Gerais
    HWND m_chkStartWindows = nullptr;
    HWND m_chkStartWindowsAdmin = nullptr;
    HWND m_chkStartMinimized = nullptr;
    HWND m_chkCloseToTray = nullptr;
    HWND m_chkDisableTelemetry = nullptr;
    HWND m_chkSmartGamingServices = nullptr;
    HWND m_comboInterval = nullptr;
    HWND m_btnSaveConfig = nullptr;
    HWND m_btnResetDefaults = nullptr;
    HWND m_lblConfigStatus = nullptr;
    std::vector<HWND> m_tab11Controls;

    // Tab 12: Diagnóstico & Logs
    HWND m_lblDiagElevated = nullptr;
    HWND m_lblDiagDb = nullptr;
    HWND m_listLogs = nullptr;
    HWND m_btnRefreshLogs = nullptr;
    HWND m_btnCopyDiag = nullptr;
    HWND m_lblDiagStatus = nullptr;
    std::vector<HWND> m_tab12Controls;

    // Tab 13: Sobre & Código Aberto
    HWND m_lblAboutTitle = nullptr;
    HWND m_lblAboutBody = nullptr;
    HWND m_lblPixHead = nullptr;
    HWND m_lblPixKey = nullptr;
    HWND m_btnCopyPix = nullptr;
    HWND m_lblAboutStatus = nullptr;
    std::vector<HWND> m_tab13Controls;

    std::vector<std::vector<HWND>*> m_allTabControls;
};

} // namespace opitcore
