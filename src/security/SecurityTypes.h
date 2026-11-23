// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace opitcore {
namespace security {

enum class SecurityCategory {
    CategoryA, // Evento não crítico (log, métrica, operação contínua)
    CategoryB, // Integridade não confirmada em módulo secundário (bloqueio parcial de componente)
    CategoryC  // Violação crítica confirmada (bloqueio de orquestração e encaminhamento para reparo)
};

enum class IntegrityStatus {
    Valid,
    Tampered,
    Missing,
    UntrustedSignature,
    ManifestMissing,
    ManifestInvalid,
    ReadError
};

inline const char* IntegrityStatusToString(IntegrityStatus status) {
    switch (status) {
        case IntegrityStatus::Valid: return "Valid";
        case IntegrityStatus::Tampered: return "Tampered";
        case IntegrityStatus::Missing: return "Missing";
        case IntegrityStatus::UntrustedSignature: return "UntrustedSignature";
        case IntegrityStatus::ManifestMissing: return "ManifestMissing";
        case IntegrityStatus::ManifestInvalid: return "ManifestInvalid";
        case IntegrityStatus::ReadError: return "ReadError";
        default: return "Unknown";
    }
}

inline const char* SecurityCategoryToString(SecurityCategory cat) {
    switch (cat) {
        case SecurityCategory::CategoryA: return "CategoryA";
        case SecurityCategory::CategoryB: return "CategoryB";
        case SecurityCategory::CategoryC: return "CategoryC";
        default: return "Unknown";
    }
}

struct FileIntegrityRecord {
    std::string relativePath;
    std::string expectedSha256;
    std::string componentId;
    bool isCritical{false};
};

struct SecurityEvent {
    SecurityCategory category{SecurityCategory::CategoryA};
    std::string component;
    std::string description;
    IntegrityStatus status{IntegrityStatus::Valid};
    uint64_t timestampMs{0};
};

struct SecurityMetrics {
    double startupCheckTimeMs{0.0};
    uint32_t totalFilesChecked{0};
    uint32_t cacheHits{0};
    uint32_t hashCalculations{0};
    bool isSecure{false};
};

} // namespace security
} // namespace opitcore
