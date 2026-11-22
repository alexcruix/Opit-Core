#pragma once
#include "utils/Types.h"
#include <vector>

namespace opitcore {

class GpuDetector {
public:
    static std::vector<GpuDetails> DetectGpus();
};

} // namespace opitcore
