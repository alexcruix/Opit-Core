#include "CpuMonitor.h"
#include <windows.h>

namespace opitcore {

static ULARGE_INTEGER ft2ul(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u;
}

CpuMonitor::CpuMonitor() = default;

bool CpuMonitor::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstSample = true;
    return true;
}

CpuMetrics CpuMonitor::Sample() {
    std::lock_guard<std::mutex> lock(m_mutex);
    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);

    CpuMetrics m{};
    if (!m_firstSample) {
        ULONGLONG dIdle = ft2ul(idle).QuadPart - ft2ul(m_prevIdle).QuadPart;
        ULONGLONG dKernel = ft2ul(kernel).QuadPart - ft2ul(m_prevKernel).QuadPart;
        ULONGLONG dUser = ft2ul(user).QuadPart - ft2ul(m_prevUser).QuadPart;
        ULONGLONG total = dKernel + dUser;

        if (total > 0 && total >= dIdle) {
            m.totalUsagePercent = 100.0 * (1.0 - (double)dIdle / (double)total);
            if (m.totalUsagePercent < 0.0) m.totalUsagePercent = 0.0;
            if (m.totalUsagePercent > 100.0) m.totalUsagePercent = 100.0;
        }
    }

    m_prevIdle = idle;
    m_prevKernel = kernel;
    m_prevUser = user;
    m_firstSample = false;

    return m;
}

} // namespace opitcore
