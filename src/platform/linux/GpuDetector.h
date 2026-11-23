// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

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
