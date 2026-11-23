// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

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
