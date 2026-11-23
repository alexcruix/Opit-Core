// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"
#include <vector>

namespace opitcore {

// Mesma assinatura de platform/windows/GpuDetector.h.
class GpuDetector {
public:
    static std::vector<GpuDetails> DetectGpus();
};

} // namespace opitcore
