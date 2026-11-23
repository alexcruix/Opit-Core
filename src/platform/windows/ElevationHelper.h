// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <string>

namespace opitcore {

class ElevationHelper {
public:
    static bool IsElevated();
    static std::string GetOsVersionString();
    static bool Is64BitOs();
    static bool SetRunAtStartup(bool enable);
    static bool IsRunAtStartup();
    static bool SetRunAtStartupAdmin(bool enable);
    static bool IsRunAtStartupAdmin();
    static bool ApplySvchostGrouping(int groupingMode);
    static bool UnlockCoreParkingInRegistry();
};

} // namespace opitcore
