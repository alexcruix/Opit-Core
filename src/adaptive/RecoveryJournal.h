// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "core/DatabaseManager.h"
#include "power/PowerManager.h"
#include "platform/windows/ServiceManager.h"
#include "platform/windows/ProcessManager.h"
#include <string>
#include <vector>
#include <mutex>

namespace opitcore {

class RecoveryJournal {
public:
    static RecoveryJournal& Instance();

    // Ciclo de vida de alteração segura
    // PREPARED -> APPLIED -> (KEPT | REVERTED). PREPARED e APPLIED sao transientes e recuperaveis apos queda.
    bool PrepareChange(const std::string& changeId, const std::string& resourceType, const std::string& resourceKey,
                       const std::string& originalValue, const std::string& appliedValue);
    bool ConfirmApplied(const std::string& changeId);
    bool FinalizeKept(const std::string& changeId);

    // Reverte de facto o recurso no sistema e so depois marca REVERTED.
    bool RevertChange(const std::string& changeId, PowerManager& powerMgr, ServiceManager& serviceMgr);

    // Recuperação pós-queda / desligamento inesperado (Startup Crash Recovery)
    int RecoverPendingChanges(PowerManager& powerMgr, ServiceManager& serviceMgr);

    std::string GenerateChangeId(const std::string& prefix = "CHG");

private:
    RecoveryJournal() = default;

    bool ApplyRollback(const RecoveryJournalRecord& entry, PowerManager& powerMgr, ServiceManager& serviceMgr);

    std::mutex m_mutex;
};

} // namespace opitcore
