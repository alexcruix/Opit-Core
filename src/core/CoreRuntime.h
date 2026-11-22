#pragma once
#include "IModule.h"
#include "utils/Types.h"
#include "Config.h"
#include "metrics/SystemMonitor.h"
#include "power/PowerManager.h"
#include "protection/ThermalProtection.h"
#include "control/GameDetector.h"
#include "adaptive/AdaptiveEngine.h"
#include <vector>
#include <set>
#include <thread>
#include <atomic>
#include <mutex>

namespace opitcore {

class CoreRuntime {
public:
    static CoreRuntime& Instance();

    CoreRuntime();
    ~CoreRuntime();

    bool Initialize();
    void Start();
    void Stop();
    void Shutdown();

    // Aplicação e controle de perfis de desempenho e configurações unificadas
    bool ApplyProfile(Config::Profile profile);
    void ApplyUnifiedSettings();

    // Getters thread-safe de estado
    SystemMetricsSnapshot GetMetricsSnapshot();
    SystemHardwareInfo GetHardwareInfo();
    PowerManager& GetPowerManager() { return m_powerMgr; }
    GameDetector& GetGameDetector() { return m_gameDetector; }
    ThermalProtection& GetThermalProtection() { return m_thermalProtection; }

    // Janela topo-nivel para medicao real de tempo por quadro (DWM) dentro do SystemMonitor
    void SetFrameTimingWindow(HWND hwnd) { m_systemMonitor.SetFrameTimingWindow(hwnd); }

private:
    void WorkerLoop();
    void ApplyProfileSettings(Config::Profile profile);

    enum class State { Uninitialized, Initializing, Running, Stopping, Stopped };
    State m_state = State::Uninitialized;

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    mutable std::mutex m_mutex;

    // Subsistemas
    SystemMonitor m_systemMonitor;
    PowerManager m_powerMgr;
    ThermalProtection m_thermalProtection;
    GameDetector m_gameDetector;

    // Cache e estado
    SystemHardwareInfo m_hardwareInfo;
    SystemMetricsSnapshot m_latestSnapshot;
    Config::Profile m_preGameProfile = Config::Profile::Balanced;
    bool m_gameModeActive = false;
    std::set<std::string> m_lastActiveGames;
};

} // namespace opitcore
