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
