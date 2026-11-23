// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

namespace opitcore {

class ThermalProtection {
public:
    enum class Action { None, Throttle, Restore };

    ThermalProtection();

    // Avalia temperatura atual contra limite configurado e histerese
    Action Evaluate(double tempC, bool hasSensor, double limitC);

    bool IsThrottling() const { return m_isThrottling; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void Reset();

private:
    bool m_isThrottling = false;
    bool m_enabled = true;
};

} // namespace opitcore
