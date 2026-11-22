#include "SecurityPolicy.h"

namespace opitcore {
namespace security {

SecurityCategory SecurityPolicy::EvaluateStatus(IntegrityStatus status, bool isCriticalComponent) {
    if (status == IntegrityStatus::Valid) {
        return SecurityCategory::CategoryA;
    }

    if (status == IntegrityStatus::UntrustedSignature ||
        status == IntegrityStatus::ManifestInvalid ||
        status == IntegrityStatus::ManifestMissing) {
        return SecurityCategory::CategoryC;
    }

    if (status == IntegrityStatus::Tampered) {
        return isCriticalComponent ? SecurityCategory::CategoryC : SecurityCategory::CategoryB;
    }

    if (status == IntegrityStatus::Missing) {
        return isCriticalComponent ? SecurityCategory::CategoryC : SecurityCategory::CategoryB;
    }

    // ReadError em arquivo crítico vira Categoria B temporária para retry
    return isCriticalComponent ? SecurityCategory::CategoryB : SecurityCategory::CategoryA;
}

void SecurityPolicy::EvaluateOverall(const std::vector<SecurityEvent>& events,
                                     bool& outShouldBlockExecution,
                                     SecurityCategory& outHighestCategory) {
    outShouldBlockExecution = false;
    outHighestCategory = SecurityCategory::CategoryA;

    for (const auto& ev : events) {
        if (ev.category == SecurityCategory::CategoryC) {
            outHighestCategory = SecurityCategory::CategoryC;
            outShouldBlockExecution = true;
            return;
        } else if (ev.category == SecurityCategory::CategoryB) {
            if (outHighestCategory != SecurityCategory::CategoryC) {
                outHighestCategory = SecurityCategory::CategoryB;
            }
        }
    }
}

} // namespace security
} // namespace opitcore
