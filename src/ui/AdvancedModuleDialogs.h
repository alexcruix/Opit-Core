// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <windows.h>
#include <string>

namespace opitcore {

class AdvancedModuleDialogs {
public:
    static bool ShowDialog(int moduleIndex, HWND hParent);
    static bool ShowGameProfileDialog(HWND hParent, const std::string& exeName = "");
    static bool ShowAiAssistantDialog(HWND hParent);
    static void ShowServicesDialog(HWND hParent);
    static void ShowProcessesDialog(HWND hParent);
    static void ShowFansDialog(HWND hParent);

private:
    static void ShowPowerPlansDialog(HWND hParent);
    static void ShowCpuMinMaxDialog(HWND hParent);
    static void ShowCoreParkingDialog(HWND hParent);
    static void ShowGpuDialog(HWND hParent);
    static void ShowThermalDialog(HWND hParent);
    static void ShowStabilityDialog(HWND hParent);
};

} // namespace opitcore
