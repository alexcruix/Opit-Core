#pragma once
#include "utils/Types.h"
#include "CpuMonitor.h"
#include <mutex>
#include <windows.h>

namespace opitcore {

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    bool Initialize();
    SystemMetricsSnapshot Sample();

    // Janela topo-nivel usada pelo DWM para medir tempo real por quadro; sem ela,
    // frameTimeMs permanece indisponivel (hasFrameTime=false).
    void SetFrameTimingWindow(HWND hwnd);

private:
    double QueryAcpiTemperature(bool& outHasSensor);

    CpuMonitor m_cpuMon;
    std::mutex m_mutex;
    bool m_wmiInitialized = false;
    bool m_acpiUnsupported = false;

    HWND m_frameWnd = nullptr;
    ULONGLONG m_lastFramesDisplayed = 0;
    ULONGLONG m_lastFrameSampleTickMs = 0;
    ULONGLONG m_frameStaticSamples = 0;
    int m_frameDiagLogged = 0;
};

} // namespace opitcore
