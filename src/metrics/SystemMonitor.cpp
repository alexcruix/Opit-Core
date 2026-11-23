// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "SystemMonitor.h"
#include "core/Logger.h"
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <wbemidl.h>
#include <comdef.h>
#endif
#include <sstream>
#include <string>

#ifdef _WIN32
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef _WIN32
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#endif

namespace opitcore {

#ifdef _WIN32
namespace {
// DwmGetCompositionTimingInfo so e exportada pelo dwmapi a partir do Windows 8. Importa-la
// duro faria o Windows 7 recusar o executavel inteiro ("entry point not found") por causa de
// uma metrica opcional: resolvida em tempo de execucao, a metrica e que fica indisponivel.
HRESULT QueryCompositionTiming(HWND wnd, DWM_TIMING_INFO* ti) {
    using Fn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
    static Fn fn = []() -> Fn {
        HMODULE h = LoadLibraryW(L"dwmapi.dll");
        return h ? reinterpret_cast<Fn>(GetProcAddress(h, "DwmGetCompositionTimingInfo")) : nullptr;
    }();
    if (!fn) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    return fn(wnd, ti);
}
} // namespace
#endif

SystemMonitor::SystemMonitor() = default;

SystemMonitor::~SystemMonitor() = default;

bool SystemMonitor::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cpuMon.Initialize();
    return true;
}

double SystemMonitor::QueryAcpiTemperature(bool& outHasSensor) {
    outHasSensor = false;
    if (m_acpiUnsupported) {
        return 0.0;
    }
#ifdef _WIN32
    double tempC = 0.0;

    // Inicializar COM para a chamada local segura se ainda não estiver inicializado na thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hr);

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = nullptr;
        // Tentar conectar ao namespace root\wmi
        BSTR bstrNamespace = SysAllocString(L"ROOT\\WMI");
        hr = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
        SysFreeString(bstrNamespace);

        if (SUCCEEDED(hr) && pSvc) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

            BSTR bstrWql = SysAllocString(L"WQL");
            BSTR bstrQuery = SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
            IEnumWbemClassObject* pEnumerator = nullptr;
            hr = pSvc->ExecQuery(bstrWql, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator);
            SysFreeString(bstrWql);
            SysFreeString(bstrQuery);

            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject* pclsObj = nullptr;
                ULONG uReturn = 0;
                if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    if (SUCCEEDED(pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0))) {
                        if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                            // CurrentTemperature é em décimos de Kelvin (2732 = 0°C)
                            long raw = (vtProp.vt == VT_I4) ? vtProp.lVal : (long)vtProp.ulVal;
                            tempC = (raw - 2732) / 10.0;
                            if (tempC > 0.0 && tempC < 130.0) {
                                outHasSensor = true;
                            }
                        }
                        VariantClear(&vtProp);
                    }
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
            pSvc->Release();
        }
        pLoc->Release();
    }

    if (shouldUninit) {
        CoUninitialize();
    }

    if (!outHasSensor) {
        // Evita inundar o WMI-Activity com 0x8004100C (WBEM_E_NOT_SUPPORTED) a cada segundo em placas sem zona ACPI
        m_acpiUnsupported = true;
    }

    return tempC;
#else
    // Gemelo do MSAcpi_ThermalZoneTemperature: os sensores hwmon que o kernel expoe em
    // sysfs (coretemp, k10temp, zenpower, cpu_thermal, acpitz). O valor ja esta em graus
    // por 1000, nao em decimos de Kelvin. A raiz e configuravel por OPITCORE_HWMON_ROOT
    // para se poder exercitar a leitura com uma arvore falsa na maquina que nao mostra
    // hwmon nenhum (e o caso do WSL2, onde este ramo devolve sensor indisponivel).
    const char* rootEnv = std::getenv("OPITCORE_HWMON_ROOT");
    const std::string root = (rootEnv && *rootEnv) ? std::string(rootEnv) : std::string("/sys/class/hwmon");
    DIR* dir = opendir(root.c_str());
    if (!dir) {
        m_acpiUnsupported = true;
        return 0.0;
    }

    double bestC = 0.0;
    std::string bestLabel;
    for (const dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "hwmon", 5) != 0) {
            continue;
        }
        const std::string dev = root + "/" + entry->d_name;
        std::string name;
        {
            std::ifstream nameFile(dev + "/name");
            std::getline(nameFile, name);
        }
        if (name != "coretemp" && name != "k10temp" && name != "zenpower" &&
            name != "cpu_thermal" && name != "acpitz") {
            continue;
        }
        for (int canal = 1; canal <= 16; ++canal) {
            std::ifstream tempFile(dev + "/temp" + std::to_string(canal) + "_input");
            long long milliC = 0;
            if (!(tempFile >> milliC) || milliC <= 0) {
                continue;
            }
            const double t = static_cast<double>(milliC) / 1000.0;
            // mesma faixa de plausibilidade do ramo Windows: fora dela o sensor nao e confiavel
            if (t >= 130.0) {
                continue;
            }
            if (t > bestC) {
                bestC = t;
                bestLabel = "sysfs hwmon " + name + " (" + entry->d_name + " temp" + std::to_string(canal) + ")";
            }
        }
    }
    closedir(dir);

    if (bestC <= 0.0) {
        m_acpiUnsupported = true;
        return 0.0;
    }
    outHasSensor = true;
    m_tempSensorLabel = bestLabel;
    return bestC;
#endif
}

void SystemMonitor::SetFrameTimingWindow(NativeWindow hwnd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameWnd = hwnd;
}

SystemMetricsSnapshot SystemMonitor::Sample() {
    std::lock_guard<std::mutex> lock(m_mutex);
    SystemMetricsSnapshot snap;

    // CPU Usage
    CpuMetrics cm = m_cpuMon.Sample();
    snap.cpuUsagePercent = cm.totalUsagePercent;

    // RAM Status
#ifdef _WIN32
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        snap.ramUsagePercent = (double)ms.dwMemoryLoad;
        snap.ramTotalMB = ms.ullTotalPhys / (1024 * 1024);
        snap.ramUsedMB = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024);
    }
#else
    // Gemelo do GlobalMemoryStatusEx: MemTotal e MemAvailable do /proc/meminfo, ambos em kB.
    // MemAvailable (nao MemFree) e o que o kernel da a uma aplicacao sem paging, por isso e
    // o paralelo correto do ullAvailPhys; a percentagem usa os mesmos dois numeros, tal como
    // o dwMemoryLoad do Windows e uma fracao de uso.
    {
        std::ifstream mf("/proc/meminfo");
        std::string line;
        uint64_t totalKb = 0;
        uint64_t availKb = 0;
        while (std::getline(mf, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                totalKb = std::strtoull(line.c_str() + 9, nullptr, 10);
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                availKb = std::strtoull(line.c_str() + 13, nullptr, 10);
            }
            if (totalKb != 0 && availKb != 0) {
                break;
            }
        }
        if (totalKb != 0) {
            const uint64_t usedKb = (availKb <= totalKb) ? (totalKb - availKb) : 0;
            snap.ramTotalMB = totalKb / 1024ULL;
            snap.ramUsedMB = usedKb / 1024ULL;
            snap.ramUsagePercent = 100.0 * static_cast<double>(usedKb) / static_cast<double>(totalKb);
        }
    }
#endif

    // Thermal Temperature
    bool hasSensor = false;
    snap.cpuTemperatureC = QueryAcpiTemperature(hasSensor);
    snap.hasTempSensor = hasSensor;
    if (hasSensor) {
        // A origem e declarada por quem realmente consultou o sensor, nao por suposicao a posteriori
#ifdef _WIN32
        snap.tempSensorSource = "WMI root\\wmi MSAcpi_ThermalZoneTemperature";
#else
        snap.tempSensorSource = m_tempSensorLabel;
#endif
    }

#ifdef _WIN32
    // Tempo real por quadro via estatisticas do compositor DWM (cFramesDisplayed: quadros
    // unicos efetivamente exibidos). So com janela registrada e com novos quadros desde a
    // amostra anterior; caso contrario, indisponivel. Diagnostico one-shot distingue
    // falha da API de contador estatico.
    if (m_frameWnd) {
        DWM_TIMING_INFO ti{};
        ti.cbSize = sizeof(ti);
        HRESULT hr = QueryCompositionTiming(m_frameWnd, &ti);
        if (SUCCEEDED(hr)) {
            ULONGLONG nowTick = GetTickCount64();
            if (m_lastFramesDisplayed > 0 && ti.cFramesDisplayed >= m_lastFramesDisplayed &&
                nowTick > m_lastFrameSampleTickMs) {
                double elapsedS = static_cast<double>(nowTick - m_lastFrameSampleTickMs) / 1000.0;
                ULONGLONG displayed = ti.cFramesDisplayed - m_lastFramesDisplayed;
                if (elapsedS >= 0.2 && displayed > 0) {
                    double fps = static_cast<double>(displayed) / elapsedS;
                    snap.frameTimeMs = 1000.0 / fps;
                    snap.hasFrameTime = true;
                }
            }
            if (m_lastFramesDisplayed > 0 && ti.cFramesDisplayed == m_lastFramesDisplayed) {
                ++m_frameStaticSamples;
                if (m_frameDiagLogged < 1 && m_frameStaticSamples >= 4) {
                    m_frameDiagLogged = 1;
                    OPIT_LOG_WARN("SystemMonitor: DWM cFramesDisplayed estatico em " +
                                  std::to_string(m_frameStaticSamples) +
                                  " amostras (valor=" + std::to_string(ti.cFramesDisplayed) +
                                  "): sem recomposicao ou contador sem avanco neste build do Windows.");
                }
            } else {
                m_frameStaticSamples = 0;
            }
            m_lastFramesDisplayed = ti.cFramesDisplayed;
            m_lastFrameSampleTickMs = nowTick;
        } else if (m_frameDiagLogged < 2) {
            m_frameDiagLogged = 2;
            std::ostringstream hex;
            hex << std::hex << hr;
            OPIT_LOG_WARN("SystemMonitor: DwmGetCompositionTimingInfo falhou, hr=0x" + hex.str() +
                          " — metrica de quadro indisponivel neste build.");
        }
    }
#endif

    return snap;
}

} // namespace opitcore
