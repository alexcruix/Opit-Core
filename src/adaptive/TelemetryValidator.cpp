// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "TelemetryValidator.h"
#ifdef _WIN32
#include <windows.h>
#else
// Leitura de /proc/<pid>/io; nada mais do kernel e necessario aqui.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif
#include <chrono>
#include <algorithm>

#ifndef _WIN32
namespace {

// O equivalente de GetProcessIoCounters em Linux e /proc/<pid>/io: read_bytes e write_bytes contam
// o tráfego que passou pela camada de bloco, que e exatamente o que ReadTransferCount e
// WriteTransferCount do Windows medem. O ficheiro e modo 0400 do dono do processo, por isso um
// processo de outro utilizador devolve EACCES — e nesse caso saimos com 0.0, tal como a rota de
// Windows faz quando o OpenProcess e recusado.
bool ReadProcessIoBytes(uint32_t pid, uint64_t* readBytes, uint64_t* writeBytes) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/io", static_cast<unsigned>(pid));
    FILE* f = fopen(path, "re");
    if (!f) return false;
    char line[128];
    uint64_t r = 0, w = 0;
    bool haveR = false, haveW = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "read_bytes:", 11) == 0) {
            r = static_cast<uint64_t>(strtoull(line + 11, nullptr, 10));
            haveR = true;
        } else if (strncmp(line, "write_bytes:", 12) == 0) {
            w = static_cast<uint64_t>(strtoull(line + 12, nullptr, 10));
            haveW = true;
        }
    }
    fclose(f);
    if (!haveR && !haveW) return false;
    *readBytes = r;
    *writeBytes = w;
    return true;
}

} // namespace
#endif

namespace opitcore {

TelemetryValidator::TelemetryValidator() = default;

uint64_t TelemetryValidator::GetCurrentTimestampMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

double TelemetryValidator::SampleProcessDiskActivity(uint32_t pid) {
    if (pid <= 4) return 0.0;

#ifdef _WIN32
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return 0.0;

    IO_COUNTERS ioc{};
    double diskRateMBs = 0.0;
    if (GetProcessIoCounters(hProc, &ioc)) {
        uint64_t nowMs = GetCurrentTimestampMs();
        uint64_t totalBytes = ioc.ReadTransferCount + ioc.WriteTransferCount;
        uint64_t prevBytes = m_lastIoReadBytes + m_lastIoWriteBytes;

        if (m_lastIoTimeMs > 0 && nowMs > m_lastIoTimeMs) {
            double deltaSec = (nowMs - m_lastIoTimeMs) / 1000.0;
            if (deltaSec > 0.05 && totalBytes >= prevBytes) {
                double bytesSec = (totalBytes - prevBytes) / deltaSec;
                diskRateMBs = bytesSec / (1024.0 * 1024.0);
            }
        }
        m_lastIoReadBytes = ioc.ReadTransferCount;
        m_lastIoWriteBytes = ioc.WriteTransferCount;
        m_lastIoTimeMs = nowMs;
    }
    CloseHandle(hProc);
#else
    uint64_t readBytes = 0, writeBytes = 0;
    double diskRateMBs = 0.0;
    if (ReadProcessIoBytes(pid, &readBytes, &writeBytes)) {
        uint64_t nowMs = GetCurrentTimestampMs();
        uint64_t totalBytes = readBytes + writeBytes;
        uint64_t prevBytes = m_lastIoReadBytes + m_lastIoWriteBytes;

        if (m_lastIoTimeMs > 0 && nowMs > m_lastIoTimeMs) {
            double deltaSec = (nowMs - m_lastIoTimeMs) / 1000.0;
            if (deltaSec > 0.05 && totalBytes >= prevBytes) {
                double bytesSec = (totalBytes - prevBytes) / deltaSec;
                diskRateMBs = bytesSec / (1024.0 * 1024.0);
            }
        }
        m_lastIoReadBytes = readBytes;
        m_lastIoWriteBytes = writeBytes;
        m_lastIoTimeMs = nowMs;
    }
#endif

    // Converte taxa de I/O em percentual de saturação estimada (referência 150 MB/s para saturação de disco de boot convencional)
    double diskPercent = (diskRateMBs / 150.0) * 100.0;
    return std::clamp(diskPercent, 0.0, 100.0);
}

ValidatedSystemTelemetry TelemetryValidator::Validate(const SystemMetricsSnapshot& raw, uint32_t activeProcessId) {
    uint64_t nowMs = GetCurrentTimestampMs();
    ValidatedSystemTelemetry val;
    val.sampleTimestampMs = nowMs;
    val.isThermalThrottling = raw.isThermalThrottling;
    val.isGameRunning = raw.isGameRunning;
    val.runningGameName = raw.runningGameName;
    val.activePowerSchemeGuid = raw.activePowerSchemeGuid;
    val.activeProfileName = raw.activeProfileName;

    // 1. CPU Usage
    val.cpuUsage.unit = "%";
    val.cpuUsage.origin = DataOrigin::Measured;
    val.cpuUsage.timestampMs = nowMs;
    val.cpuUsage.uncertainty = 0.5; // ±0.5% resolução GetSystemTimes

    if (raw.cpuUsagePercent < 0.0 || raw.cpuUsagePercent > 100.0) {
        val.cpuUsage.value = std::clamp(raw.cpuUsagePercent, 0.0, 100.0);
        val.cpuUsage.state = MetricState::Inconsistent;
        val.cpuUsage.reason = "Valor de CPU fora dos limites normais [0, 100]";
    } else {
        val.cpuUsage.value = raw.cpuUsagePercent;
        val.cpuUsage.state = MetricState::Valid;
    }

    // 2. RAM Usage
    val.ramUsage.unit = "%";
    val.ramUsage.origin = DataOrigin::Measured;
    val.ramUsage.timestampMs = nowMs;
    val.ramUsage.uncertainty = 0.1;
    if (raw.ramUsagePercent < 0.0 || raw.ramUsagePercent > 100.0) {
        val.ramUsage.value = std::clamp(raw.ramUsagePercent, 0.0, 100.0);
        val.ramUsage.state = MetricState::Inconsistent;
    } else {
        val.ramUsage.value = raw.ramUsagePercent;
        val.ramUsage.state = MetricState::Valid;
    }

    val.ramUsedMB.value = raw.ramUsedMB;
    val.ramUsedMB.unit = "MB";
    val.ramUsedMB.origin = DataOrigin::Measured;
    val.ramUsedMB.state = MetricState::Valid;
    val.ramUsedMB.timestampMs = nowMs;

    val.ramTotalMB.value = raw.ramTotalMB;
    val.ramTotalMB.unit = "MB";
    val.ramTotalMB.origin = DataOrigin::Measured;
    val.ramTotalMB.state = MetricState::Valid;
    val.ramTotalMB.timestampMs = nowMs;

    // 3. CPU Temperature
    val.cpuTemperatureC.unit = "°C";
    val.cpuTemperatureC.timestampMs = nowMs;
    val.cpuTemperatureC.uncertainty = 1.0; // Resolução típica de 1°C ACPI ThermalZone

    if (!raw.hasTempSensor) {
        val.cpuTemperatureC.value = 0.0;
        val.cpuTemperatureC.state = MetricState::Unavailable;
        val.cpuTemperatureC.origin = DataOrigin::Unknown;
        val.cpuTemperatureC.reason = "Sensor ACPI/WMI nao disponibilizado pelo driver ACPI ou firmware da placa-mae";
    } else if (raw.cpuTemperatureC <= 0.0 || raw.cpuTemperatureC > 125.0) {
        val.cpuTemperatureC.value = raw.cpuTemperatureC;
        val.cpuTemperatureC.state = MetricState::Suspicious;
        val.cpuTemperatureC.origin = DataOrigin::Detected;
        val.cpuTemperatureC.reason = "Leitura termica improvavel ou sensor retornando faixa anomala";
    } else {
        val.cpuTemperatureC.value = raw.cpuTemperatureC;
        val.cpuTemperatureC.state = MetricState::Valid;
        val.cpuTemperatureC.origin = DataOrigin::Measured;
    }

    // 4. Disk Activity
    double diskAct = SampleProcessDiskActivity(activeProcessId);
    val.diskBusyPercent.value = diskAct;
    val.diskBusyPercent.unit = "%";
    val.diskBusyPercent.origin = DataOrigin::Measured;
    val.diskBusyPercent.state = MetricState::Valid;
    val.diskBusyPercent.timestampMs = nowMs;
    val.diskBusyPercent.uncertainty = 5.0;

    // 5. GPU Usage
    // Nota: sem driver proprietario (NVAPI/ADLX) ou D3DKMT com suporte ativo, a GPU e tratada como Unavailable para evitar dados falsos
    val.gpuUsagePercent.value = 0.0;
    val.gpuUsagePercent.unit = "%";
    val.gpuUsagePercent.origin = DataOrigin::Unknown;
    val.gpuUsagePercent.state = MetricState::Unavailable;
    val.gpuUsagePercent.reason = "Telemetria de carga de GPU requer driver D3DKMT ou NVAPI dedicado";

    // 6. Frame time real (DWM)
    val.frameTimeMs.unit = "ms";
    val.frameTimeMs.timestampMs = nowMs;
    val.frameTimeMs.uncertainty = 2.0;
    if (!raw.hasFrameTime || raw.frameTimeMs <= 0.0) {
        val.frameTimeMs.value = 0.0;
        val.frameTimeMs.state = MetricState::Unavailable;
        val.frameTimeMs.origin = DataOrigin::Unknown;
        val.frameTimeMs.reason = "Sem estatisticas de composicao DWM nesta amostra (sem janela registrada ou compositor ocioso)";
    } else if (raw.frameTimeMs > 1000.0) {
        val.frameTimeMs.value = raw.frameTimeMs;
        val.frameTimeMs.state = MetricState::Suspicious;
        val.frameTimeMs.origin = DataOrigin::Measured;
        val.frameTimeMs.reason = "Frame time acima de 1s improvavel para o compositor";
    } else {
        val.frameTimeMs.value = raw.frameTimeMs;
        val.frameTimeMs.state = MetricState::Valid;
        val.frameTimeMs.origin = DataOrigin::Measured;
    }

    // 7. Potencia de GPU medida pelo driver
    val.gpuPowerWatts.unit = "W";
    val.gpuPowerWatts.timestampMs = nowMs;
    val.gpuPowerWatts.uncertainty = 3.0;
    if (!raw.hasGpuPowerW || raw.gpuPowerWatts <= 0.0) {
        val.gpuPowerWatts.value = 0.0;
        val.gpuPowerWatts.state = MetricState::Unavailable;
        val.gpuPowerWatts.origin = DataOrigin::Unknown;
        val.gpuPowerWatts.reason = "Sensor de potencia nao exposto pelo driver do fabricante";
    } else if (raw.gpuPowerWatts > 1200.0) {
        val.gpuPowerWatts.value = raw.gpuPowerWatts;
        val.gpuPowerWatts.state = MetricState::Suspicious;
        val.gpuPowerWatts.origin = DataOrigin::Measured;
        val.gpuPowerWatts.reason = "Potencia acima de 1200W improvavel para uma GPU";
    } else {
        val.gpuPowerWatts.value = raw.gpuPowerWatts;
        val.gpuPowerWatts.state = MetricState::Valid;
        val.gpuPowerWatts.origin = DataOrigin::Measured;
    }

    m_lastSampleTimeMs = nowMs;
    m_lastCpuUsage = val.cpuUsage.value;
    m_lastTempC = val.cpuTemperatureC.value;

    return val;
}

} // namespace opitcore
