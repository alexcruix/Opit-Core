// Prova do guardiao de integridade de modo: o modo declarado pela usuaria tem que sobreviver a qualquer
// agente que mexa no sistema depois dela. Cada check mede o powrprof de volta; nenhum check mede adjetivo.
#include "adaptive/AdaptiveEngine.h"
#include "adaptive/RecoveryJournal.h"
#include "control/ModeCoordinator.h"
#include "core/DatabaseManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/UnifiedSettingsModel.h"
#include "power/PowerManager.h"
#include "platform/windows/ServiceManager.h"
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

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

struct Reality {
    std::string schemeGuid;
    std::string schemeName;
    uint32_t parkingAc = 0, parkingDc = 0;
    uint32_t minAc = 0, maxAc = 0, minDc = 0, maxDc = 0;
    bool parkingReadable = false;
    bool throttleReadable = false;
};

Reality ReadRealityOf(PowerManager& pm, const std::string& guid, const std::string& name) {
    Reality r;
    r.schemeGuid = guid;
    r.schemeName = name;
    if (guid.empty()) return r;
    r.parkingReadable = pm.ReadCoreParking(guid, r.parkingAc, r.parkingDc);
    r.throttleReadable = pm.ReadSchemeProcessorThrottle(guid, r.minAc, r.maxAc, r.minDc, r.maxDc);
    return r;
}

Reality ReadActiveReality(PowerManager& pm) {
    PowerScheme active = pm.GetActiveScheme();
    return ReadRealityOf(pm, active.guid, active.name);
}

void PrintReality(const std::string& label, const Reality& r) {
    std::cout << "[medido] " << label << ": plano=" << r.schemeName
              << " | parking AC=" << (r.parkingReadable ? std::to_string(r.parkingAc) : "ilegivel")
              << "% DC=" << (r.parkingReadable ? std::to_string(r.parkingDc) : "ilegivel")
              << "% | throttle AC=" << (r.throttleReadable ? std::to_string(r.minAc) + "-" + std::to_string(r.maxAc) : "ilegivel")
              << "% DC=" << (r.throttleReadable ? std::to_string(r.minDc) + "-" + std::to_string(r.maxDc) : "ilegivel") << "%" << std::endl;
}

// Devolve TODOS os planos tocados ao estado em que foram encontrados, inclusive quando um check falha pelo meio.
class MachineGuard {
public:
    MachineGuard(PowerManager& pm, std::vector<Reality> saved, std::string originalActiveGuid)
        : m_pm(pm), m_saved(std::move(saved)), m_originalActiveGuid(std::move(originalActiveGuid)) {}

    ~MachineGuard() {
        if (!m_originalActiveGuid.empty()) m_pm.SetActiveScheme(m_originalActiveGuid);

        for (const auto& s : m_saved) {
            // Parking e gravado junto em AC e DC: so devolve o valor quando a maquina era simetrica,
            // porque restaurar AC nao recuperaria um DC diferente que la estava.
            if (s.parkingReadable && s.parkingAc == s.parkingDc) m_pm.SetCoreParkingForScheme(s.schemeGuid, s.parkingAc);
            if (s.throttleReadable) {
                m_pm.WriteSchemeProcessorThrottle(s.schemeGuid, s.minAc, s.maxAc, s.minDc, s.maxDc);
            }
        }

        std::cout << "[cleanup] restauracao plano a plano:" << std::endl;
        for (const auto& s : m_saved) {
            Reality now = ReadRealityOf(m_pm, s.schemeGuid, s.schemeName);
            const bool parkingOk = !s.parkingReadable || s.parkingAc != s.parkingDc || now.parkingAc == s.parkingAc;
            const bool throttleOk = !s.throttleReadable ||
                                    (now.minAc == s.minAc && now.maxAc == s.maxAc &&
                                     now.minDc == s.minDc && now.maxDc == s.maxDc);
            std::cout << "          " << s.schemeName << " -> parking "
                      << (now.parkingReadable ? std::to_string(now.parkingAc) + "%" : "ilegivel")
                      << " (era " << (s.parkingReadable ? std::to_string(s.parkingAc) + "%" : "ilegivel") << ") "
                      << (parkingOk ? "[ok]" : "[PENDENCIA]")
                      << " | throttle " << (now.throttleReadable ? std::to_string(now.minAc) + "-" + std::to_string(now.maxAc) + "%" : "ilegivel")
                      << " (era " << (s.throttleReadable ? std::to_string(s.minAc) + "-" + std::to_string(s.maxAc) + "%" : "ilegivel") << ") "
                      << (throttleOk ? "[ok]" : "[PENDENCIA]") << std::endl;
        }

        Reality active = ReadActiveReality(m_pm);
        std::cout << "[cleanup] plano ativo final: " << active.schemeName
                  << (active.schemeGuid == m_originalActiveGuid ? " [igual ao inicial]" : " [DIFERENTE DO INICIAL]") << std::endl;
    }
private:
    PowerManager& m_pm;
    std::vector<Reality> m_saved;
    std::string m_originalActiveGuid;
};

} // namespace

int main() {
    const std::string dbPath = "test_mode_guardian.db";
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);

    auto& db = DatabaseManager::Instance();
    if (!db.Open(dbPath)) {
        std::cout << "[SKIP] banco de dados indisponivel; nada foi verificado (NOT_RUN)." << std::endl;
        return 0;
    }

    PowerManager power;
    power.Initialize();

    Reality before = ReadActiveReality(power);
    PrintReality("plano ativo encontrado", before);
    if (!before.parkingReadable || !before.throttleReadable || before.schemeGuid.empty()) {
        std::cout << "[SKIP] plano/parking/throttle ilegiveis; o corredor de modo nao pode ser medido (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }
    if (before.parkingAc != before.parkingDc) {
        std::cout << "[SKIP] core parking assimetrico no plano ativo (AC=" << before.parkingAc << "% DC=" << before.parkingDc
                  << "%): a escrita do modo colapsaria o DC (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }

    // Retrato inicial de todos os planos que o teste pode alcançar, para a restauracao nao dever nada a ninguem.
    const std::string ecoGuid = power.FindEcoScheme();
    const std::string highGuid = power.FindHighPerformanceScheme();
    const std::string balancedGuid = power.FindBalancedScheme();
    std::vector<Reality> saved;
    std::set<std::string> seen;
    for (const auto& pair : { std::make_pair(before.schemeGuid, before.schemeName),
                              std::make_pair(ecoGuid, std::string("Economico")),
                              std::make_pair(highGuid, std::string("Alto desempenho")),
                              std::make_pair(balancedGuid, std::string("Equilibrado")) }) {
        if (pair.first.empty() || seen.count(pair.first)) continue;
        seen.insert(pair.first);
        saved.push_back(ReadRealityOf(power, pair.first, pair.second));
    }

    MachineGuard guard(power, saved, before.schemeGuid);
    UnifiedSettingsModel::Instance().enableOrchestrator = true;

    // O corredor declarado na tela de configuracoes avancadas passa a ser a lei do modo.
    auto& adv = ModuleAdvancedSettings::Instance();
    adv.coreParking.minCoresActiveAC = 10;
    adv.cpuMinMax.ecoPlan.minPercentAC = 5;
    adv.cpuMinMax.ecoPlan.maxPercentAC = 80;
    const uint32_t ecoFloor = adv.coreParking.minCoresActiveAC;
    const uint32_t ecoCeiling = adv.cpuMinMax.ecoPlan.maxPercentAC;

    std::string msg;
    ModeCoordinator::Instance().Initialize();
    bool ecoOn = ModeCoordinator::Instance().SetDeepEcoMode(true, msg);
    std::cout << "[info] SetDeepEcoMode(true): " << (ecoOn ? "ok" : "recusado") << " - " << msg << std::endl;
    if (!ecoOn) {
        std::cout << "[SKIP] a Economia Maxima nao pode ser ativada nesta maquina (NOT_RUN)." << std::endl;
        db.Close();
        std::filesystem::remove(dbPath, ec);
        return 0;
    }
    Check(ModeCoordinator::Instance().GetCurrentMode() == SystemOperatingMode::MaxEconomy,
          "modo declarado da aplicacao = Economia Maxima");
    PrintReality("estado dentro da Economia Maxima", ReadActiveReality(power));

    // 1. Deriva hostil: outro agente sobe nucleos, sobe o teto de CPU e sai do plano economico.
    Reality inEco = ReadActiveReality(power);
    power.SetCoreParking(100);
    if (inEco.throttleReadable) {
        power.WriteSchemeProcessorThrottle(inEco.schemeGuid, inEco.minAc, 100, inEco.minDc, inEco.maxDc);
    }
    const bool schemeDriftApplied = !highGuid.empty() && highGuid != inEco.schemeGuid && power.SetActiveScheme(highGuid);
    PrintReality("estado apos a deriva injetada", ReadActiveReality(power));
    Check(ReadActiveReality(power).parkingReadable, "a leitura de volta do plano derivado funciona");

    // 2. Alta demanda declarada: em Eco rigido isso nao pode virar escalada de consumo.
    AdaptiveEngine::Instance().Initialize();
    AdaptiveDecision dec = AdaptiveEngine::Instance().ProcessTick(MakeSnap(92.0, 61.0), power, ServiceManager::Instance());
    Reality fixed = ReadActiveReality(power);
    PrintReality("estado apos o tick do guardiao", fixed);
    std::cout << "[info] acao=" << AdaptiveActionTypeToString(dec.action) << " alvo=" << dec.targetResource << "="
              << dec.targetValue << " | " << dec.justification << std::endl;

    Check(fixed.parkingReadable && fixed.parkingAc <= ecoFloor,
          "guardiao devolveu os nucleos ao piso economico de " + std::to_string(ecoFloor) + "% (medido: " +
          std::to_string(fixed.parkingAc) + "%)");
    Check(fixed.throttleReadable && fixed.maxAc <= ecoCeiling,
          "teto de CPU do plano ativo nao passou dos " + std::to_string(ecoCeiling) + "% da Economia (medido: " +
          std::to_string(fixed.maxAc) + "%)");
    if (schemeDriftApplied) {
        Check(fixed.schemeGuid == inEco.schemeGuid, "plano ativo voltou sozinho para o plano economico da Economia Maxima");
    } else {
        std::cout << "[SKIP] nao houve dois planos distinguiveis nesta maquina para provar a volta ao plano economico (NOT_RUN)." << std::endl;
    }
    Check(dec.targetResource != "CORE_PARKING" || dec.targetValue == std::to_string(ecoFloor),
          "a alta demanda nao gerou escalada de nucleos dentro da Economia Maxima");

    // 3. Falha segura termica: arrefecer nunca pode ABRIR nucleos acima do que ja esta ativo.
    AdaptiveDecision thermal = AdaptiveEngine::Instance().ProcessTick(MakeSnap(95.0, 95.0), power, ServiceManager::Instance());
    Reality thermalState = ReadActiveReality(power);
    PrintReality("estado apos a subida termica simulada", thermalState);
    std::cout << "[info] termica: acao=" << AdaptiveActionTypeToString(thermal.action) << " alvo="
              << thermal.targetResource << "=" << thermal.targetValue << " override="
              << (thermal.safetyOverride ? "sim" : "nao") << " | " << thermal.justification << std::endl;
    Check(thermal.safetyOverride, "a telemetria termica critica virou override de seguranca");
    Check(thermalState.parkingReadable && thermalState.parkingAc <= ecoFloor,
          "a seguranca termica nao abriu nucleos acima do piso economico (medido: " + std::to_string(thermalState.parkingAc) + "%)");

    // 3b. O interruptor "Inteligente" da linha Planos de energia e uma lei: desligado, o motor nao pode
    // propor troca de plano nenhuma, por mais que o uso peca outra banda. Seis ticks a 92% de CPU.
    UnifiedSettingsModel::Instance().levelPowerPlans.autoSmartMode = false;
    bool planoPropostoSemInteligente = false;
    for (int volta = 0; volta < 6; ++volta) {
        AdaptiveDecision probe = AdaptiveEngine::Instance().ProcessTick(MakeSnap(92.0, 61.0), power, ServiceManager::Instance());
        if (probe.targetResource == "POWER_SCHEME") planoPropostoSemInteligente = true;
    }
    Check(!planoPropostoSemInteligente,
          "com 'Inteligente' desligado em Planos de energia o motor nao propoe troca de plano (6 ticks a 92%)");

    // 4. A outra metade da regra: um app em foco que EXIGE desempenho fura o teto economico de proposito.
    // O cooldown de 20s entre escritas diretas e uma regra do programa: o teste espera por ela, nao a contorna -
    // por isso a janela de espera e maior que o cooldown, senao nada pode ser escrito e nada e provado.
    // Como a escalada so vale para o app que esta EM FOCO no tick, o objetivo e re-declarado a cada volta:
    // guardar o appId de uma vez elegia um app que ja tinha saido da janela (falsa falha medida antes).
    // Com "Inteligente" ligado em Planos de energia, a banda de uso passa a mandar no plano.
    UnifiedSettingsModel::Instance().levelPowerPlans.autoSmartMode = true;
    const std::string firstFocusAppId = AdaptiveEngine::Instance().GetCurrentProfile().appId;
    std::cout << "[info] app em foco no inicio da escalada: '" << firstFocusAppId << "'" << std::endl;

    AdaptiveDecision escalated;
    Reality afterEscalation;
    std::string focusAppId = firstFocusAppId;
    bool objectiveSet = false;
    bool nucleosSubiram = false;
    bool planoMudou = false;
    const int maxVoltas = 45;
    for (int volta = 1; volta <= maxVoltas; ++volta) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        focusAppId = AdaptiveEngine::Instance().GetCurrentProfile().appId;
        objectiveSet = AppProfileManager::Instance().UpdateObjective(focusAppId, AppObjective::MaxPerformance);
        escalated = AdaptiveEngine::Instance().ProcessTick(MakeSnap(92.0, 61.0), power, ServiceManager::Instance());
        afterEscalation = ReadActiveReality(power);
        if (afterEscalation.parkingReadable && afterEscalation.parkingAc > ecoFloor) nucleosSubiram = true;
        if (!afterEscalation.schemeGuid.empty() && afterEscalation.schemeGuid != inEco.schemeGuid) planoMudou = true;
        std::cout << "[volta " << volta << "] foco='" << focusAppId << "' parking=" << afterEscalation.parkingAc
                  << "% plano=" << afterEscalation.schemeName << " acao=" << AdaptiveActionTypeToString(escalated.action)
                  << " alvo=" << escalated.targetResource << "=" << escalated.targetValue
                  << ((nucleosSubiram && planoMudou) ? " <- corredor completo" : "") << std::endl;
        if (nucleosSubiram && planoMudou) break;
    }
    PrintReality("estado com app de desempenho em foco", afterEscalation);
    std::cout << "[info] escalada: acao=" << AdaptiveActionTypeToString(escalated.action) << " alvo="
              << escalated.targetResource << "=" << escalated.targetValue << " | " << escalated.justification << std::endl;
    if (!objectiveSet) {
        std::cout << "[SKIP] o objetivo do app em foco nao pode ser gravado; a escalada autorizada nao foi verificada (NOT_RUN)." << std::endl;
    } else {
        Check(nucleosSubiram,
              "com app de desempenho em foco ('" + focusAppId + "') os nucleos subiram acima do piso economico de " +
              std::to_string(ecoFloor) + "% (ultimo parking medido: " + std::to_string(afterEscalation.parkingAc) + "%)");
        Check(planoMudou,
              "a banda 79-100% com escalada autorizada trocou o plano: de " + inEco.schemeName + " para " +
              afterEscalation.schemeName + " (medido)");
        Check(escalated.targetResource != "CORE_PARKING" || escalated.targetValue != std::to_string(ecoFloor),
              "a decisao registrou a escalada como acao do programa, nao como deriva");
    }

    // 5. Sair da Economia restaura o que a maquina era antes de o modo ser ligado.
    bool ecoOff = ModeCoordinator::Instance().SetDeepEcoMode(false, msg);
    std::cout << "[info] SetDeepEcoMode(false): " << (ecoOff ? "ok" : "recusado") << " - " << msg << std::endl;
    Reality restored = ReadActiveReality(power);
    PrintReality("estado apos desligar a Economia Maxima", restored);
    Check(ModeCoordinator::Instance().GetCurrentMode() != SystemOperatingMode::MaxEconomy,
          "o modo declarou saida da Economia Maxima");
    Check(restored.parkingAc == before.parkingAc,
          "sair da Economia devolve o parking REAL que a maquina tinha (" + std::to_string(before.parkingAc) +
          "%), nao o valor atual da tela de configuracoes (medido: " + std::to_string(restored.parkingAc) + "%)");
    Check(!restored.throttleReadable || (restored.minAc == before.minAc && restored.maxAc == before.maxAc),
          "sair da Economia devolve o throttle REAL do plano de origem (medido: " + std::to_string(restored.minAc) +
          "-" + std::to_string(restored.maxAc) + "%, era " + std::to_string(before.minAc) + "-" + std::to_string(before.maxAc) + "%)");
    Check(db.GetPendingRecoveryEntries().empty(),
          "nenhuma escrita do guardiao ficou pendente: o proximo boot nao vai desfazer politica vigente");

    std::cout << (g_failures == 0 ? "GUARDIAO DE MODO: CORREDOR DE ECONOMIA VERIFICADO" : "GUARDIAO DE MODO: FALHAS DETECTADAS")
              << " (" << g_failures << " falha(s))" << std::endl;

    db.Close();
    std::filesystem::remove(dbPath, ec);
    return g_failures == 0 ? 0 : 1;
}
