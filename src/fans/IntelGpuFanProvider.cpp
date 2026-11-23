// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "IntelGpuFanProvider.h"
#include "platform/windows/GpuDetector.h"
#include "utils/StringUtils.h"
#include <igcl_api.h>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace opitcore {

static std::string ToHexStr(uint32_t val) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << val;
    return ss.str();
}

IntelGpuFanProvider::IntelGpuFanProvider() = default;

IntelGpuFanProvider::~IntelGpuFanProvider() {
    Shutdown();
}

bool IntelGpuFanProvider::Initialize() {
    if (m_initialized) return true;

    // 1. Tenta carregar a biblioteca nativa do driver Intel Graphics (ControlLib.dll / ControlLib64.dll)
    m_hControlLib = LoadLibraryW(L"ControlLib.dll");
    if (!m_hControlLib) {
        m_hControlLib = LoadLibraryW(L"ControlLib64.dll");
    }

    if (m_hControlLib) {
        auto pfnInit = (ctl_pfnInit_t)GetProcAddress(m_hControlLib, "ctlInit");
        if (pfnInit) {
            ctl_init_args_t initArgs{};
            initArgs.Size = sizeof(ctl_init_args_t);
            initArgs.Version = 0;
            initArgs.AppVersion = CTL_MAKE_VERSION(CTL_IMPL_MAJOR_VERSION, CTL_IMPL_MINOR_VERSION);

            ctl_api_handle_t hApi = nullptr;
            ctl_result_t res = pfnInit(&initArgs, &hApi);
            if (res == CTL_RESULT_SUCCESS && hApi) {
                m_hApiHandle = hApi;

                auto pfnEnumDevices = (ctl_pfnEnumerateDevices_t)GetProcAddress(m_hControlLib, "ctlEnumerateDevices");
                auto pfnGetDeviceProps = (ctl_pfnGetDeviceProperties_t)GetProcAddress(m_hControlLib, "ctlGetDeviceProperties");
                auto pfnEnumFans = (ctl_pfnEnumFans_t)GetProcAddress(m_hControlLib, "ctlEnumFans");
                auto pfnFanGetProps = (ctl_pfnFanGetProperties_t)GetProcAddress(m_hControlLib, "ctlFanGetProperties");
                auto pfnEnumTemp = (ctl_pfnEnumTemperatureSensors_t)GetProcAddress(m_hControlLib, "ctlEnumTemperatureSensors");

                if (pfnEnumDevices) {
                    uint32_t deviceCount = 0;
                    if (pfnEnumDevices(hApi, &deviceCount, nullptr) == CTL_RESULT_SUCCESS && deviceCount > 0) {
                        std::vector<ctl_device_adapter_handle_t> devices(deviceCount);
                        if (pfnEnumDevices(hApi, &deviceCount, devices.data()) == CTL_RESULT_SUCCESS) {
                            for (uint32_t i = 0; i < deviceCount; ++i) {
                                ctl_device_adapter_handle_t hDev = devices[i];
                                std::string devName = "GPU Intel Arc / Iris Xe #" + std::to_string(i + 1);

                                if (pfnGetDeviceProps) {
                                    ctl_device_adapter_properties_t props{};
                                    props.Size = sizeof(ctl_device_adapter_properties_t);
                                    props.Version = 0;
                                    if (pfnGetDeviceProps(hDev, &props) == CTL_RESULT_SUCCESS) {
                                        devName = "Intel GPU (PCI: 0x" + ToHexStr(props.pci_device_id) + ")";
                                    }
                                }

                                // Enumera sensores térmicos para o adaptador
                                void* hTempSensor = nullptr;
                                if (pfnEnumTemp) {
                                    uint32_t tempCount = 0;
                                    if (pfnEnumTemp(hDev, &tempCount, nullptr) == CTL_RESULT_SUCCESS && tempCount > 0) {
                                        std::vector<ctl_temp_handle_t> tempHandles(tempCount);
                                        if (pfnEnumTemp(hDev, &tempCount, tempHandles.data()) == CTL_RESULT_SUCCESS) {
                                            hTempSensor = tempHandles[0];
                                        }
                                    }
                                }

                                // Enumera ventoinhas
                                if (pfnEnumFans) {
                                    uint32_t fanCount = 0;
                                    if (pfnEnumFans(hDev, &fanCount, nullptr) == CTL_RESULT_SUCCESS && fanCount > 0) {
                                        std::vector<ctl_fan_handle_t> fanHandles(fanCount);
                                        if (pfnEnumFans(hDev, &fanCount, fanHandles.data()) == CTL_RESULT_SUCCESS) {
                                            for (uint32_t f = 0; f < fanCount; ++f) {
                                                IntelFanHandleInfo info{};
                                                info.deviceId = "gpu:intel:igcl:" + std::to_string(i) + ":" + std::to_string(f);
                                                info.name = devName + " - Ventoinha #" + std::to_string(f + 1);
                                                info.hAdapter = hDev;
                                                info.hFan = fanHandles[f];
                                                info.hTempSensor = hTempSensor;
                                                info.canReadTemp = (hTempSensor != nullptr);

                                                if (pfnFanGetProps) {
                                                    ctl_fan_properties_t fProps{};
                                                    fProps.Size = sizeof(ctl_fan_properties_t);
                                                    fProps.Version = 0;
                                                    if (pfnFanGetProps(fanHandles[f], &fProps) == CTL_RESULT_SUCCESS) {
                                                        info.canControl = fProps.canControl;
                                                        info.canReadRpm = (fProps.maxRPM > 0);
                                                        info.maxRpm = fProps.maxRPM;
                                                        info.supportedUnits = fProps.supportedUnits;
                                                    }
                                                }
                                                m_fans.push_back(info);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Se IGCL não encontrou fans de GPU dedicada Intel (ou GPU integrada Intel sem ventoinha exclusiva),
    // detecta via GpuDetector para exibir relatório fidedigno
    if (m_fans.empty()) {
        auto gpus = GpuDetector::DetectGpus();
        for (const auto& g : gpus) {
            if (g.vendorId == 0x8086) {
                m_fallbackIntelGpuNames.push_back(g.description.empty() ? "Intel HD/UHD/Iris Graphics" : g.description);
            }
        }
    }

    m_initialized = true;
    return true;
}

void IntelGpuFanProvider::Shutdown() {
    if (m_hControlLib && m_hApiHandle) {
        auto pfnClose = (ctl_pfnClose_t)GetProcAddress(m_hControlLib, "ctlClose");
        if (pfnClose) {
            pfnClose((ctl_api_handle_t)m_hApiHandle);
        }
        m_hApiHandle = nullptr;
    }

    if (m_hControlLib) {
        FreeLibrary(m_hControlLib);
        m_hControlLib = nullptr;
    }

    m_fans.clear();
    m_fallbackIntelGpuNames.clear();
    m_initialized = false;
}

std::vector<FanDeviceInfo> IntelGpuFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized) return devices;

    // Dispositivos via Intel IGCL nativo
    for (const auto& f : m_fans) {
        FanDeviceInfo dev;
        dev.id = f.deviceId;
        dev.name = f.name;
        dev.vendor = "Intel Corporation";
        dev.deviceType = FanDeviceType::GpuIntel;

        dev.capabilities.canReadTemperature = f.canReadTemp;
        dev.capabilities.canReadRpm = f.canReadRpm;
        dev.capabilities.canControlSpeed = f.canControl;
        dev.capabilities.canSetCurve = f.canControl;
        dev.capabilities.minSpeedPercent = 20;
        dev.capabilities.maxSpeedPercent = 100;
        dev.activeMode = FanControlMode::BiosFirmwareAuto;

        dev.latestReading.sensorSource = "Intel Control Library (IGCL)";
        dev.latestReading.hasTemperature = f.canReadTemp;
        dev.latestReading.hasRpm = f.canReadRpm;
        devices.push_back(dev);
    }

    // Dispositivos detectados sem ventoinha dedicada (GPU integrada)
    for (size_t i = 0; i < m_fallbackIntelGpuNames.size(); ++i) {
        FanDeviceInfo dev;
        dev.id = "gpu:intel:igpu:" + std::to_string(i);
        dev.name = m_fallbackIntelGpuNames[i];
        dev.vendor = "Intel Corporation";
        dev.deviceType = FanDeviceType::GpuIntel;

        dev.capabilities.canReadTemperature = false;
        dev.capabilities.canReadRpm = false;
        dev.capabilities.canControlSpeed = false;
        dev.capabilities.canSetCurve = false;
        dev.activeMode = FanControlMode::BiosFirmwareAuto;

        dev.latestReading.sensorSource = "Intel Graphics (Integrada/Compartilhada)";
        dev.latestReading.hasTemperature = false;
        dev.latestReading.hasRpm = false;
        dev.latestReading.lastErrorMessage = "GPU integrada compartilha arrefecimento da CPU. Tacometro exclusivo indisponivel.";

        devices.push_back(dev);
    }

    return devices;
}

FanResult<FanReading> IntelGpuFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Provedor Intel IGCL nao inicializado.";
        return res;
    }

    for (const auto& f : m_fans) {
        if (f.deviceId == deviceId) {
            FanReading r;
            r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            r.sensorSource = "Intel IGCL";

            // Temperatura
            if (f.hTempSensor && m_hControlLib) {
                auto pfnTempState = (ctl_pfnTemperatureGetState_t)GetProcAddress(m_hControlLib, "ctlTemperatureGetState");
                if (pfnTempState) {
                    double tempVal = 0.0;
                    if (pfnTempState((ctl_temp_handle_t)f.hTempSensor, &tempVal) == CTL_RESULT_SUCCESS) {
                        r.temperatureC = tempVal;
                        r.hasTemperature = (tempVal > 0.0 && tempVal < 125.0);
                    }
                }
            }

            // RPM / Percentual
            if (f.hFan && m_hControlLib) {
                auto pfnFanState = (ctl_pfnFanGetState_t)GetProcAddress(m_hControlLib, "ctlFanGetState");
                if (pfnFanState) {
                    int32_t speedRpm = 0;
                    if (pfnFanState((ctl_fan_handle_t)f.hFan, CTL_FAN_SPEED_UNITS_RPM, &speedRpm) == CTL_RESULT_SUCCESS && speedRpm >= 0) {
                        r.rpm = static_cast<uint32_t>(speedRpm);
                        r.hasRpm = true;
                    }

                    int32_t speedPct = 0;
                    if (pfnFanState((ctl_fan_handle_t)f.hFan, CTL_FAN_SPEED_UNITS_PERCENT, &speedPct) == CTL_RESULT_SUCCESS && speedPct >= 0) {
                        r.reportedSpeedPercent = static_cast<uint32_t>(speedPct);
                        r.hasReportedSpeedPercent = true;
                    }
                }
            }

            res.code = FanStatusCode::Ok;
            res.value = r;
            return res;
        }
    }

    res.code = FanStatusCode::Unsupported;
    res.message = "GPU Intel integrada sem sensor de ventoinha exclusivo.";
    return res;
}

FanResult<bool> IntelGpuFanProvider::SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    res.value = false;

    for (const auto& f : m_fans) {
        if (f.deviceId == deviceId) {
            if (!f.canControl || !m_hControlLib || !f.hFan) {
                res.code = FanStatusCode::Unsupported;
                res.message = "Controle manual de ventoinha nao suportado neste hardware Intel.";
                return res;
            }

            auto pfnSetSpeed = (ctl_pfnFanSetFixedSpeedMode_t)GetProcAddress(m_hControlLib, "ctlFanSetFixedSpeedMode");
            if (!pfnSetSpeed) {
                res.code = FanStatusCode::Unsupported;
                res.message = "Funcao ctlFanSetFixedSpeedMode indisponivel na DLL do driver Intel.";
                return res;
            }

            ctl_fan_speed_t spd{};
            spd.Size = sizeof(ctl_fan_speed_t);
            spd.Version = 0;
            spd.speed = static_cast<int32_t>(std::clamp(speedPercent, 20u, 100u));
            spd.units = CTL_FAN_SPEED_UNITS_PERCENT;

            ctl_result_t ctlRes = pfnSetSpeed((ctl_fan_handle_t)f.hFan, &spd);
            if (ctlRes == CTL_RESULT_SUCCESS) {
                res.code = FanStatusCode::Ok;
                res.value = true;
                res.message = "Velocidade de " + std::to_string(spd.speed) + "% aplicada via Intel IGCL.";
            } else {
                res.code = FanStatusCode::CommandRejected;
                res.message = "Driver Intel rejeitou a configuracao fixa (Erro: 0x" + ToHexStr(ctlRes) + ").";
            }
            return res;
        }
    }

    res.code = FanStatusCode::Unsupported;
    res.message = "Dispositivo Intel nao compativel com controle manual.";
    return res;
}

FanResult<bool> IntelGpuFanProvider::RestoreDefaultControl(const std::string& deviceId) {
    FanResult<bool> res;
    res.value = false;

    for (const auto& f : m_fans) {
        if (f.deviceId == deviceId) {
            if (m_hControlLib && f.hFan) {
                auto pfnRestore = (ctl_pfnFanSetDefaultMode_t)GetProcAddress(m_hControlLib, "ctlFanSetDefaultMode");
                if (pfnRestore) {
                    pfnRestore((ctl_fan_handle_t)f.hFan);
                }
            }
            res.code = FanStatusCode::Ok;
            res.value = true;
            res.message = "Controle automatico de ventilacao restaurado para a VBIOS Intel.";
            return res;
        }
    }

    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Modo de fabrica mantido pelo firmware Intel.";
    return res;
}

} // namespace opitcore
