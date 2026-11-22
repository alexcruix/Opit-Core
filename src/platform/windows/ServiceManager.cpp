#include "ServiceManager.h"
#include "ProcessManager.h"
#include "core/Logger.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/NotificationManager.h"
#include "core/UnifiedSettingsModel.h"
#include "core/DatabaseManager.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <winsvc.h>
#include <psapi.h>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <algorithm>

namespace opitcore {

// Definida mais abaixo, usada pelo catalogo de automacao.
static bool IsKnownNonEssentialThirdParty(const std::string& lower);

static uint64_t NowSteadyMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

namespace {

constexpr uint64_t kStopCooldownMs = 300000;   // 5 min por servico: backstop contra martelar o SCM
constexpr uint64_t kWakeLockoutMs = 900000;    // 15 min sem encostar de novo depois de devolver por demanda
constexpr uint64_t kDemandWindowMs = 3600000;  // janela de contagem das voltas
constexpr int kMaxDemandWakes = 3;             // 3 voltas em 1 h = nome nao automatizavel nesta sessao
constexpr uint64_t kHostCacheMs = 15000;       // o SCM e reenumerado no maximo a cada 15 s

std::string LowerKey(const std::string& s) { return StringUtils::ToLower(StringUtils::Trim(s)); }

// Ocio medido da sessao interativa. Sem medicao devolvemos 0: na duda nada e "ocioso" e nenhum servico
// e tocado. O contador de 32 bits vira a cada ~49,7 dias; se a virada cair no meio, tratamos como 0.
uint64_t LastInputIdleMs() {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) return 0;
    const uint32_t nowTicks = GetTickCount();
    if (nowTicks < lii.dwTime) return 0;
    return static_cast<uint64_t>(nowTicks - lii.dwTime);
}

bool SendServiceControl(const std::string& name, DWORD control, DWORD access, DWORD* lastErrorOut = nullptr) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    std::wstring wName = StringUtils::ToWide(name);
    SC_HANDLE svc = OpenServiceW(scm, wName.c_str(), access | SERVICE_QUERY_STATUS);
    bool ok = false;
    if (svc) {
        SERVICE_STATUS st{};
        ok = ControlService(svc, control, &st) != FALSE;
        if (!ok && lastErrorOut) *lastErrorOut = GetLastError();
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool StartServiceByName(const std::string& name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    std::wstring wName = StringUtils::ToWide(name);
    SC_HANDLE svc = OpenServiceW(scm, wName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    bool ok = false;
    if (svc) {
        ok = (StartServiceW(svc, 0, nullptr) != FALSE) || (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

} // namespace

const char* ServiceWakeToString(ServiceWake w) {
    switch (w) {
        case ServiceWake::OnUserActivity: return "volta quando voce volta a usar o PC";
        case ServiceWake::OnDemandProcess: return "volta quando o aplicativo abre";
        case ServiceWake::OnMaintenanceWindow: return "volta na janela diaria de manutencao";
        case ServiceWake::OnTriggerEnd: return "volta quando o gatilho acaba";
        case ServiceWake::None:
        default: return "sem retorno garantido";
    }
}

// O que a automacao encosta, com que prova de retorno e a partir de que autonomia. Nada aqui e
// invasivel: os nomes do bloco critico (rpcss, lsass, winmgmt, WlanSvc, audiosrv...) nem aparecem.
const std::vector<ServiceCatalogEntry>& ServiceManager::Catalog() {
    static const std::vector<ServiceCatalogEntry> s_catalog = {
        {"wsearch", "Windows Search (indexacao)", ServiceWake::OnUserActivity, 1,
         "a busca do Explorador reabre o indice sozinha no primeiro clique"},
        {"spooler", "Spooler de impressao", ServiceWake::OnUserActivity, 1,
         "abrir a fila de impressao liga de novo; nunca pausamos com trabalho pendurado"},
        {"sysmain", "SysMain (pre-carga)", ServiceWake::OnUserActivity, 2,
         "em SSD o ganho e pequeno, por isso so na autonomia maxima"},
        {"diagtrack", "Telemetria do Windows", ServiceWake::OnMaintenanceWindow, 1,
         "volta na janela diaria para enviar o que ficou acumulado"},
        {"dmwappushservice", "Rastreamento WAP", ServiceWake::OnMaintenanceWindow, 1, ""},
        {"wisvc", "Feedback do Windows", ServiceWake::OnMaintenanceWindow, 2, ""},
        {"wuauserv", "Windows Update", ServiceWake::OnMaintenanceWindow, 2,
         "so na autonomia maxima: voltar na janela diaria ou quando a pessoa abre o Update"},
        {"bits", "Transferencia em 2o plano", ServiceWake::OnMaintenanceWindow, 2, ""},
        {"pcasvc", "Assistente de compatibilidade", ServiceWake::OnUserActivity, 2, ""},
        {"mapsbroker", "Plataforma de dispositivos conectados", ServiceWake::OnUserActivity, 2, ""},
    };
    return s_catalog;
}

ServiceWake ServiceManager::RequiredWakeFor(const std::string& serviceName) {
    const std::string key = LowerKey(serviceName);
    for (const auto& e : Catalog()) {
        if (key == e.name) return e.wake;
    }
    if (IsKnownNonEssentialThirdParty(key)) return ServiceWake::OnUserActivity;
    return ServiceWake::None;
}

bool ServiceManager::IsUnautomatable(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_unautomatable.find(LowerKey(serviceName)) != m_unautomatable.end();
}

std::string ServiceManager::BlockReasonFor(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    auto it = m_blockReason.find(LowerKey(serviceName));
    return it == m_blockReason.end() ? std::string() : it->second;
}

void ServiceManager::SetBlockReason(const std::string& serviceName, const std::string& reason, uint64_t) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blockReason[LowerKey(serviceName)] = reason;
}

// --- Provas de retorno -----------------------------------------------------------------------------
// Cada uma e uma leitura do mundo, nao uma promessa: se a prova nao vale agora, o servico e devolvido.
bool ServiceManager::ShouldWakeNow(ServiceWake w, const ServiceSweepInput& in, bool demandProcessRunning) {
    switch (w) {
        case ServiceWake::OnUserActivity:
            return in.userIdleMs < in.idlePauseAfterMs;
        case ServiceWake::OnDemandProcess:
            return demandProcessRunning;
        case ServiceWake::OnMaintenanceWindow:
            return !InMaintenanceWindow(in.maintenanceHour, in.maintenanceMinutes);
        case ServiceWake::OnTriggerEnd:
            return false; // quem poe no desejado ja checou o gatilho: while ativo, fica parado
        case ServiceWake::None:
        default:
            return true; // sem prova nenhuma: nao pode ficar parado por nossa decisao
    }
}

bool ServiceManager::InMaintenanceWindow(int hour, int minuteSpan) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const int nowMin = static_cast<int>(st.wHour) * 60 + static_cast<int>(st.wMinute);
    const int startMin = std::clamp(hour, 0, 23) * 60;
    const int span = std::max(5, minuteSpan);
    int delta = nowMin - startMin;
    if (delta < 0) delta += 24 * 60; // janela que cruza a meia-noite
    return delta < span;
}

// Ha .SPL/.SHD na fila do spooler: sinal de que tem trabalho esperando impressora.
bool ServiceManager::PrintJobsPending() {
    wchar_t winDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(winDir, MAX_PATH) == 0) return false;
    std::wstring pattern = std::wstring(winDir) + L"\\System32\\spool\\PRINTERS\\*.SPL";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        return true;
    }
    pattern = std::wstring(winDir) + L"\\System32\\spool\\PRINTERS\\*.SHD";
    h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        return true;
    }
    return false;
}

bool ServiceManager::HostProcessExclusive(const std::string& serviceName, uint32_t* pidOut,
                                          uint64_t* workingSetMBOut) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm) return false;
    DWORD needed = 0, returned = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE,
                          nullptr, 0, &needed, &returned, &resume, nullptr);
    bool exclusive = false;
    if (needed > 0) {
        std::vector<BYTE> buffer(needed);
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE,
                                  buffer.data(), needed, &needed, &returned, &resume, nullptr)) {
            auto* rows = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
            const std::string key = LowerKey(serviceName);
            uint32_t targetPid = 0;
            int share = 0;
            for (DWORD i = 0; i < returned; ++i) {
                const uint32_t pid = rows[i].ServiceStatusProcess.dwProcessId;
                if (pid == 0) continue;
                if (LowerKey(StringUtils::ToUtf8(rows[i].lpServiceName)) == key) targetPid = pid;
            }
            if (targetPid != 0) {
                for (DWORD i = 0; i < returned; ++i) {
                    if (rows[i].ServiceStatusProcess.dwProcessId == targetPid) ++share;
                }
                if (share == 1) {
                    uint64_t mb = 0;
                    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, targetPid);
                    if (h) {
                        PROCESS_MEMORY_COUNTERS pmc{};
                        pmc.cb = sizeof(pmc);
                        if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) mb = pmc.WorkingSetSize >> 20;
                        CloseHandle(h);
                    }
                    exclusive = (mb > 0);
                    if (pidOut) *pidOut = targetPid;
                    if (workingSetMBOut) *workingSetMBOut = mb;
                }
            }
        }
    }
    CloseServiceHandle(scm);
    return exclusive;
}

ServiceManager::ServiceManager() {
    LoadAllServiceRules();
}

ServiceManager& ServiceManager::Instance() {
    static ServiceManager instance;
    return instance;
}

ServiceManager::~ServiceManager() {
    RestoreModifiedServices();
}

bool ServiceManager::IsServiceRunning(const std::string& serviceName) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    std::wstring wName = StringUtils::ToWide(serviceName);
    SC_HANDLE svc = OpenServiceW(scm, wName.c_str(), SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;
    bool running = false;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
        running = (ssp.dwCurrentState == SERVICE_RUNNING);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return running;
}

static const std::set<std::string>& GetCriticalServicesSet() {
    static const std::set<std::string> s_criticalServices = {
        "rpcss", "rpceptmapper", "dcomlaunch", "lsm", "samss", "eventlog", "plugplay",
        "brokerinfrastructure", "systemeventsbroker", "coremessagingregistrar", "audiosrv",
        "audioendpointbuilder", "dhcp", "dnscache", "lanmanworkstation", "lanmanserver",
        "cryptsvc", "winmgmt", "schedule", "profsvc", "themes", "usermanager", "staterepository",
        "securityhealthservice", "windefend", "mpssvc", "steam client service", "easyanticheat",
        "beservice", "vgk", "epiconlineservices", "origin client service", "battle.net", "xblauthmanager",
        "xblgamesave", "xboxnetapisvc", "xboxgipsvc"
    };
    return s_criticalServices;
}

bool ServiceManager::IsCriticalService(const std::string& serviceName) const {
    std::string lowerKey = StringUtils::ToLower(StringUtils::Trim(serviceName));
    const auto& crit = GetCriticalServicesSet();
    return crit.find(lowerKey) != crit.end();
}

bool ServiceManager::CanServiceBeOptimized(const std::string& serviceName) const {
    if (IsCriticalService(serviceName)) return false;
    return ModuleAdvancedSettings::Instance().services.IsServiceAllowed(serviceName);
}

bool ServiceManager::IsServiceNeededByActiveApplication(const std::string& serviceName) {
    auto runningProcs = ProcessManager::EnumerateProcesses();
    std::vector<std::string> names;
    names.reserve(runningProcs.size());
    for (const auto& p : runningProcs) names.push_back(p.name);
    return IsServiceNeededByActiveApplication(serviceName, names);
}

bool ServiceManager::IsServiceNeededByActiveApplication(const std::string& serviceName,
                                                        const std::vector<std::string>& runningProcesses) {
    std::string lowerSvc = StringUtils::ToLower(StringUtils::Trim(serviceName));

    // Mapeamento Inteligente: Se os programas associados estiverem em execução, o serviço é preservado
    static const std::unordered_map<std::string, std::vector<std::string>> s_serviceToAppMap = {
        {"spooler", {"printui.exe", "splwow64.exe", "acrobat.exe", "acrord32.exe"}},
        {"nordsec-service", {"nordvpn.exe"}},
        {"nordvpn-service", {"nordvpn.exe"}},
        {"wireguard", {"wireguard.exe"}},
        {"tailscale", {"tailscale.exe", "tailscaled.exe"}},
        {"adobearmservice", {"photoshop.exe", "illustrator.exe", "premiere.exe", "afterfx.exe", "creative cloud.exe"}},
        {"adobeservice", {"photoshop.exe", "illustrator.exe", "premiere.exe", "afterfx.exe", "creative cloud.exe"}},
        {"gupdate", {"chrome.exe"}},
        {"gupdatem", {"chrome.exe"}},
        {"edgeupdate", {"msedge.exe"}},
        {"dropboxupdate", {"dropbox.exe"}},
        {"steam client service", {"steam.exe"}},
        {"easyanticheat", {"easyanticheat.exe", "easyanticheat_eos.exe"}},
        {"beservice", {"battleye.exe"}},
        {"vgk", {"vgc.exe", "valorant.exe"}}
    };

    auto it = s_serviceToAppMap.find(lowerSvc);
    if (it == s_serviceToAppMap.end()) return false;
    for (const auto& proc : runningProcesses) {
        const std::string lowerProc = StringUtils::ToLower(proc);
        for (const auto& appName : it->second) {
            if (lowerProc == appName) return true;
        }
    }
    return false;
}

bool ServiceManager::PauseOrStopService(const std::string& serviceName) {
    // Caminho manual (botao da UI, decisao do motor adaptativo, pedido direto ao agente): escolha
    // declarada da pessoa, sem prova exigida. O reconciliador nao desfaz: volta pelo botao Retomar,
    // pela restauracao de saida, ou quando o proprio Windows reacender o servico.
    return PauseOrStopService(serviceName, ServiceWake::None, "decisao manual", false, true);
}

bool ServiceManager::PauseOrStopService(const std::string& serviceName, ServiceWake wake,
                                        const std::string& reason, bool counted, bool declared) {
    std::string lowerKey = StringUtils::ToLower(StringUtils::Trim(serviceName));
    if (IsCriticalService(serviceName)) {
        SetBlockReason(lowerKey, "critico para a estabilidade do Windows", NowSteadyMs());
        return false;
    }

    const auto& adv = ModuleAdvancedSettings::Instance().services;
    if (!adv.IsServiceAllowed(serviceName)) {
        SetBlockReason(lowerKey, "fora da automacao pelas suas excecoes", NowSteadyMs());
        return false;
    }

    // Verificação de dependência com aplicativos em execução (Ideia Inteligente). No caminho do
    // reconciliador (counted) quem checou foi o ciclo, com a lista de processos compartilhada; aqui so o
    // caminho manual reenumerada, para nao pagar uma enumeracao por servico.
    if (!counted && adv.checkDependencies && IsServiceNeededByActiveApplication(serviceName)) {
        OPIT_LOG_INFO("ServiceManager: Servico '" + serviceName + "' preservado em execucao pois o aplicativo associado esta em uso.");
        SetBlockReason(lowerKey, "aplicativo associado esta aberto", NowSteadyMs());
        return false;
    }
    if (counted && lowerKey == "spooler" && PrintJobsPending()) {
        SetBlockReason(lowerKey, "tem trabalho na fila de impressao", NowSteadyMs());
        return false;
    }

    const uint64_t nowMs = NowSteadyMs();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastStopAttemptMs.find(lowerKey);
        // Cooldown de 5 minutos (300.000 ms) por servico: impede loop infinito (thrashing)
        // caso o Windows SCM / COM reative automaticamente servicos como WSearch (SearchIndexer).
        // Antes a restauracao apagava esta marca, e os tres caminhos de automacao brigavam no mesmo tick.
        if (counted && it != m_lastStopAttemptMs.end() && (nowMs - it->second) < kStopCooldownMs) {
            m_blockReason[lowerKey] = "pausado ha menos de 5 min (trava anti-vaivem)";
            return false;
        }
        auto lit = m_lastWakeMs.find(lowerKey);
        if (counted && lit != m_lastWakeMs.end() && (nowMs - lit->second) < kWakeLockoutMs) {
            m_blockReason[lowerKey] = "devolvido por demanda ha menos de 15 min";
            return false;
        }
        m_lastStopAttemptMs[lowerKey] = nowMs;
    }

    bool acceptsPause = false;
    {
        HostInfo hi;
        if (HostFromCache(serviceName, &hi)) acceptsPause = (hi.controlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0;
    }
    const bool wantTruePause = adv.allowTruePause && acceptsPause;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    std::wstring wName = StringUtils::ToWide(serviceName);
    const DWORD desiredAccess = SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS |
                                (wantTruePause ? SERVICE_PAUSE_CONTINUE : SERVICE_STOP);
    SC_HANDLE svc = OpenServiceW(scm, wName.c_str(), desiredAccess);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }

    // Verificação de dependências ativas no Windows SCM (Anti-Crash)
    if (adv.checkDependencies) {
        DWORD bytesNeeded = 0;
        DWORD count = 0;
        EnumDependentServicesW(svc, SERVICE_ACTIVE, nullptr, 0, &bytesNeeded, &count);
        if (count > 0) {
            OPIT_LOG_INFO("ServiceManager: Servico '" + serviceName + "' possui " + std::to_string(count) +
                          " dependencias ativas no Windows. Pausa cancelada para garantir estabilidade.");
            SetBlockReason(lowerKey, std::to_string(count) + " dependencias ativas", nowMs);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return false;
        }
    }

    SERVICE_STATUS status{};
    DWORD usedControl = SERVICE_CONTROL_STOP;
    BOOL ok = FALSE;
    if (wantTruePause) {
        ok = ControlService(svc, SERVICE_CONTROL_PAUSE, &status);
        if (ok) usedControl = SERVICE_CONTROL_PAUSE;
        else if (GetLastError() == ERROR_NOT_SUPPORTED) {
            // Aceita o flag no papel sem implementar o pause: ai, e so ai, o STOP e o caminho honesto.
            ok = ControlService(svc, SERVICE_CONTROL_STOP, &status);
        }
    } else {
        ok = ControlService(svc, SERVICE_CONTROL_STOP, &status);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (ok) {
        ServicePauseRecord rec;
        rec.name = serviceName;
        rec.wasRunning = true;
        rec.pausedByPauseControl = (usedControl == SERVICE_CONTROL_PAUSE);
        rec.declared = declared;
        rec.pausedAtMs = nowMs;
        rec.wake = wake;
        rec.reason = reason;
        uint32_t hostPid = 0;
        uint64_t hostMB = 0;
        if (HostProcessExclusive(serviceName, &hostPid, &hostMB)) rec.hostWorkingSetMB = hostMB;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pausedServices.insert(lowerKey);
            m_records[lowerKey] = rec;
            m_blockReason.erase(lowerKey);
        }
        SaveJournal();
        OPIT_LOG_INFO("ServiceManager: Servico " + std::string(usedControl == SERVICE_CONTROL_PAUSE ? "suspenso" : "parado") +
                      " para otimizacao: " + serviceName + " (" + reason + "; " + ServiceWakeToString(wake) +
                      "; " + std::to_string(rec.hostWorkingSetMB) + " MB liberados).");
        NotificationManager::Instance().Notify(NotificationManager::Category::Service,
                                               "Servico otimizado: " + serviceName,
                                               serviceName + " foi " +
                                               std::string(usedControl == SERVICE_CONTROL_PAUSE ? "suspenso" : "parado") +
                                               " (" + reason + "). " + std::to_string(rec.hostWorkingSetMB) +
                                               " MB liberados; volta sozinho pelo gatilho " + ServiceWakeToString(wake) + ".");
    } else {
        SetBlockReason(lowerKey, "o SCM recusou (sem privilegio ou estado transitario)", nowMs);
    }
    return (ok != 0);
}

bool ServiceManager::ResumeOrStartService(const std::string& serviceName) {
    const std::string lowerKey = StringUtils::ToLower(StringUtils::Trim(serviceName));
    bool wasTruePaused = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_records.find(lowerKey);
        if (it != m_records.end()) wasTruePaused = it->second.pausedByPauseControl;
    }

    bool ok = false;
    if (wasTruePaused) {
        // SERVICE_CONTROL_CONTINUE devolve o servico com o estado interno dele preservado; e mais rapido
        // e mais suave do que partir do zero. Se recusar, caimos para o StartServiceW classico.
        ok = SendServiceControl(serviceName, SERVICE_CONTROL_CONTINUE, SERVICE_PAUSE_CONTINUE);
    }
    if (!ok) ok = StartServiceByName(serviceName);

    if (ok) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pausedServices.erase(lowerKey);
            m_records.erase(lowerKey);
            m_blockReason.erase(lowerKey);
            // NAO apagamos m_lastStopAttemptMs: foi justamente apagar essa marca na restauracao que ligou
            // o vaivem do Spooler medido em 15 eventos em 3 minutos.
        }
        SaveJournal();
        OPIT_LOG_INFO("ServiceManager: Servico restaurado/iniciado: " + serviceName);
        NotificationManager::Instance().Notify(NotificationManager::Category::Service,
                                               "Servico de volta: " + serviceName,
                                               serviceName + " voltou a rodar (gatilho de retorno cumprido ou restauracao).");
        return true;
    }
    return false;
}

void ServiceManager::RestoreModifiedServices() {
    std::vector<std::string> toRestore;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        toRestore.assign(m_pausedServices.begin(), m_pausedServices.end());
    }
    if (toRestore.empty()) return;

    OPIT_LOG_INFO("ServiceManager: Restaurando " + std::to_string(toRestore.size()) + " servicos modificados...");
    for (const auto& svc : toRestore) {
        // Sem contagem de demanda: aqui a saida e pedida pela pessoa (fechar o programa / botao), nao por
        // um aplicativo que precisava do servico, entao nao deve consumir a trava anti-vaivem.
        WakeTracked(svc, false, NowSteadyMs());
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pausedServices.clear();
        m_records.clear();
        m_lastStopAttemptMs.clear();
    }
    SaveJournal();
}

// Matriz de Serviços do rodapé da UI: "min=off | med=indexacao | max=+Spooler | ultra=+telemetria".
// "subSmart" ligado so deixa a matriz agir com jogo aberto, como antes.
bool ServiceManager::LevelMatrixIncludes(const std::string& level, const std::string& key,
                                         bool subSmartServices, bool isGameRunning) {
    if (level == "min" || level.empty()) return false;
    if (subSmartServices && !isGameRunning) return false;
    if (key == "wsearch") return true; // med, max e ultra
    if (key == "spooler") return (level == "max" || level == "ultra");
    if (key == "diagtrack" || key == "sysmain") return (level == "ultra");
    return false;
}

ServiceSweepStats ServiceManager::Stats() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_stats;
}

void ServiceManager::ApplyServiceLevel(const std::string& level, bool isGameRunning, bool subSmartServices) {
    // Antes este metodo parava por conta propria e ainda chamava RestoreModifiedServices quando a matriz
    // ficava vazia - um dos tres atores que disputavam o mesmo servico no mesmo tick. Hoje ele so informa
    // o nivel desejado e entrega a decisao ao reconciliador, que e um por ciclo.
    RunServiceCycleFromSystem(isGameRunning, false, 0.0, level, subSmartServices);
}

// Opt-out de telemetria que sobrevive ao boot: com elevacao, guarda o start type original de cada
// servico no banco (apenas na primeira vez, para nunca sobrescrever o verdadeiro original com
// DISABLED) e marca SERVICE_DISABLED no SCM. Sem elevacao o SCM recusa (ERROR_ACCESS_DENIED) e
// resta a pausa da sessao. A reativacao restaura exatamente o start type guardado.
static bool SetTelemetryServiceBootState(const std::string& serviceName, bool disable) {
    auto& db = DatabaseManager::Instance();
    const std::string origKey = "svc_telemetry_orig_" + serviceName;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        OPIT_LOG_WARN("ServiceManager: OpenSCManager falhou (" + std::to_string(GetLastError()) +
                      "); start type de '" + serviceName + "' nao mudou.");
        return false;
    }
    std::wstring wName = StringUtils::ToWide(serviceName);
    SC_HANDLE svc = OpenServiceW(scm, wName.c_str(),
                                 SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_START);
    if (!svc) {
        OPIT_LOG_WARN("ServiceManager: OpenService('" + serviceName + "') falhou (" +
                      std::to_string(GetLastError()) + "); start type nao mudou.");
        CloseServiceHandle(scm);
        return false;
    }

    bool ok = false;
    if (disable) {
        DWORD bytesNeeded = 0;
        std::vector<BYTE> buf;
        QueryServiceConfig(svc, nullptr, 0, &bytesNeeded);
        if (bytesNeeded > 0) buf.resize(bytesNeeded);
        if (!buf.empty()) {
            auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIG*>(buf.data());
            if (QueryServiceConfig(svc, cfg, bytesNeeded, &bytesNeeded) &&
                db.GetSetting(origKey, "").empty()) {
                db.SetSetting(origKey, std::to_string(cfg->dwStartType));
                OPIT_LOG_INFO("ServiceManager: start type original de '" + serviceName + "' guardado (" +
                              std::to_string(cfg->dwStartType) + ") para restauracao exata.");
            }
        }
        if (ChangeServiceConfig(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
            ok = true;
            OPIT_LOG_INFO("ServiceManager: '" + serviceName +
                          "' marcado DISABLED no SCM (opt-out de telemetria sobrevive ao boot).");
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED) {
                OPIT_LOG_WARN("ServiceManager: sem elevacao para marcar '" + serviceName +
                              "' DISABLED; opt-out vale so nesta sessao.");
            } else {
                OPIT_LOG_WARN("ServiceManager: ChangeServiceConfig('" + serviceName + "') falhou (" +
                              std::to_string(err) + "); opt-out vale so nesta sessao.");
            }
        }
    } else {
        std::string stored = db.GetSetting(origKey, "");
        if (!stored.empty()) {
            DWORD startType = SERVICE_AUTO_START;
            try { startType = static_cast<DWORD>(std::stoul(stored)); } catch (...) { startType = SERVICE_AUTO_START; }
            if (ChangeServiceConfig(svc, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
                OPIT_LOG_INFO("ServiceManager: '" + serviceName + "' restaurado ao start type original (" +
                              stored + ").");
            } else {
                OPIT_LOG_WARN("ServiceManager: falha ao restaurar start type de '" + serviceName + "' (" +
                              std::to_string(GetLastError()) + ").");
            }
            db.SetSetting(origKey, "");
        }
        ok = true; // sem registro guardado nao ha nada a restaurar no boot; a sessao fica com ResumeOrStartService
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool ServiceManager::SetMicrosoftTelemetryDisabled(bool disable) {
    if (disable) {
        // Opt-out declarado: escolha da pessoa, sem prova de retorno exigida. O reconciliador nao
        // reavalia nem desfaz; so sai com Retomar/restauracao de saida.
        PauseOrStopService("DiagTrack", ServiceWake::None, "telemetria desativada por escolha", false, true);
        PauseOrStopService("dmwappushservice", ServiceWake::None, "telemetria desativada por escolha", false, true);

        bool bootDisabled = SetTelemetryServiceBootState("DiagTrack", true) &&
                            SetTelemetryServiceBootState("dmwappushservice", true);

        // Politica AllowTelemetry: valor original guardado para restauracao exata na reativacao.
        auto& db = DatabaseManager::Instance();
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
                            0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            DWORD cur = 0;
            DWORD size = sizeof(cur);
            std::string storedPolicy = db.GetSetting("svc_telemetry_policy_allow_orig", "");
            if (storedPolicy.empty()) {
                bool absent = (RegQueryValueExW(hKey, L"AllowTelemetry", nullptr, nullptr,
                                                reinterpret_cast<LPBYTE>(&cur), &size) != ERROR_SUCCESS);
                db.SetSetting("svc_telemetry_policy_allow_orig", absent ? "absent" : std::to_string(cur));
            }
            DWORD val = 0;
            RegSetValueExW(hKey, L"AllowTelemetry", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
            RegCloseKey(hKey);
        }
        OPIT_LOG_INFO(std::string("ServiceManager: Telemetria da Microsoft desativada pelo usuario (start type DISABLED no boot: ") +
                      (bootDisabled ? "sim, com elevacao" : "nao, sem elevacao - vale so nesta sessao") + ").");
        return true;
    } else {
        // Restaura o start type ANTES de iniciar: com DISABLED o ResumeOrStartService nunca abria.
        SetTelemetryServiceBootState("DiagTrack", false);
        SetTelemetryServiceBootState("dmwappushservice", false);

        // Politica AllowTelemetry volta ao valor exato guardado (ou sai se nunca existiu).
        auto& db = DatabaseManager::Instance();
        std::string storedPolicy = db.GetSetting("svc_telemetry_policy_allow_orig", "");
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
                          0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            if (!storedPolicy.empty() && storedPolicy == "absent") {
                RegDeleteValueW(hKey, L"AllowTelemetry");
            } else if (!storedPolicy.empty()) {
                DWORD val = 1;
                try { val = static_cast<DWORD>(std::stoul(storedPolicy)); } catch (...) { val = 1; }
                RegSetValueExW(hKey, L"AllowTelemetry", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
            } else {
                DWORD val = 1;
                RegSetValueExW(hKey, L"AllowTelemetry", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
            }
            RegCloseKey(hKey);
        }
        db.SetSetting("svc_telemetry_policy_allow_orig", "");
        ResumeOrStartService("DiagTrack");
        ResumeOrStartService("dmwappushservice");
        OPIT_LOG_INFO("ServiceManager: Telemetria da Microsoft restaurada (start types e politica AllowTelemetry).");
        return true;
    }
}

bool ServiceManager::IsMicrosoftTelemetryDisabled() const {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
                      0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AllowTelemetry", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    return false;
}

static bool IsKnownNonEssentialThirdParty(const std::string& lower) {
    static const std::vector<std::string> prefixes = {
        "gupdate", "edgeupdate", "adobearm", "adobeservice", "mozillamaintenance",
        "braveelevation", "opera autoupdate", "dellclient", "hpapphelper", "hpnetwork",
        "hpsysinfo", "lenovovantage", "asuslink", "asussystem", "dropboxupdate"
    };
    for (const auto& p : prefixes) {
        if (lower.find(p) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> ServiceManager::DetectInstalledNonEssentialServices() {
    std::vector<std::string> results;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm) return results;

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;

    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr);

    if (bytesNeeded > 0) {
        std::vector<BYTE> buffer(bytesNeeded);
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                  buffer.data(), bytesNeeded, &bytesNeeded, &servicesReturned,
                                  &resumeHandle, nullptr)) {
            auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
            for (DWORD i = 0; i < servicesReturned; ++i) {
                std::string svcName = StringUtils::ToUtf8(services[i].lpServiceName);
                std::string lower = StringUtils::ToLower(StringUtils::Trim(svcName));
                if (IsKnownNonEssentialThirdParty(lower)) {
                    results.push_back(svcName);
                }
            }
        }
    }
    CloseServiceHandle(scm);
    return results;
}

void ServiceManager::ApplySmartGamingAndEcoServices(bool isGaming, bool isDeepEco) {
    // Idem: era o terceiro ator do mesmo tick (parava WSearch/Spooler/atualizadores e, fora de jogo,
    // chamava RestoreModifiedServices por conta propria). Agora entra no mesmo reconciliador.
    RunServiceCycleFromSystem(isGaming, isDeepEco, 0.0, "min", false);
}

void ServiceManager::LoadAllServiceRules() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_rulesLoaded) return;
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return;

    std::string rulesBlob = db.GetSetting("custom_service_rules_blob", "");
    if (!rulesBlob.empty()) {
        std::stringstream ss(rulesBlob);
        std::string item;
        while (std::getline(ss, item, ';')) {
            auto pos = item.find('=');
            if (pos != std::string::npos) {
                std::string k = StringUtils::ToLower(StringUtils::Trim(item.substr(0, pos)));
                std::string v = StringUtils::Trim(item.substr(pos + 1));
                if (!k.empty() && !v.empty()) {
                    m_serviceRules[k] = v;
                }
            }
        }
    }
    m_rulesLoaded = true;
}

void ServiceManager::SetServiceRule(const std::string& serviceName, const std::string& rule) {
    std::string lowerKey = StringUtils::ToLower(StringUtils::Trim(serviceName));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_serviceRules.find(lowerKey);
        // Regra igual a existente: nada muda, entao sem reescrita do blob no banco e sem linha de log.
        // O rollback da IA restaurava 280 regras de uma vez e inundava o diario com escritas uteis zero.
        if (it != m_serviceRules.end() && it->second == rule) return;
        m_serviceRules[lowerKey] = rule;
    }
    auto& db = DatabaseManager::Instance();
    if (db.IsOpen()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream ss;
        for (const auto& kv : m_serviceRules) {
            ss << kv.first << "=" << kv.second << ";";
        }
        db.SetSetting("custom_service_rules_blob", ss.str());
    }
    OPIT_LOG_INFO("ServiceManager: Regra granular salva para " + serviceName + " -> " + rule);
}

std::string ServiceManager::GetServiceRule(const std::string& serviceName) {
    std::string lowerKey = StringUtils::ToLower(StringUtils::Trim(serviceName));
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_serviceRules.find(lowerKey);
    if (it != m_serviceRules.end()) {
        return it->second;
    }
    return "default";
}

std::unordered_map<std::string, std::string> ServiceManager::GetAllCustomRules() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_serviceRules;
}

std::vector<ServiceStatusInfo> ServiceManager::EnumerateAllServices(const std::string& filter) {
    std::vector<ServiceStatusInfo> results;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm) return results;

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;

    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr);

    if (bytesNeeded > 0) {
        std::vector<BYTE> buffer(bytesNeeded);
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                  buffer.data(), bytesNeeded, &bytesNeeded, &servicesReturned,
                                  &resumeHandle, nullptr)) {
            auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
            const auto& adv = ModuleAdvancedSettings::Instance().services;
            std::string lowerFilter = StringUtils::ToLower(StringUtils::Trim(filter));

            for (DWORD i = 0; i < servicesReturned; ++i) {
                std::string sName = StringUtils::ToUtf8(services[i].lpServiceName);
                std::string dName = StringUtils::ToUtf8(services[i].lpDisplayName);
                std::string lowerSName = StringUtils::ToLower(sName);
                std::string lowerDName = StringUtils::ToLower(dName);

                if (!lowerFilter.empty()) {
                    if (lowerSName.find(lowerFilter) == std::string::npos &&
                        lowerDName.find(lowerFilter) == std::string::npos) {
                        continue;
                    }
                }

                ServiceStatusInfo info;
                info.name = sName;
                info.displayName = dName;
                info.isRunning = (services[i].ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    info.isPausedByOpit = (m_pausedServices.find(lowerSName) != m_pausedServices.end());
                }
                info.isCritical = IsCriticalService(sName);
                info.isAllowed = adv.IsServiceAllowed(sName);
                info.rule = GetServiceRule(sName);
                {
                    HostInfo hi;
                    if (HostFromCache(sName, &hi)) info.gainMB = hi.workingSetMB;
                }
                const ServiceWake w = RequiredWakeFor(sName);
                info.wake = ServiceWakeToString(w);
                info.blockReason = BlockReasonFor(sName);

                if (info.isCritical) {
                    info.category = "Sistema (Critico)";
                    info.statusText = "Inviolavel";
                } else if (IsKnownNonEssentialThirdParty(lowerSName)) {
                    info.category = "Atualizador / Terceiros";
                    info.statusText = info.isRunning ? "Em Execucao" : "Parado";
                } else {
                    info.category = "Servico Windows";
                    info.statusText = info.isRunning ? "Em Execucao" : "Parado";
                }

                results.push_back(info);
            }
        }
    }
    CloseServiceHandle(scm);
    return results;
}

void ServiceManager::EvaluateAdvancedTriggers(bool isGameRunning, bool isBatteryOrEco, double cpuUsagePercent) {
    // Porta antiga, mantida viva para quem ainda chama. Ela nao decide mais nada sozinha: entrega o
    // gatilho ao reconciliador, que e um por ciclo.
    RunServiceCycleFromSystem(isGameRunning, isBatteryOrEco, cpuUsagePercent, "min", false);
}

// --- Reconciliador de servicos ---------------------------------------------------------------------
// Uma passada por ciclo: calcula o estado desejado de cada nome, devolve o que saiu do desejo ou cuja
// prova de retorno venceu, so entonces para o que ainda nao esta parado. Com um unico ator por tick, o
// vaivem deixa de ser possivel por construcao - nao por cooldown.

ServiceSweepInput ServiceManager::SweepInputFromSystem(bool isGameRunning, bool isBatteryOrEco,
                                                       double cpuUsagePercent, const std::string& serviceLevel,
                                                       bool subSmartServices) {
    const auto& adv = ModuleAdvancedSettings::Instance().services;
    ServiceSweepInput in;
    in.nowMs = NowSteadyMs();
    in.userIdleMs = LastInputIdleMs();
    in.idlePauseAfterMs = static_cast<uint64_t>(adv.idleSecondsBeforePause) * 1000ULL;
    in.isGameRunning = isGameRunning;
    in.isBatteryOrEco = isBatteryOrEco;
    in.cpuUsagePercent = cpuUsagePercent;
    in.autonomyLevel = adv.aiAutonomyLevel;
    in.highCpuThreshold = adv.highCpuThreshold;
    in.minGainMB = adv.minGainMB;
    in.maintenanceHour = adv.maintenanceHour;
    in.maintenanceMinutes = adv.maintenanceMinutes;
    in.serviceLevel = serviceLevel;
    in.subSmartServices = subSmartServices;
    in.pauseIndexingIdle = UnifiedSettingsModel::Instance().modPauseIndexingIdle;
    return in;
}

void ServiceManager::RunServiceCycleFromSystem(bool isGameRunning, bool isBatteryOrEco, double cpuUsagePercent,
                                               const std::string& serviceLevel, bool subSmartServices) {
    const uint64_t nowMs = NowSteadyMs();
    {
        // Desduplicacao: as tres portas antigas podiam cair no mesmo tick. Um ciclo de 2 em 2 s basta e
        // nenhuma delas consegue mais contrariar a outra.
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastCycleMs != 0 && nowMs - m_lastCycleMs < 2000) return;
    }
    RunAutomaticCycle(SweepInputFromSystem(isGameRunning, isBatteryOrEco, cpuUsagePercent, serviceLevel,
                                           subSmartServices));
}

void ServiceManager::RefreshHostCache(uint64_t nowMs) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hostCache.empty() && (nowMs - m_lastHostCacheMs) < kHostCacheMs) return;
    }

    std::unordered_map<std::string, HostInfo> fresh;
    std::vector<std::string> thirdParty;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm) return;

    DWORD needed = 0, returned = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &needed, &returned, &resume, nullptr);
    if (needed > 0) {
        std::vector<BYTE> buffer(needed);
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                  buffer.data(), needed, &needed, &returned, &resume, nullptr)) {
            auto* rows = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
            std::unordered_map<uint32_t, int> share;
            for (DWORD i = 0; i < returned; ++i) {
                const std::string key = LowerKey(StringUtils::ToUtf8(rows[i].lpServiceName));
                const auto& ssp = rows[i].ServiceStatusProcess;
                HostInfo hi;
                hi.pid = ssp.dwProcessId;
                hi.controlsAccepted = ssp.dwControlsAccepted;
                hi.state = ssp.dwCurrentState;
                hi.known = true;
                fresh[key] = hi;
                if (ssp.dwProcessId != 0) ++share[ssp.dwProcessId];
                if (IsKnownNonEssentialThirdParty(key)) {
                    thirdParty.push_back(StringUtils::ToUtf8(rows[i].lpServiceName));
                }
            }
            // A memoria de cada hospedeiro vem de UMA enumeracao de processos para todos os PIDs.
            std::unordered_map<uint32_t, uint64_t> pidMB;
            for (const auto& p : ProcessManager::EnumerateProcesses()) {
                if (p.workingSetBytes != 0) pidMB[p.pid] = p.workingSetBytes >> 20;
            }
            for (auto& kv : fresh) {
                if (kv.second.pid == 0) continue;
                auto sit = share.find(kv.second.pid);
                kv.second.servicesInHost = (sit == share.end()) ? 1 : sit->second;
                auto mit = pidMB.find(kv.second.pid);
                if (mit != pidMB.end()) kv.second.workingSetMB = mit->second;
            }
        }
    }
    CloseServiceHandle(scm);
    if (fresh.empty()) return; // sem leitura nenhuma: conservamos o ultimo mapa valido, nao decidimos no escuro

    std::lock_guard<std::mutex> lock(m_mutex);
    m_hostCache.swap(fresh);
    m_thirdPartyNames = std::move(thirdParty);
    m_lastHostCacheMs = nowMs;
}

bool ServiceManager::HostFromCache(const std::string& serviceName, HostInfo* out) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    auto it = m_hostCache.find(LowerKey(serviceName));
    if (it == m_hostCache.end()) return false;
    if (out) *out = it->second;
    return true;
}

void ServiceManager::BuildDesiredStates(const ServiceSweepInput& in,
                                        std::unordered_map<std::string, DesiredState>* out) {
    const auto& adv = ModuleAdvancedSettings::Instance().services;
    std::unordered_map<std::string, std::string> rules;
    std::vector<std::string> thirdParty;
    bool trigCpu = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        rules = m_serviceRules;
        thirdParty = m_thirdPartyNames;
        trigCpu = m_highCpuTriggerActive;
    }

    const bool trigGame = adv.pauseOnGame && in.isGameRunning;
    const bool trigEco = adv.pauseOnEco && in.isBatteryOrEco;
    const bool anyTrigger = trigGame || trigEco || trigCpu;
    const bool idleOk = (in.userIdleMs >= in.idlePauseAfterMs) && !in.isGameRunning;
    const bool idleSweep = adv.pauseWhenIdle && idleOk;
    // O interruptor antigo da janela unificada ("Pausar indexacao com o PC parado") continua valendo para o
    // WSearch mesmo com a varredura de ociosidade desligada, com o mesmo limiar de CPU de antes.
    const bool indexingIdle = in.pauseIndexingIdle && idleOk && in.cpuUsagePercent < 15.0;

    // 1) As regras individuais da pessoa mandam sobre qualquer automacao.
    for (const auto& kv : rules) {
        DesiredState d;
        if (kv.second == "never_pause") {
            d.protect = true;
            d.reason = "protegido por voce";
        } else if (kv.second == "always_paused") {
            d.pause = true;
            d.fixedRule = true;
            d.wake = ServiceWake::None;
            d.reason = "regra fixa: manter parado";
        } else if (kv.second == "pause_on_game") {
            if (!trigGame) continue;
            d.pause = true;
            d.wake = ServiceWake::OnTriggerEnd;
            d.reason = "gatilho: jogo aberto";
        } else if (kv.second == "pause_on_eco") {
            if (!trigEco) continue;
            d.pause = true;
            d.wake = ServiceWake::OnTriggerEnd;
            d.reason = "gatilho: economia ou bateria";
        } else if (kv.second == "pause_on_high_cpu") {
            if (!trigCpu) continue;
            d.pause = true;
            d.wake = ServiceWake::OnTriggerEnd;
            d.reason = "gatilho: CPU alta";
        } else {
            continue; // "default"/"auto": a automacao geral decide abaixo
        }
        (*out)[kv.first] = d;
    }

    // 2) Catalogo: cada nome ja carrega a prova de como ele volta. A prova depende do MOTIVO:
    //    matriz/gatilho de jogo ou CPU -> volta quando o gatilho acaba (era o comportamento antigo);
    //    gatilho de eco -> nomes de "volta por atividade" so dormem com o PC ocioso de verdade;
    //    varredura de ociosidade -> prova do catalogo; bloatware permanente -> janela de manutencao.
    for (const auto& e : Catalog()) {
        const std::string key = e.name;
        if (out->find(key) != out->end()) continue; // regra da pessoa tem prioridade
        if (in.autonomyLevel < e.minAutonomy) continue;
        if (!adv.IsServiceAllowed(key)) continue;
        DesiredState d;
        if (LevelMatrixIncludes(in.serviceLevel, key, in.subSmartServices, in.isGameRunning)) {
            d.pause = true;
            d.wake = ServiceWake::OnTriggerEnd;
            d.reason = "nivel de servicos " + in.serviceLevel;
        } else if (trigGame || trigCpu) {
            d.pause = true;
            d.wake = ServiceWake::OnTriggerEnd;
            d.reason = trigGame ? "gatilho: jogo aberto" : "gatilho: CPU alta";
        } else if (trigEco) {
            d.pause = true;
            d.wake = e.wake; // OnUserActivity so dorme enquanto o PC esta ocioso (guarda no PauseTracked)
            d.reason = "gatilho: economia ou bateria";
        } else if (adv.pauseBloatwarePermanent &&
                   (e.wake == ServiceWake::OnMaintenanceWindow || key == "sysmain")) {
            d.pause = true;
            d.wake = ServiceWake::OnMaintenanceWindow;
            d.reason = "parado por escolha fixa";
        } else if (idleSweep && e.wake == ServiceWake::OnUserActivity) {
            d.pause = true;
            d.wake = e.wake;
            d.reason = "ociosa ha " + std::to_string(in.userIdleMs / 1000) + " s";
        } else if (key == "wsearch" && indexingIdle) {
            d.pause = true;
            d.wake = ServiceWake::OnUserActivity;
            d.reason = "PC parado (interruptor de indexacao)";
        }
        if (d.pause) (*out)[key] = d;
    }

    // 3) Descoberta de terceiros (atualizadores instalados na maquina), so com autonomia ligada.
    if (in.autonomyLevel >= 1 && adv.manageThirdPartyUpdaters && (anyTrigger || idleSweep || adv.pauseBloatwarePermanent)) {
        for (const auto& raw : thirdParty) {
            const std::string key = LowerKey(raw);
            if (out->find(key) != out->end()) continue;
            if (IsCriticalService(key) || !adv.IsServiceAllowed(key)) continue;
            DesiredState d;
            d.pause = true;
            d.wake = adv.pauseBloatwarePermanent ? ServiceWake::OnMaintenanceWindow : ServiceWake::OnUserActivity;
            d.reason = "atualizador de terceiro em 2o plano";
            (*out)[key] = d;
        }
    }
}

void ServiceManager::CountDemandWakeLocked(const std::string& lowerKey, uint64_t nowMs) {
    auto wit = m_lastWakeMs.find(lowerKey);
    if (wit == m_lastWakeMs.end() || (nowMs - wit->second) > kDemandWindowMs) {
        m_demandWakes[lowerKey] = 0; // janela de 1 h renovada
    }
    m_lastWakeMs[lowerKey] = nowMs;
    const int hits = ++m_demandWakes[lowerKey];
    if (hits >= kMaxDemandWakes) {
        m_unautomatable.insert(lowerKey);
        m_blockReason[lowerKey] = "devolveu por demanda " + std::to_string(hits) +
                                  " vezes em 1 h - nao automatizavel nesta sessao";
        OPIT_LOG_WARN("ServiceManager: '" + lowerKey + "' voltou por demanda " + std::to_string(hits) +
                      " vezes na ultima hora. Sai da automacao desta sessao para nao viver em vaivem.");
    }
}

bool ServiceManager::WakeTracked(const std::string& name, bool countedAsDemand, uint64_t nowMs) {
    const std::string key = LowerKey(name);
    ServicePauseRecord rec;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_records.find(key);
        if (it == m_records.end()) return false;
        rec = it->second;
    }

    bool ok = true;
    if (rec.wasRunning) {
        // Pausa de verdade (SERVICE_CONTROL_PAUSE) se devolve com CONTINUE, sem perder o estado interno do
        // servico; o que foi STOP volta pelo caminho classico.
        if (rec.pausedByPauseControl) ok = SendServiceControl(name, SERVICE_CONTROL_CONTINUE, SERVICE_PAUSE_CONTINUE);
        if (!ok) ok = StartServiceByName(name);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_records.erase(key);
        m_pausedServices.erase(key);
        m_blockReason.erase(key);
        if (countedAsDemand) CountDemandWakeLocked(key, nowMs);
    }
    SaveJournal();
    OPIT_LOG_INFO(std::string("ServiceManager: Servico ") + (ok ? "devolvido" : "DEVOLVIDO COM FALHA") + ": " +
                  name + " (" + (rec.pausedByPauseControl ? "CONTINUE" : "START") +
                  (countedAsDemand ? ", volta por demanda" : ", gatilho encerrado") + ")");
    return ok;
}

int ServiceManager::PauseTracked(const std::string& name, const DesiredState& want,
                                 const ServiceSweepInput& in, std::string* blockOut) {
    const std::string key = LowerKey(name);
    auto block = [&](int code, const std::string& text) {
        SetBlockReason(key, text, in.nowMs);
        if (blockOut) *blockOut = text;
        return code;
    };

    if (IsUnautomatable(key)) return block(3, "nao automatizavel nesta sessao");
    // A trava de 15 min le-se sob mutex, mas decide-se FORA dele: escrever o motivo (SetBlockReason) pede
    // o mesmo mutex, que nao e recursivo, e lanca resource_deadlock_would_ocour no meio do ciclo.
    uint64_t lastWakeMs = 0;
    bool wokenRecently = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto wit = m_lastWakeMs.find(key);
        wokenRecently = (wit != m_lastWakeMs.end());
        if (wokenRecently) lastWakeMs = wit->second;
    }
    if (wokenRecently && (in.nowMs - lastWakeMs) < kWakeLockoutMs) {
        return block(3, "devolvido por demanda ha menos de 15 min");
    }
    if (want.wake == ServiceWake::None && !want.fixedRule) {
        return block(4, std::string("sem prova de retorno (") + ServiceWakeToString(want.wake) + ")");
    }
    // Prova de volta por atividade vale ao contrario: so dorme com o PC OCIOSO. Com a pessoa no mouse,
    // pausar seria devolver na amostra seguinte — o vaivem que criamos as guardas para impedir.
    if (want.wake == ServiceWake::OnUserActivity && in.userIdleMs < in.idlePauseAfterMs) {
        return block(4, "voce esta usando o PC agora; a volta seria imediata");
    }

    HostInfo hi;
    if (!HostFromCache(key, &hi) || !hi.known) return block(7, "nao lemos o servico no SCM");
    if (hi.state != SERVICE_RUNNING) return block(9, "ja esta parado");

    const auto& adv = ModuleAdvancedSettings::Instance().services;
    if (adv.checkDependencies && IsServiceNeededByActiveApplication(key, *in.runningProcesses)) {
        return block(6, "aplicativo associado esta aberto");
    }
    if (key == "spooler" && PrintJobsPending()) return block(6, "tem trabalho na fila de impressao");
    if (hi.servicesInHost > 1) {
        return block(1, "hospedado junto com " + std::to_string(hi.servicesInHost - 1) +
                        " outros servicos no mesmo processo");
    }
    if (hi.workingSetMB < in.minGainMB) {
        return block(2, "ganho medido de " + std::to_string(hi.workingSetMB) + " MB (minimo " +
                        std::to_string(in.minGainMB) + " MB)");
    }

    std::string blockText;
    const bool didPause = PauseOrStopService(name, want.wake, want.reason, true, want.fixedRule);
    if (!didPause) {
        blockText = BlockReasonFor(key);
        return block(7, blockText.empty() ? "o SCM recusou" : blockText);
    }
    if (blockOut) blockOut->clear();
    return 0;
}

void ServiceManager::RunAutomaticCycle(const ServiceSweepInput& inRaw) {
    ServiceSweepInput in = inRaw;
    if (in.nowMs == 0) in.nowMs = NowSteadyMs();

    LoadAllServiceRules();
    LoadJournal();

    std::vector<std::string> procNames;
    if (!in.runningProcesses) {
        for (const auto& p : ProcessManager::EnumerateProcesses()) procNames.push_back(p.name);
        in.runningProcesses = &procNames;
    }

    const auto& adv = ModuleAdvancedSettings::Instance().services;
    {
        // Histerese real do gatilho de CPU: entra no limiar escolhido e so sai 15 pontos abaixo. Antes a
        // marca era ligada e desligada no mesmo tick, e o servico ia e voltava com o ruído da amostra.
        std::lock_guard<std::mutex> lock(m_mutex);
        const double th = static_cast<double>(adv.highCpuThreshold);
        if (adv.pauseOnHighCpu && in.cpuUsagePercent >= th) m_highCpuTriggerActive = true;
        else if (m_highCpuTriggerActive && in.cpuUsagePercent < th - 15.0) m_highCpuTriggerActive = false;
        m_lastCycleMs = in.nowMs;
    }

    RefreshHostCache(in.nowMs);

    std::unordered_map<std::string, DesiredState> desired;
    BuildDesiredStates(in, &desired);

    ServiceSweepStats stats;

    // a) Devolver primeiro: um servico que saiu do desejo ou cuja prova de retorno venceu nao pode
    //    continuar parado enquanto alguem espera por ele.
    std::vector<std::string> recordKeys;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kv : m_records) recordKeys.push_back(kv.first);
    }
    for (const std::string& key : recordKeys) {
        ServicePauseRecord rec;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_records.find(key);
            if (it == m_records.end()) continue;
            rec = it->second;
        }
        HostInfo hi;
        const bool known = HostFromCache(key, &hi);
        const bool cameBack = known && (hi.state == SERVICE_RUNNING || hi.state == SERVICE_START_PENDING ||
                                        hi.state == SERVICE_CONTINUE_PENDING);
        auto dit = desired.find(key);
        const bool wantPause = (dit != desired.end() && dit->second.pause);

        // Voltou sem nos pedir (o SCM iniciou por demanda ou a pessoa acendeu): nao e vitoria nossa, e a
        // prova de que a maquina precisa dele acordado. Devolvemos o registro e contamos a volta.
        if (cameBack && wantPause && rec.wake != ServiceWake::None) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_records.erase(key);
                m_pausedServices.erase(key);
                CountDemandWakeLocked(key, in.nowMs);
            }
            SaveJournal();
            stats.resumed++;
            continue;
        }
        if (cameBack && rec.wake == ServiceWake::None) {
            // Pausa manual ou regra fixa que o SCM reacendeu: largamos o registro para nao brigar com o
            // Windows; a pessoa ve o estado real na aba.
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_records.erase(key);
                m_pausedServices.erase(key);
            }
            OPIT_LOG_INFO("ServiceManager: '" + key + "' voltou a rodar por conta propria; largamos o registro.");
            SaveJournal();
            continue;
        }
        // Escolha declarada (manual, opt-out, regra fixa): a prova de volta e a PESSOA (retomar,
        // restaurar) ou o proprio Windows (ramo acima). Nem gatilho ativo nem prova vencida desfazem.
        if (rec.declared) continue;
        if (!wantPause) {
            if (WakeTracked(key, false, in.nowMs)) stats.resumed++;
            continue;
        }
        // A prova de retorno e a que foi registrada na hora da pausa, nao a do estado desejado atual.
        // So e demanda quando a prova e OnDemandProcess: ociosidade e janela de manutencao sao retornos
        // planejados — conta-los colocaria o nome em quarentena por cumprir o proprio plano.
        const bool demand = IsServiceNeededByActiveApplication(key, *in.runningProcesses);
        const bool countedAsDemand = rec.wake == ServiceWake::OnDemandProcess;
        if (ShouldWakeNow(rec.wake, in, demand) && WakeTracked(key, countedAsDemand, in.nowMs)) stats.resumed++;
    }

    // b) Pausar depois, com orçamento: encostar em 25 serviços de uma vez e o jeito certo de perder
    //    trabalho no meio do caminho (e de a maquina parecer travada). Os maiores ganhos medidos primeiro.
    constexpr size_t kMaxPausesPerCycle = 6;
    std::vector<std::pair<std::string, DesiredState>> pauseQueue;
    for (const auto& kv : desired) {
        if (!kv.second.pause || kv.second.protect) continue;
        pauseQueue.emplace_back(kv.first, kv.second);
    }
    std::sort(pauseQueue.begin(), pauseQueue.end(), [&](const auto& a, const auto& b) {
        HostInfo ha, hb;
        HostFromCache(a.first, &ha);
        HostFromCache(b.first, &hb);
        return ha.workingSetMB > hb.workingSetMB;
    });
    for (const auto& item : pauseQueue) {
        const std::string& key = item.first;
        const DesiredState& want = item.second;
        stats.candidates++;
        bool already = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            already = m_records.find(key) != m_records.end();
        }
        if (already) continue;
        if (stats.paused >= kMaxPausesPerCycle) {
            stats.skippedBudget++;
            continue;
        }
        std::string blockText;
        const int code = PauseTracked(key, want, in, &blockText);
        if (code == 0) {
            ServicePauseRecord rec;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_records.find(key);
                if (it != m_records.end()) rec = it->second;
            }
            stats.paused++;
            stats.gainMBLastCycle += rec.hostWorkingSetMB;
        } else if (code == 1) {
            stats.skippedSharedHost++;
        } else if (code == 2) {
            stats.skippedSmallGain++;
        } else if (code == 3) {
            stats.skippedLockout++;
        } else if (code == 4) {
            stats.skippedNoReturn++;
        } else if (code != 9) {
            stats.skippedOther++; // recusa do SCM / falta de privilegio: counted para nao sumir numero
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        stats.unautomatable = m_unautomatable.size();
        m_stats = stats;
        m_stats.lastCycleMs = in.nowMs;
    }

    if (stats.paused > 0 || stats.resumed > 0) {
        OPIT_LOG_INFO("ServiceGovernor: ciclo " + std::to_string(in.nowMs) + " - alvos " +
                      std::to_string(stats.candidates) + ", parados " + std::to_string(stats.paused) +
                      " (" + std::to_string(stats.gainMBLastCycle) + " MB fora da RAM), devolvidos " +
                      std::to_string(stats.resumed) + ", host dividido " + std::to_string(stats.skippedSharedHost) +
                      ", ganho pequeno " + std::to_string(stats.skippedSmallGain) + ", trava anti-vaivem " +
                      std::to_string(stats.skippedLockout) + ", sem prova " + std::to_string(stats.skippedNoReturn) +
                      ", orcamento pulado " + std::to_string(stats.skippedBudget) +
                      ", recusas " + std::to_string(stats.skippedOther) + ", nao automatizaveis " +
                      std::to_string(stats.unautomatable) + ".");
    }
}

std::vector<ServicePauseRecord> ServiceManager::PauseRecords() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    std::vector<ServicePauseRecord> out;
    out.reserve(m_records.size());
    for (const auto& kv : m_records) out.push_back(kv.second);
    return out;
}

// Diario persistido: se o programa fechar (ou cair) com servicos pausados, a proxima abertura sabe o que
// era nosso e devolve. Sem isso, um STOP nosso viraria "Windows estranho" para sempre.
void ServiceManager::SaveJournal() {
    std::ostringstream ss;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& kv : m_records) {
            const ServicePauseRecord& r = kv.second;
            std::string reason = r.reason;
            for (auto& c : reason) { if (c == '\t' || c == '\n' || c == ';') c = ' '; }
            ss << kv.first << '\t' << (r.wasRunning ? 1 : 0) << '\t' << (r.pausedByPauseControl ? 1 : 0)
               << '\t' << (r.declared ? 1 : 0)
               << '\t' << static_cast<int>(r.wake) << '\t' << r.hostWorkingSetMB << '\t' << reason << '\n';
        }
    }
    auto& db = DatabaseManager::Instance();
    if (db.IsOpen()) db.SetSetting("svc_auto_journal_v1", ss.str());
}

void ServiceManager::LoadJournal() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_journalLoaded) return;
        m_journalLoaded = true;
    }
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return;
    std::string blob = db.GetSetting("svc_auto_journal_v1", "");
    if (blob.empty()) return;

    std::stringstream ss(blob);
    std::string line;
    size_t restored = 0;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f;
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            if (tab == std::string::npos) { f.push_back(line.substr(start)); break; }
            f.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (f.size() < 6 || f[0].empty()) continue;
        ServicePauseRecord r;
        r.name = f[0];
        r.wasRunning = (f[1] == "1");
        r.pausedByPauseControl = (f[2] == "1");
        r.wake = static_cast<ServiceWake>(std::atoi(f[3].c_str()));
        r.hostWorkingSetMB = static_cast<uint64_t>(std::atoll(f[4].c_str()));
        r.reason = f[5];
        r.pausedAtMs = 0;
        if (f.size() >= 7) {
            // Formato novo: coluna 3 = declarada (escolha da pessoa, sem prova exigida).
            r.declared = (f[3] == "1");
            r.wake = static_cast<ServiceWake>(std::atoi(f[4].c_str()));
            r.hostWorkingSetMB = static_cast<uint64_t>(std::atoll(f[5].c_str()));
            r.reason = f[6];
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_records[f[0]] = r;
        m_pausedServices.insert(f[0]);
        ++restored;
    }
    if (restored > 0) {
        OPIT_LOG_INFO("ServiceManager: diario de servicos recarregado - " + std::to_string(restored) +
                      " nomes ainda nossos; cada um volta pela sua prova ou por restauracao explicita.");
    }
}

bool ServiceManager::RequestAiServiceOptimization(const std::string& serviceName, bool pause, const std::string& rationale, std::string* outFeedback) {
    const auto& adv = ModuleAdvancedSettings::Instance().services;

    // Regra 1: Verificar autonomia da IA
    if (adv.aiAutonomyLevel == 0) {
        if (outFeedback) *outFeedback = "Acao recusada: Autonomia da IA para gerenciamento de servicos esta configurada como Manual/Desativada.";
        OPIT_LOG_WARN("ServiceManager AI: Acao recusada (Modo Manual): " + serviceName);
        return false;
    }

    // Regra 2: Blindagem inviolável do sistema
    if (IsCriticalService(serviceName)) {
        if (outFeedback) *outFeedback = "Acao BLOQUEADA por seguranca: O servico '" + serviceName + "' e critico para a estabilidade do Windows.";
        OPIT_LOG_WARN("ServiceManager AI: Tentativa de manipular servico critico bloqueada: " + serviceName);
        return false;
    }

    // Regra 3: Exceções do usuário
    if (!adv.IsServiceAllowed(serviceName)) {
        if (outFeedback) *outFeedback = "Acao recusada: O servico '" + serviceName + "' esta nas regras de exclusao/excecao do usuario.";
        OPIT_LOG_INFO("ServiceManager AI: Servico em lista de excecao: " + serviceName);
        return false;
    }

    // Regra 4: Modo Consultivo (requer confirmação do usuário)
    if (adv.aiAutonomyLevel == 1) {
        std::string rec = "Recomendacao da IA: " + std::string(pause ? "Pausar " : "Retomar ") + serviceName + " (Motivo: " + rationale + "). Aguardando confirmacao.";
        if (outFeedback) *outFeedback = rec;
        OPIT_LOG_INFO("ServiceManager AI (Consultivo): " + rec);
        return true;
    }

    // Regra 5: Modo Automático (aplica com segurança)
    if (pause) {
        bool ok = PauseOrStopService(serviceName);
        if (outFeedback) {
            *outFeedback = ok ? ("Servico " + serviceName + " pausado automaticamente pela IA (" + rationale + ").")
                              : ("Falha ao pausar servico " + serviceName);
        }
        return ok;
    } else {
        bool ok = ResumeOrStartService(serviceName);
        if (outFeedback) {
            *outFeedback = ok ? ("Servico " + serviceName + " restaurado automaticamente pela IA (" + rationale + ").")
                              : ("Falha ao restaurar servico " + serviceName);
        }
        return ok;
    }
}

void ServiceManager::RestoreAllServicesNow() {
    RestoreModifiedServices();
    OPIT_LOG_INFO("ServiceManager: Todos os servicos foram restaurados manualmente pelo usuario ao padrao do Windows.");
}

size_t ServiceManager::GetPausedServicesCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_pausedServices.size();
}

std::vector<std::string> ServiceManager::GetPausedServicesList() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return std::vector<std::string>(m_pausedServices.begin(), m_pausedServices.end());
}

std::vector<ServiceStatusInfo> ServiceManager::GetTrackedServicesInfo() {
    std::vector<ServiceStatusInfo> list;
    const auto& adv = ModuleAdvancedSettings::Instance().services;

    auto addSvc = [&](const std::string& name, const std::string& disp, const std::string& cat) {
        ServiceStatusInfo info;
        info.name = name;
        info.displayName = disp;
        info.category = cat;
        info.isRunning = IsServiceRunning(name);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            info.isPausedByOpit = (m_pausedServices.find(LowerKey(name)) != m_pausedServices.end());
        }
        info.isCritical = IsCriticalService(name);
        info.isAllowed = adv.IsServiceAllowed(name);
        info.rule = GetServiceRule(name);
        {
            HostInfo hi;
            if (HostFromCache(name, &hi)) info.gainMB = hi.workingSetMB;
        }
        info.wake = ServiceWakeToString(RequiredWakeFor(name));
        info.blockReason = BlockReasonFor(name);
        list.push_back(info);
    };

    addSvc("WSearch", "Windows Search (Indexacao)", "Indexacao");
    addSvc("SysMain", "SysMain (Superfetch)", "Cache / Disco");
    addSvc("Spooler", "Spooler de Impressao", "Impressao");
    addSvc("DiagTrack", "Telemetria do Windows", "Telemetria");
    addSvc("dmwappushservice", "Servico de Rastreamento WAP", "Telemetria");

    auto thirdParty = DetectInstalledNonEssentialServices();
    for (const auto& tp : thirdParty) {
        addSvc(tp, tp + " (Atualizador)", "Atualizadores Terceiros");
    }

    return list;
}

} // namespace opitcore
