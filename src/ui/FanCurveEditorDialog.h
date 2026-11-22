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
