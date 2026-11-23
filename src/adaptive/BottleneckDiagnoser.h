// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "TelemetryValidator.h"
#include <string>
#include <vector>
#include <deque>

namespace opitcore {

enum class BottleneckType {
    None = 0,
    CpuBound,
    GpuBound,
    MemoryBound,
    DiskBound,
    ThermalThrottled,
    PowerLimited,
    ResourceContention,
    ExternalLimit
};

inline const char* BottleneckTypeToString(BottleneckType b) {
    switch (b) {
        case BottleneckType::None: return "None";
        case BottleneckType::CpuBound: return "CpuBound";
        case BottleneckType::GpuBound: return "GpuBound";
        case BottleneckType::MemoryBound: return "MemoryBound";
        case BottleneckType::DiskBound: return "DiskBound";
        case BottleneckType::ThermalThrottled: return "ThermalThrottled";
        case BottleneckType::PowerLimited: return "PowerLimited";
        case BottleneckType::ResourceContention: return "ResourceContention";
        default: return "ExternalLimit";
    }
}

enum class DiagnosisConfidence {
    InsufficientData = 0, // Amostras insuficientes (< 3 amostras na janela)
    Hypothesis = 1,       // Pico transitório isolado
    Probable = 2,         // Consistência temporal observada em > 60% da janela
    Confirmed = 3         // Evidência multifatorial irrefutável (ex: throttle térmico ativo + temp alta)
};

inline const char* DiagnosisConfidenceToString(DiagnosisConfidence c) {
    switch (c) {
        case DiagnosisConfidence::Confirmed: return "Confirmed";
        case DiagnosisConfidence::Probable: return "Probable";
        case DiagnosisConfidence::Hypothesis: return "Hypothesis";
        default: return "InsufficientData";
    }
}

struct BottleneckDiagnosis {
    BottleneckType primaryType = BottleneckType::None;
    DiagnosisConfidence confidence = DiagnosisConfidence::InsufficientData;
    std::string justification;
    std::vector<std::string> supportingEvidence;
    std::vector<std::string> limitations;
    uint64_t timestampMs = 0;

    bool RequiresAction() const {
        return (primaryType != BottleneckType::None && confidence >= DiagnosisConfidence::Probable);
    }
};

class BottleneckDiagnoser {
public:
    explicit BottleneckDiagnoser(size_t windowSize = 5);

    void Reset();
    void FeedSample(const ValidatedSystemTelemetry& sample);
    BottleneckDiagnosis Evaluate();

private:
    size_t m_windowSize;
    std::deque<ValidatedSystemTelemetry> m_window;
};

} // namespace opitcore
