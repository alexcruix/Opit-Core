// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "CpuMonitor.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fstream>
#endif

namespace opitcore {

#ifdef _WIN32
static uint64_t ft2units100ns(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}
#endif

CpuMonitor::CpuMonitor() = default;

bool CpuMonitor::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstSample = true;
    return true;
}

CpuMetrics CpuMonitor::Sample() {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t idle = 0;
    uint64_t kernel = 0;
    uint64_t user = 0;

#ifdef _WIN32
    FILETIME fIdle, fKernel, fUser;
    if (GetSystemTimes(&fIdle, &fKernel, &fUser)) {
        idle = ft2units100ns(fIdle);
        kernel = ft2units100ns(fKernel);
        user = ft2units100ns(fUser);
    }
#else
    // /proc/stat: user nice system idle iowait irq softirq steal. A aritmetica de deltas
    // la em baixo assume a convencao do GetSystemTimes Windows, em que o tempo do kernel
    // JA INCLUDE o idle e o total e kernel + user. Por isso o idle (e o iowait) entra no
    // bucket kernel daqui: se ficar so em "idle", total fica sem tempo ocioso e o guarda
    // total >= dIdle reprova cada amostra, dando 0.0% ate com as maquinas todas ocupadas.
    // O guest do Linux ja vai somado em user, por isso nao se conta outra vez.
    std::ifstream f("/proc/stat");
    std::string tag;
    uint64_t v0 = 0, v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0, v7 = 0;
    if (f >> tag >> v0 >> v1 >> v2 >> v3 >> v4 >> v5 >> v6 >> v7) {
        // 1 jiffie = 1/CLK_TCK segundos = 10^7/CLK_TCK unidades de 100 ns.
        const uint64_t clkTck = static_cast<uint64_t>(sysconf(_SC_CLK_TCK));
        const uint64_t unitsPerJiffie = (clkTck > 0) ? (10000000ULL / clkTck) : 100000ULL;
        idle = (v3 + v4) * unitsPerJiffie;
        kernel = (v2 + v3 + v4 + v5 + v6 + v7) * unitsPerJiffie;
        user = (v0 + v1) * unitsPerJiffie;
    }
#endif

    CpuMetrics m{};
    if (!m_firstSample) {
        uint64_t dIdle = idle - m_prevIdle;
        uint64_t dKernel = kernel - m_prevKernel;
        uint64_t dUser = user - m_prevUser;
        uint64_t total = dKernel + dUser;

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
