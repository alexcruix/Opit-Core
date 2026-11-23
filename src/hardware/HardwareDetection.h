// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"

namespace opitcore {

class HardwareDetection {
public:
    static CpuDetails DetectCpu();
    static MemoryDetails DetectMemory();
    static SystemHardwareInfo DetectAll();
};

} // namespace opitcore
