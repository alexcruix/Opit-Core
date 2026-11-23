// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"
#include "CpuMonitor.h"
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

namespace opitcore {

// Alias da janela nativa da metrica de quadro do DWM. O DWM so existe em Windows; nos
// outros sistemas o identificador e opaco e a metrica permanece indisponivel.
#ifdef _WIN32
using NativeWindow = HWND;
#else
using NativeWindow = void*;
#endif

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    bool Initialize();
    SystemMetricsSnapshot Sample();

    // Janela topo-nivel usada pelo DWM para medir tempo real por quadro; sem ela,
    // frameTimeMs permanece indisponivel (hasFrameTime=false).
    void SetFrameTimingWindow(NativeWindow hwnd);

private:
    double QueryAcpiTemperature(bool& outHasSensor);

    CpuMonitor m_cpuMon;
    std::mutex m_mutex;
    bool m_wmiInitialized = false;
    bool m_acpiUnsupported = false;
    // Ramo nao-Windows: nome do sensor hwmon que realmente deu a leitura, para o
    // tempSensorSource declarar a origem em vez de um rotulo inventado a posteriori.
    std::string m_tempSensorLabel;

    NativeWindow m_frameWnd = nullptr;
    uint64_t m_lastFramesDisplayed = 0;
    uint64_t m_lastFrameSampleTickMs = 0;
    uint64_t m_frameStaticSamples = 0;
    int m_frameDiagLogged = 0;
};

} // namespace opitcore
