#include "adaptive/TelemetryValidator.h"
#include "adaptive/AppProfileManager.h"
#include "adaptive/BottleneckDiagnoser.h"
#include "adaptive/RecoveryJournal.h"
#include "adaptive/AdaptiveEngine.h"
#include "core/DatabaseManager.h"
#include "power/PowerManager.h"
#include "platform/windows/ServiceManager.h"
#include <iostream>
#include <cassert>
#include <filesystem>

using namespace opitcore;

void TestTelemetryValidation() {
    std::cout << "[Test] TelemetryValidator..." << std::endl;
    TelemetryValidator validator;

    // Caso 1: Métricas normais
    SystemMetricsSnapshot snapNormal;
    snapNormal.cpuUsagePercent = 25.5;
    snapNormal.ramUsagePercent = 40.0;
    snapNormal.ramTotalMB = 16384;
    snapNormal.ramUsedMB = 6553;
    snapNormal.hasTempSensor = true;
    snapNormal.cpuTemperatureC = 55.0;

    ValidatedSystemTelemetry val1 = validator.Validate(snapNormal, 0);
    assert(val1.cpuUsage.state == MetricState::Valid);
    assert(val1.cpuUsage.value == 25.5);
    assert(val1.cpuTemperatureC.state == MetricState::Valid);
    assert(val1.cpuTemperatureC.value == 55.0);

    // Caso 2: Sensor de temperatura ausente
    SystemMetricsSnapshot snapNoTemp;
    snapNoTemp.cpuUsagePercent = 30.0;
    snapNoTemp.hasTempSensor = false;
    snapNoTemp.cpuTemperatureC = 0.0;

    ValidatedSystemTelemetry val2 = validator.Validate(snapNoTemp, 0);
    assert(val2.cpuTemperatureC.state == MetricState::Unavailable);
    assert(val2.cpuTemperatureC.value == 0.0);

    // Caso 3: Temperatura suspeita (fora de faixa física: 135°C)
    SystemMetricsSnapshot snapSusTemp;
    snapSusTemp.hasTempSensor = true;
    snapSusTemp.cpuTemperatureC = 135.0;

    ValidatedSystemTelemetry val3 = validator.Validate(snapSusTemp, 0);
    assert(val3.cpuTemperatureC.state == MetricState::Suspicious);

    // Caso 4: CPU inconsistente (120%)
    SystemMetricsSnapshot snapAnomCpu;
    snapAnomCpu.cpuUsagePercent = 120.0;
    ValidatedSystemTelemetry val4 = validator.Validate(snapAnomCpu, 0);
    assert(val4.cpuUsage.state == MetricState::Inconsistent);
    assert(val4.cpuUsage.value == 100.0);

    std::cout << "  -> TelemetryValidator: OK" << std::endl;
}

void TestObjectiveScoring() {
    std::cout << "[Test] AppProfileManager & Objective Scoring..." << std::endl;

    ValidatedSystemTelemetry tGood;
    tGood.cpuUsage.value = 30.0;
    tGood.cpuUsage.state = MetricState::Valid;
    tGood.cpuTemperatureC.value = 50.0;
    tGood.cpuTemperatureC.state = MetricState::Valid;
    tGood.isThermalThrottling = false;

    ValidatedSystemTelemetry tThrottled;
    tThrottled.cpuUsage.value = 95.0;
    tThrottled.cpuUsage.state = MetricState::Valid;
    tThrottled.cpuTemperatureC.value = 92.0;
    tThrottled.cpuTemperatureC.state = MetricState::Valid;
    tThrottled.isThermalThrottling = true;

    // MaxPerformance score
    double scoreGoodPerf = AppProfileManager::EvaluateObjectiveScore(AppObjective::MaxPerformance, tGood);
    double scoreThrottledPerf = AppProfileManager::EvaluateObjectiveScore(AppObjective::MaxPerformance, tThrottled);
    assert(scoreGoodPerf > scoreThrottledPerf);
    assert(scoreGoodPerf >= 80.0);
    assert(scoreThrottledPerf < 60.0);

    // Stability score
    double scoreGoodStab = AppProfileManager::EvaluateObjectiveScore(AppObjective::Stability, tGood);
    double scoreThrottledStab = AppProfileManager::EvaluateObjectiveScore(AppObjective::Stability, tThrottled);
    assert(scoreGoodStab > scoreThrottledStab);
    assert(scoreThrottledStab <= 30.0); // Severamente penalizado por throttling + 92°C

    // Eco score
    double scoreGoodEco = AppProfileManager::EvaluateObjectiveScore(AppObjective::Eco, tGood);
    double scoreHotEco = AppProfileManager::EvaluateObjectiveScore(AppObjective::Eco, tThrottled);
    assert(scoreGoodEco > scoreHotEco);

    std::cout << "  -> Objective Scoring: OK" << std::endl;
}

void TestBottleneckDiagnosis() {
    std::cout << "[Test] BottleneckDiagnoser..." << std::endl;
    BottleneckDiagnoser diagnoser(5);

    // Amostras insuficientes
    ValidatedSystemTelemetry s1;
    s1.cpuUsage.value = 99.0;
    s1.cpuUsage.state = MetricState::Valid;
    diagnoser.FeedSample(s1);
    auto d1 = diagnoser.Evaluate();
    assert(d1.confidence == DiagnosisConfidence::InsufficientData);

    // Alimentar 4 amostras com throttling térmico confirmado
    for (int i = 0; i < 4; ++i) {
        ValidatedSystemTelemetry s;
        s.cpuUsage.value = 80.0;
        s.cpuUsage.state = MetricState::Valid;
        s.cpuTemperatureC.value = 90.0;
        s.cpuTemperatureC.state = MetricState::Valid;
        s.isThermalThrottling = true;
        diagnoser.FeedSample(s);
    }
    auto dThermal = diagnoser.Evaluate();
    assert(dThermal.primaryType == BottleneckType::ThermalThrottled);
    assert(dThermal.confidence == DiagnosisConfidence::Confirmed);

    // Limpar e testar CPU Bound sustentado
    diagnoser.Reset();
    for (int i = 0; i < 5; ++i) {
        ValidatedSystemTelemetry s;
        s.cpuUsage.value = 95.0;
        s.cpuUsage.state = MetricState::Valid;
        s.cpuTemperatureC.value = 65.0;
        s.cpuTemperatureC.state = MetricState::Valid;
        s.isThermalThrottling = false;
        diagnoser.FeedSample(s);
    }
    auto dCpu = diagnoser.Evaluate();
    assert(dCpu.primaryType == BottleneckType::CpuBound);
    assert(dCpu.confidence >= DiagnosisConfidence::Probable);

    std::cout << "  -> BottleneckDiagnoser: OK" << std::endl;
}

void TestRecoveryJournalAndPersistence() {
    std::cout << "[Test] RecoveryJournal & SQLite Persistence..." << std::endl;

    std::string testDb = "test_adaptive_recovery.db";
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    auto& db = DatabaseManager::Instance();
    assert(db.Open(testDb));
    assert(db.GetSchemaVersion() >= 3);

    auto& journal = RecoveryJournal::Instance();
    std::string chgId = journal.GenerateChangeId("TEST_CHG");

    // 1. Preparar alteração temporária
    assert(journal.PrepareChange(chgId, "CPU_THROTTLE", "PROCESSOR_CAP", "100", "75"));

    // 2. Confirmar aplicação
    assert(journal.ConfirmApplied(chgId));

    // 3. Verificar que aparece em operações pendentes
    auto pending = db.GetPendingRecoveryEntries();
    assert(!pending.empty());
    assert(pending.back().changeId == chgId);
    assert(pending.back().status == "APPLIED");

    // 4. Testar simulação de recuperação pós-queda
    PowerManager powerMgr;
    powerMgr.Initialize();
    ServiceManager& svcMgr = ServiceManager::Instance();

    int recovered = journal.RecoverPendingChanges(powerMgr, svcMgr);
    assert(recovered >= 1);

    // 5. Verificar que a operação foi marcada como REVERTED
    auto pendingAfter = db.GetPendingRecoveryEntries();
    assert(pendingAfter.empty());

    // 6. Testar persistência de perfil de aplicativo
    AppProfileRecord profRec;
    profRec.appId = "photoshop_12345678";
    profRec.normalizedPath = "c:/program files/adobe/photoshop.exe";
    profRec.processName = "photoshop.exe";
    profRec.fileHash = "12345678";
    profRec.objective = static_cast<int>(AppObjective::MaxPerformance);
    profRec.authorizedModules = "power,cpu,priority";
    profRec.confidenceScore = 0.9;
    profRec.baselineScore = 85.0;
    profRec.experimentCount = 2;
    assert(db.SaveAppProfile(profRec));

    AppProfileRecord loadedRec;
    assert(db.LoadAppProfile("photoshop_12345678", loadedRec));
    assert(loadedRec.processName == "photoshop.exe");
    assert(loadedRec.objective == static_cast<int>(AppObjective::MaxPerformance));
    assert(loadedRec.confidenceScore == 0.9);

    db.Close();
    if (std::filesystem::exists(testDb)) {
        std::filesystem::remove(testDb);
    }

    std::cout << "  -> RecoveryJournal & SQLite Persistence: OK" << std::endl;
}

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << " INICIANDO TESTES DO MOTOR INTELIGENTE ADAPTATIVO" << std::endl;
    std::cout << "=================================================" << std::endl;

    TestTelemetryValidation();
    TestObjectiveScoring();
    TestBottleneckDiagnosis();
    TestRecoveryJournalAndPersistence();

    std::cout << "=================================================" << std::endl;
    std::cout << " TODOS OS TESTES ADAPTATIVOS PASSARAM COM SUCESSO" << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
