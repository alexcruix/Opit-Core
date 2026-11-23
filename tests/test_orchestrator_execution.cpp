// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

// Prova de execução do orquestrador: decisões que não escrevem no sistema não são orquestração.
// Este teste mede o efeito real no powrprof (leitura de volta), nunca o retorno isolado de uma função.
#include "adaptive/AdaptiveEngine.h"
#include "adaptive/RecoveryJournal.h"
#include "core/DatabaseManager.h"
#include "core/UnifiedSettingsModel.h"
#include "power/PowerManager.h"
#include "platform/windows/ServiceManager.h"
#include <filesystem>
#include <iostream>
#include <string>

using namespace opitcore;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& label) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << std::endl;
    if (!ok) g_failures++;
}

SystemMetricsSnapshot MakeSnap(double cpuUsage, double tempC) {
    SystemMetricsSnapshot s;
    s.cpuUsagePercent = cpuUsage;
    s.ramUsagePercent = 40.0;
    s.ramUsedMB = 6553;
    s.ramTotalMB = 16384;
    s.hasTempSensor = true;
    s.cpuTemperatureC = tempC;
    s.isThermalThrottling = false;
    s.isGameRunning = false;
    return s;
}

constexpr uint32_t kReadFailed = 0xFFFFFFFFu;

uint32_t ReadParkingAc(PowerManager& pm, const std::string& schemeGuid) {
    uint32_t ac = 0, dc = 0;
    if (schemeGuid.empty() || !pm.ReadCoreParking(schemeGuid, ac, dc)) return kReadFailed;
    return ac;
}

// Restaura o valor real da maquina mesmo quando um check falha.
class ParkingGuard {
public:
    ParkingGuard(PowerManager& pm, const std::string& schemeGuid, uint32_t originalAc)
        : m_pm(pm), m_guid(schemeGuid), m_originalAc(originalAc) {}
    ~ParkingGuard() {
        if (m_guid.empty()) return;
        m_pm.SetCoreParking(m_originalAc);
        uint32_t now = ReadParkingAc(m_pm, m_guid);
        std::cout << "[cleanup] core parking devolvido para " << m_originalAc << "% (lido de volta: "
                  << (now == kReadFailed ? std::string("ilegivel") : std::to_string(now)) << "%)" << std::endl;
    }
private:
    PowerManager& m_pm;
    std::string m_guid;
    uint32_t m_originalAc = 100;
};

} // namespace

int main() {
    const std::string dbPath = "test_orchestrator_execution.db";
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);

    auto& db = DatabaseManager::Instance();
    if (!db.Open(dbPath)) {
        std::cout << "[SKIP] banco de dados indisponivel; nada foi verificado (NOT_RUN)." << std::endl;
        return 0;
    }

    PowerManager power;
    power.Initialize();
    PowerScheme active = power.GetActiveScheme();

    uint32_t origAc = 0, origDc = 0;
    if (active.guid.empty() || !power.ReadCoreParking(active.guid, origAc, origDc)) {
        std::cout << "[SKIP] o core parking atual nao pode ser lido; nada foi verificado (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }
    std::cout << "[info] plano ativo: " << active.name << " | core parking real AC=" << origAc << "% DC=" << origDc << "%" << std::endl;

    // A API de escrita grava AC e DC com o mesmo valor: se a maquina tiver os dois diferentes,
    // a limpeza do teste alteraria o DC. Nesse caso o teste nao roda em vez de degradar a configuracao.
    if (origAc != origDc) {
        std::cout << "[SKIP] core parking assimetrico (AC=" << origAc << "% DC=" << origDc
                  << "%): a escrita do teste nao poderia restaurar o DC (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }

    ParkingGuard guard(power, active.guid, origAc);
    UnifiedSettingsModel::Instance().enableOrchestrator = true;

    if (!power.SetCoreParking(25) || ReadParkingAc(power, active.guid) != 25) {
        std::cout << "[SKIP] o sistema recusou a escrita de core parking; execucao nao verificavel (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }
    Check(true, "preparacao: escrita de 25% confirmada por leitura de volta");

    AdaptiveEngine::Instance().Initialize();

    // 1. A escada em Idle precisa executar, nao apenas deliberar.
    AdaptiveDecision dec = AdaptiveEngine::Instance().ProcessTick(MakeSnap(92.0, 61.0), power, ServiceManager::Instance());
    uint32_t afterLadder = ReadParkingAc(power, active.guid);
    std::cout << "[info] acao=" << AdaptiveActionTypeToString(dec.action) << " alvo=" << dec.targetResource << "="
              << dec.targetValue << " | " << dec.justification << std::endl;
    Check(afterLadder == 50, "orquestrador agiu no sistema: parking 25% -> 50% medido no powrprof");
    Check(dec.justification.find("Aplicado") != std::string::npos, "a decisao registra que a alteracao foi aplicada");
    Check(db.GetPendingRecoveryEntries().empty(),
          "acao direta e terminal (KEPT): a recuperacao de boot nao vai desfazer uma politica vigente");

    // 2. Reversao: o diario restaura o valor REAL de origem, nao apenas o status no SQLite.
    std::string chg = RecoveryJournal::Instance().GenerateChangeId("TEST_PARK");
    std::string originalStr = std::to_string(origAc) + ":" + std::to_string(origDc);
    Check(RecoveryJournal::Instance().PrepareChange(chg, "CORE_PARKING", active.guid, originalStr, "75"),
          "diario grava PREPARED com o valor de origem lido do sistema");

    power.SetCoreParking(75);
    Check(ReadParkingAc(power, active.guid) == 75, "preparacao do caso de reversao: sistema em 75%");
    Check(RecoveryJournal::Instance().ConfirmApplied(chg), "ConfirmApplied marca a entrada como APPLIED");

    bool pendingHasEntry = false;
    for (const auto& entry : db.GetPendingRecoveryEntries()) {
        if (entry.changeId == chg) pendingHasEntry = true;
    }
    Check(pendingHasEntry, "mudanca aplicada e nao validada fica pendente para a recuperacao de boot");

    int recovered = RecoveryJournal::Instance().RecoverPendingChanges(power, ServiceManager::Instance());
    uint32_t afterRecovery = ReadParkingAc(power, active.guid);
    std::cout << "[info] entradas revertidas na recuperacao: " << recovered << " | parking agora: " << afterRecovery << "%" << std::endl;
    Check(recovered >= 1, "recuperacao de pendencias executou pelo menos uma reversao");
    Check(afterRecovery == origAc, "a reversao restaurou o valor real de origem no sistema");
    Check(db.GetPendingRecoveryEntries().empty(), "nenhuma entrada ficou pendente apos a recuperacao");

    // 3. O diario nao pode afirmar sucesso sobre ids que nao existem.
    Check(!RecoveryJournal::Instance().ConfirmApplied("CHG_INVENTADO"), "ConfirmApplied de id inexistente devolve false");
    Check(!RecoveryJournal::Instance().FinalizeKept("CHG_INVENTADO"), "FinalizeKept de id inexistente devolve false");
    Check(!RecoveryJournal::Instance().RevertChange("CHG_INVENTADO", power, ServiceManager::Instance()),
          "RevertChange de id inexistente devolve false");
    Check(!RecoveryJournal::Instance().PrepareChange("", "CORE_PARKING", active.guid, "50:50", "75"),
          "PrepareChange sem changeId e recusado (a alteracao nao pode ficar sem rastro)");

    db.Close();
    std::filesystem::remove(dbPath, ec);

    std::cout << (g_failures == 0 ? "ORQUESTRADOR: EXECUCAO E REVERSAO VERIFICADAS" : "ORQUESTRADOR: FALHAS DETECTADAS")
              << " (" << g_failures << " falha(s))" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
