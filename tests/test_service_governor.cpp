// Teste do reconciliador de servicos: as provas de retorno, a matriz de niveis e as travas.
// Nada aqui pausa um servico real: o que o teste prova e a DECISAO (e as recusas de nomes criticos),
// porque encostar no SCM de verdade e trabalho para a evidencia do app dirigido, nao do teste de unidade.
#include "platform/windows/ServiceManager.h"
#include "core/DatabaseManager.h"
#include "core/ModuleAdvancedSettings.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using opitcore::ServiceCatalogEntry;
using opitcore::ServiceManager;
using opitcore::ServiceSweepInput;
using opitcore::ServiceWake;

static int g_failures = 0;

#define CHECK(cond, msg)                                                                            \
    do {                                                                                            \
        if (cond) {                                                                                 \
            std::printf("  ok   %s\n", msg);                                                        \
        } else {                                                                                    \
            std::printf("  FALHOU %s\n", msg);                                                      \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (0)

static ServiceSweepInput MakeInput(uint64_t nowMs, uint64_t idleMs, bool game, bool eco, double cpu) {
    ServiceSweepInput in;
    in.nowMs = nowMs;
    in.userIdleMs = idleMs;
    in.idlePauseAfterMs = 240000;
    in.isGameRunning = game;
    in.isBatteryOrEco = eco;
    in.cpuUsagePercent = cpu;
    in.autonomyLevel = 1;
    in.highCpuThreshold = 85;
    in.minGainMB = 8;
    in.maintenanceHour = 3;
    in.maintenanceMinutes = 30;
    in.serviceLevel = "min";
    return in;
}

int main() {
    std::printf("== test_service_governor ==\n");

    // 1. Catalogo: nome minusculo, prova de retorno nunca nula e ninguem invasivel no catalogo.
    for (const auto& e : ServiceManager::Catalog()) {
        std::string name(e.name);
        CHECK(name == opitcore::StringUtils::ToLower(name), ("catalogo em minusculo: " + name).c_str());
        CHECK(e.wake != ServiceWake::None, ("prova de retorno existe: " + name).c_str());
        CHECK(e.minAutonomy >= 0 && e.minAutonomy <= 2, ("autonomia 0..2: " + name).c_str());
        CHECK(!ServiceManager::Instance().IsCriticalService(name), ("fora do bloco invasivel: " + name).c_str());
    }

    // 2. Prova de retorno por nome.
    CHECK(ServiceManager::RequiredWakeFor("WSearch") == ServiceWake::OnUserActivity,
          "WSearch volta quando a pessoa volta a usar o PC");
    CHECK(ServiceManager::RequiredWakeFor("DiagTrack") == ServiceWake::OnMaintenanceWindow,
          "Telemetria volta na janela de manutencao");
    CHECK(ServiceManager::RequiredWakeFor("gupdate") == ServiceWake::OnUserActivity,
          "atualizador de terceiro volta com a pessoa na maquina");
    CHECK(ServiceManager::RequiredWakeFor(" servico-inventado ") == ServiceWake::None,
          "nome sem prova conhecida NAO entra na automacao");

    // 3. A prova vale ou nao vale agora (decisao reproduzivel, sem relogio do sistema).
    {
        ServiceSweepInput in = MakeInput(1000, 300000, false, false, 10.0); // ociosa ha 5 min
        CHECK(!ServiceManager::ShouldWakeNow(ServiceWake::OnUserActivity, in, false),
              "ociosa ha 5 min: a prova de atividade NAO vale, pode ficar parado");
        in.userIdleMs = 60000; // voltou a usar ha 1 min
        CHECK(ServiceManager::ShouldWakeNow(ServiceWake::OnUserActivity, in, false),
              "pessoa de volta: a prova vale, servico acorda");
        CHECK(ServiceManager::ShouldWakeNow(ServiceWake::OnDemandProcess, in, true),
              "aplicativo associado aberto: prova de demanda vale");
        CHECK(!ServiceManager::ShouldWakeNow(ServiceWake::OnDemandProcess, in, false),
              "sem aplicativo associado: prova de demanda nao vale");
        CHECK(ServiceManager::ShouldWakeNow(ServiceWake::None, in, false),
              "sem prova nenhuma: o servico NAO pode ficar parado por decisao nossa");
        CHECK(!ServiceManager::ShouldWakeNow(ServiceWake::OnTriggerEnd, in, false),
              "gatilho ativo: quem manda e o proprio gatilho, nao a prova");
    }

    // 4. Janela de manutencao: deterministica em torno da hora atual.
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        CHECK(ServiceManager::InMaintenanceWindow(st.wHour, 180),
              "janela de 180 min a partir da hora atual esta aberta agora");
        const int farHour = (st.wHour + 12) % 24;
        CHECK(!ServiceManager::InMaintenanceWindow(farHour, 5),
              "janela de 5 min daqui a 12 h esta fechada agora");
        // Janela que cruza a meia-noite: come 23:00 com 180 min cobre qualquer hora entre 23:00 e 02:00.
        bool covered = (st.wHour >= 23) || (st.wHour <= 1);
        CHECK(ServiceManager::InMaintenanceWindow(23, 180) == covered,
              "janela de madrugada cobre exatamente 23:00-02:00");
    }

    // 5. Matriz min/med/max/ultra (a mesma que o rodape da janela promete).
    CHECK(!ServiceManager::LevelMatrixIncludes("min", "wsearch", false, false), "nivel min nao encosta em ninguem");
    CHECK(ServiceManager::LevelMatrixIncludes("med", "wsearch", false, false), "nivel med pausa indexacao");
    CHECK(ServiceManager::LevelMatrixIncludes("med", "wsearch", true, true),
          "com 'so em jogos', a matriz age com jogo aberto");
    CHECK(!ServiceManager::LevelMatrixIncludes("med", "wsearch", true, false),
          "com 'so em jogos', a matriz fica de maos vazias sem jogo");
    CHECK(ServiceManager::LevelMatrixIncludes("max", "spooler", false, false), "nivel max soma o Spooler");
    CHECK(!ServiceManager::LevelMatrixIncludes("med", "spooler", false, false), "nivel med nao toca no Spooler");
    CHECK(ServiceManager::LevelMatrixIncludes("ultra", "diagtrack", false, false), "nivel ultra soma telemetria");
    CHECK(ServiceManager::LevelMatrixIncludes("ultra", "sysmain", false, false), "nivel ultra soma SysMain");
    CHECK(!ServiceManager::LevelMatrixIncludes("ultra", "audiosrv", false, false),
          "a matriz nunca inclui som, rede ou qualquer coisa fora da lista dela");

    // 6. Demanda por aplicativo, com a lista injetada (sem enumerar processos no teste).
    CHECK(ServiceManager::Instance().IsServiceNeededByActiveApplication("Spooler", {"chrome.exe", "printui.exe"}),
          "fila de impressao aberta preserva o Spooler");
    CHECK(ServiceManager::Instance().IsServiceNeededByActiveApplication("gupdate", {"chrome.exe"}),
          "Chrome aberto preserva o atualizador dele");
    CHECK(!ServiceManager::Instance().IsServiceNeededByActiveApplication("Spooler", {"chrome.exe", "notepad.exe"}),
          "sem app de impressao aberto, o Spooler nao e preservado por demanda");
    CHECK(!ServiceManager::Instance().IsServiceNeededByActiveApplication("servico-inventado", {"chrome.exe"}),
          "nome sem mapeamento de demanda nao e preservado por demanda");

    // 7. Guardas de pausa disparam ANTES de qualquer escrita no SCM (nomes criticos e exclusoes).
    CHECK(!ServiceManager::Instance().PauseOrStopService("RpcSs"),
          "RpcSs e recusado sem tocar no SCM");
    CHECK(!ServiceManager::Instance().PauseOrStopService("LanmanWorkstation"),
          "LanmanWorkstation e recusado sem tocar no SCM");
    {
        auto& adv = opitcore::ModuleAdvancedSettings::Instance();
        std::string saved = adv.services.customUserExcludedServices;
        adv.services.customUserExcludedServices = "spooler";
        CHECK(!ServiceManager::Instance().PauseOrStopService("Spooler"),
              "exclusao da pessoa recusa a pausa antes do SCM");
        adv.services.customUserExcludedServices = saved;
        CHECK(adv.services.customUserExcludedServices == saved, "exclusao da pessoa restaurada apos o teste");
    }

    // 8. Estado inicial honesto: o teste nao pausou nada de verdade.
    CHECK(ServiceManager::Instance().GetPausedServicesCount() == 0,
          "nenhum servico real foi tocado pelo teste");
    CHECK(ServiceManager::Instance().Stats().paused == 0, "estatistica de pausas real zerada");

    // 9. Leitura real do SCM: enumerar, classificar e provar que cada linha carrega a prova de retorno.
    {
        auto all = ServiceManager::Instance().EnumerateAllServices();
        CHECK(all.size() >= 50, ("SCM enumerado: " + std::to_string(all.size()) + " servicos").c_str());
        size_t withWake = 0, critical = 0, named = 0;
        for (const auto& s : all) {
            if (!s.name.empty()) ++named;
            if (!s.wake.empty()) ++withWake;
            if (s.isCritical) {
                ++critical;
                CHECK(s.category == "Sistema (Critico)",
                      ("critico classificado: " + s.name).c_str());
            }
        }
        CHECK(named == all.size(), "todo servico enumerado tem nome");
        CHECK(withWake == all.size(), "todo servico tem uma frase de retorno ou uma recusa explicada");
        CHECK(critical >= 5, ("bloco invasivel visivel na enumeracao: " + std::to_string(critical) + " nomes").c_str());

        // Prova de hospedeiro exclusivo num nome conhecido, sem afirmar o estado da maquina.
        uint32_t pid = 0;
        uint64_t mb = 0;
        if (ServiceManager::HostProcessExclusive("WSearch", &pid, &mb)) {
            CHECK(pid > 0 && mb > 0, "hospedeiro exclusivo medido tem PID e memoria");
        } else {
            std::printf("  NOT_RUN hospedeiro do WSearch (servico parado ou compartilhado nesta maquina)\n");
        }
        (void)ServiceManager::PrintJobsPending(); // leitura segura da fila; sem estado para afirmar
    }

    // 10. Regressao do ciclo rebentado: a trava de 15 min depois de uma volta por demanda tem de RECUSAR
    //     (codigo 3) e nao lanca exceccao. Antes, a decisao era tomada dentro do ambito que ja segurava
    //     m_mutex e SetBlockReason voltava a pedir o mesmo mutex, que nao e recursivo:
    //     resource_deadlock_would_ocour engolia o ciclo inteiro a cada amostra (medido no app dirigido:
    //     231 erros em 2 h 49, todos entre as sondas RL:c1b e RL:c1c). Aqui nada escreve no SCM: a recusa
    //     vem antes de abrir qualquer handle, e o nome escolhido vai na lista de exclusoes da pessoa para
    //     o caso improvavel de a rota cair direto na pausa.
    {
        const std::string testDb = "test_opitcore_service_governor.db";
        std::error_code ec;
        if (std::filesystem::exists(testDb)) std::filesystem::remove(testDb, ec);
        auto& db = opitcore::DatabaseManager::Instance();
        if (!db.Open(testDb)) {
            std::printf("  NOT_RUN base de teste para o diario de servicos\n");
        } else {
            std::string nome;
            for (const char* c : {"Dnscache", "Schedule", "Themes", "Eventlog"}) {
                if (ServiceManager::Instance().IsServiceRunning(c)) {
                    nome = c;
                    break;
                }
            }
            if (nome.empty()) {
                std::printf("  NOT_RUN nenhum nome de guarda em execucao nesta maquina\n");
            } else {
                const std::string chave = opitcore::StringUtils::ToLower(nome);
                auto& adv = opitcore::ModuleAdvancedSettings::Instance();
                const std::string saved = adv.services.customUserExcludedServices;
                adv.services.customUserExcludedServices = chave;

                // Diario semeado: registo NOSSO, com prova de demanda e nao declarado - e o caminho que
                // devolve o nome por demanda e lhe carrega a trava de 15 min no mesmo ciclo.
                const std::string diario = chave + "\t0\t0\t0\t" +
                                           std::to_string(static_cast<int>(ServiceWake::OnDemandProcess)) +
                                           "\t0\tsemeado pelo teste\n";
                CHECK(db.SetSetting("svc_auto_journal_v1", diario), "diario de servicos semeado");
                ServiceManager::Instance().SetServiceRule(chave, "always_paused");

                ServiceSweepInput in = MakeInput(1000000, 10, false, false, 5.0);
                in.minGainMB = 0;
                std::vector<std::string> procs;
                in.runningProcesses = &procs;

                bool lancou = false;
                std::string oQue;
                try {
                    ServiceManager::Instance().RunAutomaticCycle(in);
                } catch (const std::exception& e) {
                    lancou = true;
                    oQue = std::string(" (o que: ") + e.what() + ")";
                }
                CHECK(!lancou, ("ciclo de servicos sobrevive a trava de 15 min" + oQue).c_str());
                const auto st = ServiceManager::Instance().Stats();
                CHECK(st.skippedLockout >= 1, "a recusa conta-se no ciclo, nao no excecao");
                CHECK(ServiceManager::Instance().BlockReasonFor(chave) == "devolvido por demanda ha menos de 15 min",
                      "o motivo da recusa ficou no diario");
                CHECK(ServiceManager::Instance().GetPausedServicesCount() == 0,
                      "o teste continua sem ter tocado nenhum servico real");

                ServiceManager::Instance().SetServiceRule(chave, "default");
                db.SetSetting("svc_auto_journal_v1", "");
                db.Close();
                adv.services.customUserExcludedServices = saved;
                std::filesystem::remove(testDb, ec);
                std::printf("  (nome usado nesta maquina: %s)\n", nome.c_str());
            }
        }
    }

    if (g_failures == 0) {
        std::printf("SUCESSO (reconciliador de servicos: provas de retorno, matriz e guardas medidas)\n");
        return 0;
    }
    std::printf("FALHAS: %d\n", g_failures);
    return 1;
}
