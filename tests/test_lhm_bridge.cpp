// Integration test for opitcore_lhm_bridge.dll (C++/CLI bridge to LibreHardwareMonitor).
// Exercises the real runtime-loading path used by LibreHardwareMonitorFanProvider and
// prints the actual sensor values reported by the hardware. Zero sensors is not a
// failure (LibreHardwareMonitor exposes few sensors without administrator rights);
// failing to load the bridge, an ABI mismatch or an init failure is.

#include "fans/LibreHardwareMonitorFanProvider.h"
#include "fans/lhm_bridge/OpitLhmBridge.h"

#include <windows.h>
#include <cstdio>

using namespace opitcore;

static int g_failures = 0;
#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (cond) { std::printf("[PASS] %s\n", msg); }           \
        else { std::printf("[FAIL] %s\n", msg); ++g_failures; }  \
    } while (0)

int main() {
    // 1) Direct C ABI check (same DLL the provider loads, from the exe folder).
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    HMODULE mod = LoadLibraryExW((dir + OPIT_LHM_BRIDGE_DLL_NAME).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(mod != nullptr, "opitcore_lhm_bridge.dll loads from the executable folder");
    if (!mod) {
        std::printf("LoadLibrary error: %lu\n", GetLastError());
        return 1;
    }

    auto getAbi = reinterpret_cast<PFN_OpitLhm_GetAbiVersion>(GetProcAddress(mod, "OpitLhm_GetAbiVersion"));
    auto init = reinterpret_cast<PFN_OpitLhm_Init>(GetProcAddress(mod, "OpitLhm_Init"));
    auto refresh = reinterpret_cast<PFN_OpitLhm_Refresh>(GetProcAddress(mod, "OpitLhm_Refresh"));
    auto tempCount = reinterpret_cast<PFN_OpitLhm_GetCount>(GetProcAddress(mod, "OpitLhm_GetTemperatureCount"));
    auto fanCount = reinterpret_cast<PFN_OpitLhm_GetCount>(GetProcAddress(mod, "OpitLhm_GetFanCount"));
    auto getTemp = reinterpret_cast<PFN_OpitLhm_GetTemperature>(GetProcAddress(mod, "OpitLhm_GetTemperature"));
    auto getRpm = reinterpret_cast<PFN_OpitLhm_GetRpm>(GetProcAddress(mod, "OpitLhm_GetRpm"));
    auto shutdown = reinterpret_cast<PFN_OpitLhm_Shutdown>(GetProcAddress(mod, "OpitLhm_Shutdown"));
    CHECK(getAbi && init && refresh && tempCount && fanCount && getTemp && getRpm && shutdown,
          "all bridge exports resolved");
    if (g_failures) return 1;

    CHECK(getAbi() == OPIT_LHM_ABI_VERSION, "ABI version matches");

    // A sonda e o unico export chamavel quando a assembly nao carrega neste processo: os
    // restantes referenciam tipos do LibreHardwareMonitorLib e rebentam na JIT, antes de o
    // try/catch do proprio bridge existir. Sem este gate, um teste de 32 bits mata o processo.
    auto probe = reinterpret_cast<PFN_OpitLhm_IsLibraryLoadable>(
        GetProcAddress(mod, "OpitLhm_IsLibraryLoadable"));
    CHECK(probe != nullptr, "OpitLhm_IsLibraryLoadable resolved");
    if (g_failures) return 1;
    const bool lhmUsable = probe && probe() == 1;
    std::printf("LibreHardwareMonitorLib carregavel neste processo: %s\n", lhmUsable ? "sim" : "NAO");
    if (!lhmUsable) {
        LibreHardwareMonitorFanProvider provider;
        CHECK(!provider.Initialize(), "provider degrada sem carregar a assembly LHM");
        std::printf("%s (%d failure(s))\n", g_failures ? "FAILED" : "ALL PASSED (degradado)", g_failures);
        return g_failures ? 1 : 0;
    }

    CHECK(tempCount() == -1, "counts report -1 before init");
    CHECK(init() == 1, "OpitLhm_Init (CLR + LibreHardwareMonitorLib load, Computer.Open)");
    CHECK(refresh() == 1, "OpitLhm_Refresh");

    int32_t nTemp = tempCount();
    int32_t nFan = fanCount();
    CHECK(nTemp >= 0 && nFan >= 0, "sensor counts available after init");
    std::printf("Temperature sensors: %d, fan sensors: %d\n", nTemp, nFan);
    for (int32_t i = 0; i < nTemp; ++i) {
        double c = 0.0;
        if (getTemp(i, &c)) std::printf("  temp[%d] = %.1f C\n", i, c);
        else std::printf("  temp[%d] = (no value)\n", i);
    }
    for (int32_t i = 0; i < nFan; ++i) {
        int32_t rpm = 0;
        if (getRpm(i, &rpm)) std::printf("  fan[%d] = %d RPM\n", i, rpm);
        else std::printf("  fan[%d] = (no value)\n", i);
    }
    double dummy = 0.0;
    CHECK(getTemp(nTemp + 1000, &dummy) == 0, "out-of-range index returns failure");
    CHECK(getTemp(0, nullptr) == 0, "null output pointer returns failure");
    shutdown();
    CHECK(tempCount() == -1, "counts report -1 after shutdown");

    // 2) Native provider path (runtime loading inside opitcore_fans).
    {
        LibreHardwareMonitorFanProvider provider;
        CHECK(provider.Initialize(), "LibreHardwareMonitorFanProvider::Initialize");
        auto devices = provider.EnumerateDevices();
        CHECK(devices.size() == 1, "provider enumerates the LHM device");
        auto reading = provider.ReadTelemetry("LHM_0");
        CHECK(reading.code == FanStatusCode::Ok, "provider ReadTelemetry returns Ok");
        if (reading.code == FanStatusCode::Ok) {
            const FanReading& r = reading.value;
            std::printf("Provider reading: temp=%s rpm=%s\n",
                        r.hasTemperature ? std::to_string(r.temperatureC).c_str() : "n/a",
                        r.hasRpm ? std::to_string(r.rpm).c_str() : "n/a");
        }
        provider.Shutdown();
    }

    std::printf("%s (%d failure(s))\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
