// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <windows.h>
#include <string>

namespace opitcore {

class FanCurveEditorDialog {
public:
    // Exibe o editor visual da curva de ventoinhas com arrasto interativo de pontos e clique direito
    static bool Show(HWND hParent, const std::string& deviceId = "");
};

} // namespace opitcore
