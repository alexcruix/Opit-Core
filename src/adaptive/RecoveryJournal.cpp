// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "RecoveryJournal.h"
#include "core/Logger.h"
#include <chrono>
#include <sstream>
#include <vector>

namespace opitcore {
namespace {

// "5:100" -> {5,100}, "100" -> {100}. Texto ilegivel ou fora de 0..100 devolve vazio:
// o diario nunca pode adivinhar um valor original, porque restaurar um chute destruiria a configuracao do usuario.
std::vector<uint32_t> ParseJournalNumbers(const std::string& text) {
    std::vector<uint32_t> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t sep = text.find(':', pos);
        std::string token = (sep == std::string::npos) ? text.substr(pos) : text.substr(pos, sep - pos);
        if (token.empty()) return {};
        unsigned long value = 0;
        try {
            size_t consumed = 0;
            value = std::stoul(token, &consumed);
            if (consumed != token.size() || value > 100) return {};
        } catch (...) {
            return {};
        }
        out.push_back(static_cast<uint32_t>(value));
        if (out.size() > 2) return {};
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return out;
}

} // namespace

RecoveryJournal& RecoveryJournal::Instance() {
    static RecoveryJournal instance;
    return instance;
}

std::string RecoveryJournal::GenerateChangeId(const std::string& prefix) {
    auto now = std::chrono::steady_clock::now();
    uint64_t count = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    std::stringstream ss;
    ss << prefix << "_" << count;
    return ss.str();
}

bool RecoveryJournal::PrepareChange(const std::string& changeId, const std::string& resourceType, const std::string& resourceKey,
                                   const std::string& originalValue, const std::string& appliedValue) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (changeId.empty() || resourceType.empty() || originalValue.empty()) {
        OPIT_LOG_ERROR("RecoveryJournal: PrepareChange recusado sem changeId, tipo ou valor original - a alteracao nao sera aplicada.");
        return false;
    }

    RecoveryJournalRecord rec;
    rec.changeId = changeId;
    rec.resourceType = resourceType;
    rec.resourceKey = resourceKey;
    rec.originalValue = originalValue;
    rec.appliedValue = appliedValue;
    rec.status = "PREPARED";

    return DatabaseManager::Instance().AddRecoveryJournalEntry(rec);
}

bool RecoveryJournal::ConfirmApplied(const std::string& changeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (changeId.empty()) return false;
    return DatabaseManager::Instance().UpdateRecoveryJournalStatus(changeId, "APPLIED");
}

bool RecoveryJournal::FinalizeKept(const std::string& changeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (changeId.empty()) return false;
    return DatabaseManager::Instance().UpdateRecoveryJournalStatus(changeId, "KEPT");
}

bool RecoveryJournal::RevertChange(const std::string& changeId, PowerManager& powerMgr, ServiceManager& serviceMgr) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (changeId.empty()) return false;

    RecoveryJournalRecord rec;
    if (!DatabaseManager::Instance().LoadRecoveryJournalEntry(changeId, rec)) {
        OPIT_LOG_ERROR("RecoveryJournal: nao ha entrada para reverter: " + changeId);
        return false;
    }

    if (rec.status == "REVERTED" || rec.status == "KEPT") {
        return true;
    }

    bool rolledBack = ApplyRollback(rec, powerMgr, serviceMgr);
    DatabaseManager::Instance().UpdateRecoveryJournalStatus(changeId, rolledBack ? "REVERTED" : "FAILED_RECOVERY");
    return rolledBack;
}

bool RecoveryJournal::ApplyRollback(const RecoveryJournalRecord& entry, PowerManager& powerMgr, ServiceManager& serviceMgr) {
    if (entry.resourceType == "SERVICE") {
        if (entry.originalValue == "RUNNING") {
            bool ok = serviceMgr.ResumeOrStartService(entry.resourceKey);
            OPIT_LOG_INFO("RecoveryJournal: servico devolvido para RUNNING: " + entry.resourceKey + (ok ? "" : " (falha)"));
            return ok;
        }
        return true; // originalmente parado: o sistema ja esta no estado de origem
    }

    if (entry.resourceType == "POWER_SCHEME") {
        if (entry.originalValue.empty()) return false;
        bool ok = powerMgr.SetActiveScheme(entry.originalValue);
        // O diario guarda o GUID internamente, mas o log e publico: mostra o nome amigavel do plano.
        std::string shown = entry.originalValue;
        for (const auto& s : powerMgr.ListSchemes()) {
            if (s.guid == entry.originalValue) { shown = s.name; break; }
        }
        OPIT_LOG_INFO("RecoveryJournal: plano de energia devolvido para " + shown + (ok ? "" : " (falha)"));
        return ok;
    }

    if (entry.resourceType == "CPU_THROTTLE") {
        std::vector<uint32_t> values = ParseJournalNumbers(entry.originalValue);
        if (values.empty()) {
            OPIT_LOG_ERROR("RecoveryJournal: valor original de throttle ilegivel (" + entry.originalValue + "); nenhuma escrita sera feita.");
            return false;
        }
        uint32_t maxPct = values.back();
        uint32_t minPct = (values.size() >= 2) ? values[0] : 5;
        if (minPct > maxPct) return false;
        bool ok = powerMgr.SetProcessorThrottleRange(minPct, maxPct);
        OPIT_LOG_INFO("RecoveryJournal: limites de CPU restaurados para " + entry.originalValue + (ok ? "" : " (falha)"));
        return ok;
    }

    if (entry.resourceType == "CORE_PARKING") {
        std::vector<uint32_t> values = ParseJournalNumbers(entry.originalValue);
        if (values.empty()) {
            OPIT_LOG_ERROR("RecoveryJournal: valor original de core parking ilegivel (" + entry.originalValue + "); nenhuma escrita sera feita.");
            return false;
        }
        uint32_t originalAc = values[0];
        bool ok = entry.resourceKey.empty()
                      ? powerMgr.SetCoreParking(originalAc)
                      : powerMgr.SetCoreParkingForScheme(entry.resourceKey, originalAc);
        if (values.size() >= 2 && values[1] != values[0]) {
            OPIT_LOG_WARN("RecoveryJournal: parking DC (" + std::to_string(values[1]) +
                          "%) nao restaurado - a API de escrita grava AC e DC com o mesmo valor.");
        }
        OPIT_LOG_INFO("RecoveryJournal: core parking restaurado para " + std::to_string(originalAc) + "% (AC)" + (ok ? "" : " (falha)"));
        return ok;
    }

    OPIT_LOG_ERROR("RecoveryJournal: tipo de recurso sem executor de reversao: " + entry.resourceType);
    return false;
}

int RecoveryJournal::RecoverPendingChanges(PowerManager& powerMgr, ServiceManager& serviceMgr) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto pending = DatabaseManager::Instance().GetPendingRecoveryEntries();
    if (pending.empty()) {
        return 0;
    }

    OPIT_LOG_WARN("RecoveryJournal: Detectadas " + std::to_string(pending.size()) +
                  " operacoes pendentes de sessoes anteriores. Executando recuperacao segura...");

    int recoveredCount = 0;
    // Ordem inversa a da aplicacao: o ultimo efeito a ser produzido e o primeiro a ser desfeito.
    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        const RecoveryJournalRecord& entry = *it;
        bool ok = ApplyRollback(entry, powerMgr, serviceMgr);
        if (ok) {
            DatabaseManager::Instance().UpdateRecoveryJournalStatus(entry.changeId, "REVERTED");
            recoveredCount++;
        } else {
            DatabaseManager::Instance().UpdateRecoveryJournalStatus(entry.changeId, "FAILED_RECOVERY");
            OPIT_LOG_ERROR("RecoveryJournal: Falha na restauracao da operacao: " + entry.changeId);
        }
    }

    OPIT_LOG_INFO("RecoveryJournal: Concluida recuperacao (" + std::to_string(recoveredCount) + "/" +
                  std::to_string(pending.size()) + " revertidas com sucesso)");

    int pruned = DatabaseManager::Instance().PruneRecoveryJournalHistory(500);
    if (pruned > 0) {
        OPIT_LOG_INFO("RecoveryJournal: " + std::to_string(pruned) + " entradas terminais fora da janela de retencao.");
    }

    return recoveredCount;
}

} // namespace opitcore
