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
