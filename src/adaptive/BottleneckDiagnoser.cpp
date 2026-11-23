// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "BottleneckDiagnoser.h"
#include <numeric>
#include <sstream>

namespace opitcore {

BottleneckDiagnoser::BottleneckDiagnoser(size_t windowSize)
    : m_windowSize(windowSize < 3 ? 3 : windowSize) {
}

void BottleneckDiagnoser::Reset() {
    m_window.clear();
}

void BottleneckDiagnoser::FeedSample(const ValidatedSystemTelemetry& sample) {
    m_window.push_back(sample);
    if (m_window.size() > m_windowSize) {
        m_window.pop_front();
    }
}

BottleneckDiagnosis BottleneckDiagnoser::Evaluate() {
    BottleneckDiagnosis diag;
    if (m_window.empty()) {
        diag.primaryType = BottleneckType::None;
        diag.confidence = DiagnosisConfidence::InsufficientData;
        diag.justification = "Nenhuma amostra de telemetria disponivel para diagnostico";
        return diag;
    }

    diag.timestampMs = m_window.back().sampleTimestampMs;

    if (m_window.size() < 3) {
        diag.primaryType = BottleneckType::None;
        diag.confidence = DiagnosisConfidence::InsufficientData;
        diag.justification = "Amostras insuficientes na janela temporal (necessario minimo de 3 amostras)";
        diag.limitations.push_back("Janela temporal em preenchimento (" + std::to_string(m_window.size()) + "/3)");
        return diag;
    }

    // 1. Diagnóstico de Gargalo Térmico (Prioridade absoluta de segurança)
    int thermalThrottlingCount = 0;
    int highTempCount = 0;
    for (const auto& s : m_window) {
        if (s.isThermalThrottling) thermalThrottlingCount++;
        if (s.cpuTemperatureC.IsUsable() && s.cpuTemperatureC.value >= 85.0) highTempCount++;
    }

    if (thermalThrottlingCount > 0) {
        diag.primaryType = BottleneckType::ThermalThrottled;
        diag.confidence = DiagnosisConfidence::Confirmed;
        diag.justification = "Estrangulamento termico (thermal throttling) confirmado no processador";
        diag.supportingEvidence.push_back("Flag de throttling ativa em " + std::to_string(thermalThrottlingCount) + " amostras");
        if (highTempCount > 0) {
            diag.supportingEvidence.push_back("Temperatura maxima observada na janela: >= 85°C");
        }
        return diag;
    }

    // 2. Diagnóstico de Gargalo de Memória RAM
    int highRamCount = 0;
    double avgRam = 0.0;
    for (const auto& s : m_window) {
        if (s.ramUsage.IsUsable() && s.ramUsage.value >= 90.0) highRamCount++;
        if (s.ramUsage.IsUsable()) avgRam += s.ramUsage.value;
    }
    avgRam /= m_window.size();

    if (highRamCount >= (int)m_window.size() - 1 && avgRam >= 92.0) {
        diag.primaryType = BottleneckType::MemoryBound;
        diag.confidence = (avgRam >= 95.0) ? DiagnosisConfidence::Confirmed : DiagnosisConfidence::Probable;
        diag.justification = "Pressao critica de memoria RAM detectada no sistema operacional";
        diag.supportingEvidence.push_back("Media de RAM na janela: " + std::to_string((int)avgRam) + "%");
        return diag;
    }

    // 3. Diagnóstico de Gargalo de Disco (I/O Saturated)
    int highDiskCount = 0;
    double avgDisk = 0.0;
    for (const auto& s : m_window) {
        if (s.diskBusyPercent.IsUsable() && s.diskBusyPercent.value >= 80.0) highDiskCount++;
        if (s.diskBusyPercent.IsUsable()) avgDisk += s.diskBusyPercent.value;
    }
    avgDisk /= m_window.size();

    if (highDiskCount >= 2 && avgDisk >= 75.0) {
        diag.primaryType = BottleneckType::DiskBound;
        diag.confidence = (highDiskCount == (int)m_window.size()) ? DiagnosisConfidence::Confirmed : DiagnosisConfidence::Probable;
        diag.justification = "Saturacao continuada de throughput de I/O em disco";
        diag.supportingEvidence.push_back("Taxa de atividade de disco media: " + std::to_string((int)avgDisk) + "%");
        return diag;
    }

    // 4. Diagnóstico de Gargalo de CPU
    int highCpuCount = 0;
    double avgCpu = 0.0;
    for (const auto& s : m_window) {
        if (s.cpuUsage.IsUsable() && s.cpuUsage.value >= 88.0) highCpuCount++;
        if (s.cpuUsage.IsUsable()) avgCpu += s.cpuUsage.value;
    }
    avgCpu /= m_window.size();

    if (highCpuCount >= (int)m_window.size() - 1 && avgCpu >= 90.0) {
        diag.primaryType = BottleneckType::CpuBound;
        diag.confidence = (avgCpu >= 95.0) ? DiagnosisConfidence::Confirmed : DiagnosisConfidence::Probable;
        diag.justification = "Processamento em saturacao de capacidade (CPU Bound)";
        diag.supportingEvidence.push_back("Utilizacao media da CPU: " + std::to_string((int)avgCpu) + "% em todas as amostras");
        return diag;
    }

    if (highCpuCount == 1) {
        diag.primaryType = BottleneckType::CpuBound;
        diag.confidence = DiagnosisConfidence::Hypothesis;
        diag.justification = "Pico isolado de CPU detectado na janela; provavel carga transiente";
        diag.supportingEvidence.push_back("1 amostra com pico >= 88%");
        return diag;
    }

    // 5. Nenhum gargalo crítico evidente
    diag.primaryType = BottleneckType::None;
    diag.confidence = DiagnosisConfidence::Confirmed;
    diag.justification = "Sistema operando dentro dos parametros de folga de hardware";
    diag.supportingEvidence.push_back("CPU media: " + std::to_string((int)avgCpu) + "%, RAM media: " + std::to_string((int)avgRam) + "%");
    return diag;
}

} // namespace opitcore
