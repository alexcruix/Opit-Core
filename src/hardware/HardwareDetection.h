// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

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
