// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#ifndef _WIN32

// Gemelo Linux do caminho PowrProf/powercfg.
//
// O Windows guarda cada valor por linha (tomada e bateria) dentro do plano ativo e re-aplica
// ao mudar de linha; o kernel nao tem essa tabela. O censo medido neste kernel (WSL2,
// 6.18.58.1-microsoft-standard) mostrou onde cada conceito tem mecanismo real e onde nao tem:
//   * /sys/devices/system/cpu/cpufreq EXISTE mas vazio -> nenhum policy*, nenhum modulo
//     cpufreq, nenhum ficheiro de frequencia em cpu0. Em Ubuntu em hardware real la esta
//     (intel_pstate / amd-pstate / acpi-cpufreq), e e ai que os tetos de CPU vivem.
//   * /sys/devices/system/cpu/cpuN/online EXISTE (15 ficheiros, cpu0 nao tem) -> e o unico
//     equivalente real do "core parking".
//   * /sys/module/pcie_aspm/parameters/policy EXISTE com lista e o ativo entre colchetes.
//   * /sys/class/block/<dev>/device/power/control EXISTE (auto|on) -> protecao contra
//     desligar disco; o TIMEOUT de inatividade do disco nao tem interface em sysfs (vive no
//     hdparm) e por isso e declarado indisponivel em vez de inventado.
//   * /sys/class/power_supply vazio e /sys/bus/usb/devices ausente -> linha AC/DC e suspend
//     selectivo USB sem leitura possivel aqui.
//   * euid=1000 sem sudo sem senha -> TODAS as escritas acima dao EACCES. O caminho devolve
//     false com o motivo real, porque "aplicado com sucesso" sem o kernel ter aceite seria a
//     mesma mentira que o ramo Windows evita com a leitura de confirmacao.
//
// Convencoes:
//   * escrita confirmada por leitura de volta; quando o kernel ajusta (degrau de frequencia),
//     reporta-se o numero medido, nao o pedido;
//   * restaurar usa o estado exato capturado antes da primeira escrita, por politica cpufreq
//     (hibridos tem governor, EPP e tetos proprios em policy0 e policy8);
//   * linhas AC e DC separadas nao existem: uma escrita que peca numeros diferentes para as
//     duas e aplicada na linha em uso e declarada, em vez de achatada silenciosamente.
//
// Raizes configuraveis (mesma regra do hwmon, do DMI e do DRM) -- sem fixture nao ha como
// exercitar estas rotas num kernel que nao tem os controladores, e as escritas reais
// aconteceriam na maquina dela:
//   OPITCORE_CPUFREQ_ROOT -> /sys/devices/system/cpu/cpufreq
//   OPITCORE_CPU_ROOT     -> /sys/devices/system/cpu
//   OPITCORE_POWER_ROOT   -> /sys/class/power_supply
//   OPITCORE_USB_ROOT     -> /sys/bus/usb/devices
//   OPITCORE_BLOCK_ROOT   -> /sys/class/block
//   OPITCORE_ASPM_POLICY  -> /sys/module/pcie_aspm/parameters/policy

#include "PowerManager.h"
#include "platform/linux/ElevationHelper.h"
#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>

namespace opitcore {
namespace {

std::string EnvRoot(const char* var, const std::string& fallback) {
    const char* v = std::getenv(var);
    return (v && *v) ? std::string(v) : fallback;
}

std::string CpufreqRoot() {
    return EnvRoot("OPITCORE_CPUFREQ_ROOT", "/sys/devices/system/cpu/cpufreq");
}

bool ReadStrFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::getline(f, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                            out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return true;
}

bool ReadU64File(const std::string& path, unsigned long long& out) {
    std::ifstream f(path);
    std::string token;
    if (!(f >> token)) return false;
    const char* begin = token.c_str();
    char* end = nullptr;
    const unsigned long long v = std::strtoull(begin, &end, 10);
    if (end == begin) return false;
    out = v;
    return true;
}

std::string FirstTokenOf(const std::string& s) {
    std::istringstream ss(s);
    std::string w;
    ss >> w;
    return w;
}

std::string LastErrno() {
    return std::string(strerror(errno));
}

// ---------------------------------------------------------------------------
// Escrita em sysfs
// ---------------------------------------------------------------------------
// O kernel pode (a) recusar a abertura (EACCES/EROFS), (b) recusar o conteudo (EINVAL) ou
// (c) aceitar e ajustar para o degrau mais proximo. Tratar (c) como fracasso faria o
// programa mentir para baixo; tratar como sucesso do valor pedido faria mentir para cima.
enum class WriteResult {
    Refused,   // o kernel nao aceitou a escrita
    Confirmed, // aceite e lida de volta igual
    Adjusted   // aceite, mas o kernel gravou outro numero
};

WriteResult WriteNumberAndVerify(const std::string& path, unsigned long long want,
                                 unsigned long long& actual, std::string& err) {
    actual = 0;
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) { err = "abertura recusada (" + LastErrno() + ")"; return WriteResult::Refused; }
        f << want;
        f.flush();
        if (!f) { err = "conteudo recusado (" + LastErrno() + ")"; return WriteResult::Refused; }
    }
    if (!ReadU64File(path, actual)) {
        err = "escrito mas ja nao se le (" + LastErrno() + ")";
        return WriteResult::Refused;
    }
    return actual == want ? WriteResult::Confirmed : WriteResult::Adjusted;
}

WriteResult WriteTextAndVerify(const std::string& path, const std::string& want, std::string& err) {
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) { err = "abertura recusada (" + LastErrno() + ")"; return WriteResult::Refused; }
        f << want;
        f.flush();
        if (!f) { err = "conteudo recusado (" + LastErrno() + ")"; return WriteResult::Refused; }
    }
    std::string back;
    if (!ReadStrFile(path, back)) {
        err = "escrito mas ja nao se le (" + LastErrno() + ")";
        return WriteResult::Refused;
    }
    // Alguns ficheiros devolvem mais do que o token escrito; o que interessa e o primeiro.
    return FirstTokenOf(back) == FirstTokenOf(want) ? WriteResult::Confirmed : WriteResult::Adjusted;
}

std::string PrivilegeHint() {
    return geteuid() == 0 ? std::string() : " (falta privilegio de root, euid=" +
        std::to_string(geteuid()) + ")";
}

bool DirExists(const std::string& path) {
    DIR* d = ::opendir(path.c_str());
    if (!d) return false;
    ::closedir(d);
    return true;
}

std::vector<std::string> ListNames(const std::string& dir) {
    std::vector<std::string> names;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return names;
    while (struct dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> SplitWords(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string w;
    while (ss >> w) out.push_back(w);
    return out;
}

// As politicas cpufreq deste kernel. No WSL2 medido o diretorio existe mas esta vazio, por
// isso o censo devolve 0 e todos os tetos de CPU ficam indisponiveis -- em Ubuntu real com
// intel_pstate/amd-pstate aqui aparecem policy0, policy8, ...
std::vector<std::string> CpuFreqPolicies() {
    const std::string root = CpufreqRoot();
    std::vector<std::string> out;
    for (const std::string& n : ListNames(root)) {
        if (n.rfind("policy", 0) != 0) continue;
        if (!DirExists(root + "/" + n)) continue;
        std::string gov;
        if (!ReadStrFile(root + "/" + n + "/scaling_governor", gov)) continue;
        out.push_back(root + "/" + n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct CpufreqState {
    std::string governor;
    std::string epp;
    bool hasEpp = false;
    unsigned long long minKHz = 0;
    unsigned long long maxKHz = 0;
    unsigned long long cpuinfoMinKHz = 0;
    unsigned long long cpuinfoMaxKHz = 0;
    std::vector<std::string> governors;
    std::vector<std::string> epps;
};

bool ReadPolicyState(const std::string& policy, CpufreqState& st) {
    bool any = false;
    any = ReadStrFile(policy + "/scaling_governor", st.governor) || any;
    any = ReadU64File(policy + "/scaling_min_freq", st.minKHz) || any;
    any = ReadU64File(policy + "/scaling_max_freq", st.maxKHz) || any;
    ReadU64File(policy + "/cpuinfo_min_freq", st.cpuinfoMinKHz);
    ReadU64File(policy + "/cpuinfo_max_freq", st.cpuinfoMaxKHz);
    std::string list;
    if (ReadStrFile(policy + "/scaling_available_governors", list)) st.governors = SplitWords(list);
    if (ReadStrFile(policy + "/energy_performance_available_preferences", list)) {
        st.epps = SplitWords(list);
        st.hasEpp = !st.epps.empty();
    }
    if (st.hasEpp) ReadStrFile(policy + "/energy_performance_preference", st.epp);
    return any;
}

unsigned long long PercentToKHz(unsigned long long cpuinfoMaxKHz, uint32_t percent) {
    if (cpuinfoMaxKHz == 0 || percent == 0) return 0;
    return (cpuinfoMaxKHz * percent + 50ULL) / 100ULL;
}

uint32_t KHzToPercent(unsigned long long khz, unsigned long long cpuinfoMaxKHz) {
    if (cpuinfoMaxKHz == 0) return 0;
    return static_cast<uint32_t>((khz * 100ULL + cpuinfoMaxKHz / 2ULL) / cpuinfoMaxKHz);
}

// ---------------------------------------------------------------------------
// Linha de alimentacao e bateria
// ---------------------------------------------------------------------------
struct PowerSupplyScan {
    bool hasBattery = false;
    bool acOnline = true;
    bool anyRead = false;
    int percent = -1;
    bool charging = false;
};

PowerSupplyScan ScanPowerSupplies() {
    PowerSupplyScan s;
    const std::string root = EnvRoot("OPITCORE_POWER_ROOT", "/sys/class/power_supply");
    int capacity = -1;
    bool haveCapacity = false;
    bool charging = false;
    bool haveStatus = false;
    bool acSeen = false;
    bool acConnected = false;

    for (const std::string& n : ListNames(root)) {
        const std::string base = root + "/" + n;
        std::string type;
        if (!ReadStrFile(base + "/type", type)) continue;
        s.anyRead = true;
        const std::string t = ToLower(type);
        if (t == "battery" || t == "ups") {
            s.hasBattery = true;
            unsigned long long cap = 0;
            if (ReadU64File(base + "/capacity", cap) && cap <= 100ULL) {
                if (!haveCapacity || cap < static_cast<unsigned long long>(capacity)) {
                    capacity = static_cast<int>(cap);
                }
                haveCapacity = true;
            }
            std::string status;
            if (ReadStrFile(base + "/status", status)) {
                const std::string st = ToLower(status);
                const bool ch = (st == "charging" || st == "full");
                charging = charging || ch;
                haveStatus = true;
            }
        } else if (t == "main" || t == "mains" || t == "usb" || t == "ac") {
            unsigned long long online = 0;
            if (ReadU64File(base + "/online", online)) {
                acSeen = true;
                acConnected = acConnected || online != 0;
            }
        }
    }
    s.percent = haveCapacity ? capacity : -1;
    s.charging = haveStatus && charging;
    // Sem nenhum adaptador lido, mantem-se o chao "ligado a tomada": afirmar bateria fora
    // quando nada se leu seria o erro contrario.
    s.acOnline = acSeen ? acConnected : true;
    return s;
}

// ---------------------------------------------------------------------------
// Escrita por politica, com a ordem que o kernel aceita
// ---------------------------------------------------------------------------
// scaling_min_freq acima de scaling_max_freq e recusado com EINVAL. Ao descer o teto baixa-se
// o primeiro o chao; ao subir, sobe-se primeiro o teto. O resultado reportado e sempre o
// numero que ficou no kernel.
struct BandOutcome {
    size_t policies = 0;
    size_t refused = 0;
    size_t adjusted = 0;
    uint32_t lowestMaxPercent = 100;
    std::string firstErr;
};

unsigned long long ClampKHz(unsigned long long v, unsigned long long lo, unsigned long long hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// setMin=false preserva o chao atual (e o que SetProcessorThrottleLimit faz no Windows, onde
// so se escreve POWER_POLICY_MAX_AC); com setMin=true escreve-se o par, e o chao nunca pode
// ficar acima do teto -- se o chao antigo era mais alto que o teto novo, desce com ele.
void ApplyBandToPolicy(const std::string& policy, bool setMin, uint32_t wantMinPct, uint32_t wantMaxPct,
                       BandOutcome& out) {
    ++out.policies;
    CpufreqState st;
    if (!ReadPolicyState(policy, st) || st.cpuinfoMaxKHz == 0) {
        ++out.refused;
        if (out.firstErr.empty()) out.firstErr = policy + ": sem cpuinfo_max_freq legivel";
        return;
    }
    const unsigned long long ceiling = ClampKHz(PercentToKHz(st.cpuinfoMaxKHz, wantMaxPct),
                                                st.cpuinfoMinKHz, st.cpuinfoMaxKHz);
    const unsigned long long floorWanted = setMin
        ? PercentToKHz(st.cpuinfoMaxKHz, std::min(wantMinPct, wantMaxPct))
        : (st.minKHz ? st.minKHz : st.cpuinfoMinKHz);
    const unsigned long long floor = ClampKHz(floorWanted, st.cpuinfoMinKHz, ceiling);

    unsigned long long actualMin = 0, actualMax = 0;
    std::string err;
    WriteResult rMin = WriteResult::Confirmed, rMax = WriteResult::Confirmed;
    bool accepted = true;

    if (ceiling >= st.maxKHz) {
        rMax = WriteNumberAndVerify(policy + "/scaling_max_freq", ceiling, actualMax, err);
        accepted = (rMax != WriteResult::Refused);
        if (accepted && floor != st.minKHz) {
            rMin = WriteNumberAndVerify(policy + "/scaling_min_freq", floor, actualMin, err);
            accepted = (rMin != WriteResult::Refused);
        }
    } else {
        rMin = WriteNumberAndVerify(policy + "/scaling_min_freq", floor, actualMin, err);
        accepted = (rMin != WriteResult::Refused);
        if (accepted) {
            rMax = WriteNumberAndVerify(policy + "/scaling_max_freq", ceiling, actualMax, err);
            accepted = (rMax != WriteResult::Refused);
        }
    }

    if (!accepted) {
        ++out.refused;
        if (out.firstErr.empty()) {
            out.firstErr = policy + ": " + (rMax == WriteResult::Refused ? std::string("scaling_max_freq")
                                                                          : std::string("scaling_min_freq")) +
                " -> " + err + PrivilegeHint();
        }
        return;
    }
    if (rMin == WriteResult::Adjusted || rMax == WriteResult::Adjusted) ++out.adjusted;

    // Reler o que ficou no kernel, em vez de reportar o numero pedido.
    CpufreqState back;
    if (ReadPolicyState(policy, back) && back.cpuinfoMaxKHz) {
        const unsigned long long eff = back.maxKHz ? back.maxKHz : back.cpuinfoMaxKHz;
        out.lowestMaxPercent = std::min(out.lowestMaxPercent, KHzToPercent(eff, back.cpuinfoMaxKHz));
    } else {
        out.lowestMaxPercent = std::min(out.lowestMaxPercent, wantMaxPct);
    }
}

std::string DescribeOutcome(const BandOutcome& o, uint32_t requested) {
    std::ostringstream os;
    os << o.policies - o.refused << "/" << o.policies << " politica(s) cpufreq aplicaram "
       << requested << "%";
    if (o.adjusted) os << ", " << o.adjusted << " ajustada(s) pelo kernel para "
                       << o.lowestMaxPercent << "%";
    return os.str();
}

bool FailWith(const BandOutcome& o, const std::string& what, uint32_t requested) {
    std::ostringstream os;
    os << what << ": " << DescribeOutcome(o, requested);
    if (!o.firstErr.empty()) os << " -- primeiro erro: " << o.firstErr;
    OPIT_LOG_WARN("[POWER][Linux] " + os.str());
    return false;
}

// ---------------------------------------------------------------------------
// Nucleos: online/offline (o equivalente real do core parking)
// ---------------------------------------------------------------------------
struct CpuEntry {
    int id = 0;
    std::string onlineFile;   // vazio = nao hibernivel por sysfs (cpu0 nunca tem online)
    bool online = true;
    bool writable = false;
};

std::vector<CpuEntry> ScanCpuOnline() {
    const std::string root = EnvRoot("OPITCORE_CPU_ROOT", "/sys/devices/system/cpu");
    std::vector<CpuEntry> out;
    for (const std::string& n : ListNames(root)) {
        if (n.rfind("cpu", 0) != 0 || n.size() < 4) continue;
        const std::string ds = n.substr(3);
        if (ds.empty()) continue;
        bool digits = true;
        for (char c : ds) if (c < '0' || c > '9') { digits = false; break; }
        if (!digits) continue;
        CpuEntry e;
        e.id = std::atoi(ds.c_str());
        if (!DirExists(root + "/" + n)) continue;
        const std::string f = root + "/" + n + "/online";
        unsigned long long v = 0;
        if (!ReadU64File(f, v)) {
            // cpu0 (e kernels sem hotplug) nao tem o ficheiro: nao e controlavel, mas conta
            // no denominador para a percentagem nao prometer menos nucleos dos que ha.
            out.push_back(e);
            continue;
        }
        e.onlineFile = f;
        e.online = (v != 0);
        e.writable = (::access(f.c_str(), W_OK) == 0);
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const CpuEntry& a, const CpuEntry& b) { return a.id < b.id; });
    return out;
}

} // namespace

// ============================================================================
// Ciclo de vida
// ============================================================================
PowerManager::PowerManager() = default;

PowerManager::~PowerManager() {
    RestoreOriginalSettings();
}

bool PowerManager::Initialize() {
    if (m_initialized) return true;

    // Equivalente do unlock de core parking no registo: por cada cpuN offline, escrever 1 e
    // confirmar. Devolve false com WARN quando nao ha estado para confirmar (o caso WSL2 sem
    // root: os ficheiros existem mas nao sao escreviveis por este usuario).
    ElevationHelper::UnlockCoreParkingInRegistry();

    const auto policies = CpuFreqPolicies();
    if (!m_linuxCapturedState) {
        for (const std::string& p : policies) {
            CpufreqState st;
            if (!ReadPolicyState(p, st)) continue;
            LinuxPolicyState lp;
            lp.path = p;
            lp.governor = st.governor;
            lp.epp = st.epp;
            lp.hasEpp = st.hasEpp;
            lp.minKHz = st.minKHz;
            lp.maxKHz = st.maxKHz;
            m_linuxOriginalPolicies.push_back(lp);
        }
        m_linuxCapturedState = !m_linuxOriginalPolicies.empty();
    }

    const PowerScheme active = GetActiveScheme();
    m_originalSchemeGuid = active.guid;
    m_originalThrottleLimit = GetProcessorThrottleLimit();
    m_initialized = true;
    if (policies.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] sem politicas cpufreq neste kernel: os tetos de CPU e os "
                      "planos por governor ficam indisponiveis e nada e escrito em frequencia.");
    }
    return true;
}

// ============================================================================
// Planos = governor (+ EPP quando o driver expoe)
// ============================================================================
namespace {

struct ProfileSpec {
    const char* token;
    const char* label;
    const char* governorHint;
    const char* eppHint;
};

const ProfileSpec kProfiles[] = {
    {"linux:power-saver", "Plano Economia de Energia (Linux)", "powersave", "power"},
    {"linux:balanced", "Plano Equilibrado (Linux)", "powersave", "balance_performance"},
    {"linux:high-performance", "Plano Alto Desempenho (Linux)", "performance", "performance"},
};

// O governor pedido so se escreve se o kernel o listar -- escrever um nome que la nao esta
// daria EINVAL e uma mensagem de falha em vez de dizer qual governor existe.
std::string GovernorForProfile(const CpufreqState& st, const ProfileSpec& spec) {
    const std::string want = ToLower(spec.governorHint);
    for (const std::string& g : st.governors) {
        if (ToLower(g) == want) return g;
    }
    if (want == "powersave") {
        static const char* const fallback[] = {"conservative", "ondemand", "schedutil", "userspace"};
        for (const char* cand : fallback) {
            for (const std::string& g : st.governors) {
                if (ToLower(g) == cand) return g;
            }
        }
    }
    if (want == "performance") {
        for (const std::string& g : st.governors) {
            if (ToLower(g) == "performance") return g;
        }
    }
    return std::string();
}

std::string EppForProfile(const CpufreqState& st, const ProfileSpec& spec) {
    if (!st.hasEpp) return std::string();
    const std::string want = ToLower(spec.eppHint);
    for (const std::string& e : st.epps) {
        if (ToLower(e) == want) return e;
    }
    return std::string();
}

const ProfileSpec* ProfileByToken(const std::string& guid) {
    for (const ProfileSpec& p : kProfiles) {
        if (guid == p.token) return &p;
    }
    return nullptr;
}

// Escreve governor (e EPP, se existir) em todas as politicas. Ordem: governor primeiro,
// porque em intel_pstate o EPP so se escreve com governor powersave.
bool ApplyProfileToAllPolicies(const ProfileSpec& spec, std::string& outDetail) {
    const auto policies = CpuFreqPolicies();
    if (policies.empty()) {
        outDetail = "nenhuma politica cpufreq neste kernel";
        return false;
    }
    size_t govOk = 0;
    size_t eppOk = 0;
    size_t eppTried = 0;
    std::string govUsed;
    std::string eppUsed;
    std::string firstErr;

    for (const std::string& p : policies) {
        CpufreqState st;
        if (!ReadPolicyState(p, st)) continue;
        const std::string gov = GovernorForProfile(st, spec);
        if (gov.empty()) {
            if (firstErr.empty()) {
                firstErr = p + ": governor '" + spec.governorHint + "' nao esta em "
                    "scaling_available_governors";
            }
            continue;
        }
        std::string err;
        if (WriteTextAndVerify(p + "/scaling_governor", gov, err) != WriteResult::Refused) {
            ++govOk;
            govUsed = gov;
        } else if (firstErr.empty()) {
            firstErr = p + "/scaling_governor -> " + err + PrivilegeHint();
        }
        const std::string epp = EppForProfile(st, spec);
        if (!epp.empty()) {
            ++eppTried;
            std::string errEpp;
            if (WriteTextAndVerify(p + "/energy_performance_preference", epp, errEpp) !=
                WriteResult::Refused) {
                ++eppOk;
                eppUsed = epp;
            }
        }
    }

    std::ostringstream os;
    if (govOk == 0) {
        outDetail = firstErr.empty() ? "nenhum governor aplicavel nas politicas lidas" : firstErr;
        return false;
    }
    os << "governor " << govUsed << " aplicado em " << govOk << "/" << policies.size()
       << " politica(s)";
    if (eppTried) os << ", EPP " << eppUsed << " em " << eppOk << "/" << eppTried;
    if (govOk < policies.size()) os << " (" << (policies.size() - govOk) << " recusada(s))";
    outDetail = os.str();
    return govOk == policies.size();
}

} // namespace

PowerScheme PowerManager::GetActiveScheme() {
    PowerScheme ps;
    const auto policies = CpuFreqPolicies();
    if (policies.empty()) return ps;

    CpufreqState st;
    if (!ReadPolicyState(policies[0], st)) return ps;

    ps.isActive = true;
    const std::string gov = ToLower(st.governor);
    if (gov == "performance") {
        ps.guid = kProfiles[2].token;
        ps.name = kProfiles[2].label;
    } else if (!st.epp.empty() && ToLower(st.epp) == "power") {
        ps.guid = kProfiles[0].token;
        ps.name = kProfiles[0].label;
    } else if (!st.epp.empty() && ToLower(st.epp) == "performance") {
        ps.guid = kProfiles[2].token;
        ps.name = kProfiles[2].label;
    } else {
        ps.guid = kProfiles[1].token;
        ps.name = std::string("Plano ") + (st.governor.empty() ? std::string("ativo") : st.governor);
        if (!st.epp.empty()) ps.name += " (" + st.epp + ")";
    }
    return ps;
}

std::vector<PowerScheme> PowerManager::ListSchemes() {
    std::vector<PowerScheme> out;
    const auto policies = CpuFreqPolicies();
    if (policies.empty()) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            OPIT_LOG_WARN("[POWER][Linux] ListSchemes: o kernel nao expoe cpufreq, logo nao ha "
                          "governors para listar; a lista de planos fica vazia por facto medido.");
        }
        return out;
    }
    CpufreqState st;
    if (!ReadPolicyState(policies[0], st)) return out;

    const PowerScheme ativo = GetActiveScheme();
    for (const ProfileSpec& spec : kProfiles) {
        const std::string gov = GovernorForProfile(st, spec);
        if (gov.empty()) continue;
        PowerScheme ps;
        ps.guid = spec.token;
        ps.name = spec.label;
        if (st.hasEpp) {
            const std::string epp = EppForProfile(st, spec);
            if (!epp.empty()) ps.name += " [" + epp + "]";
        }
        ps.isActive = (!ativo.guid.empty() && ativo.guid == ps.guid);
        out.push_back(ps);
    }
    return out;
}

bool PowerManager::SetActiveScheme(const std::string& schemeGuid) {
    const ProfileSpec* spec = ProfileByToken(schemeGuid);
    if (!spec) {
        OPIT_LOG_WARN("[POWER][Linux] SetActiveScheme: identificador de plano desconhecido (" +
                      schemeGuid + ").");
        return false;
    }
    std::string detail;
    const bool all = ApplyProfileToAllPolicies(*spec, detail);
    if (!all) {
        OPIT_LOG_WARN("[POWER][Linux] SetActiveScheme(" + schemeGuid + "): " + detail);
    }
    const bool eco = (spec == &kProfiles[0]);
    ConfigureIdleDeviceSuspension(schemeGuid, eco && m_pauseIdleDevicesEco);
    return all;
}

std::string PowerManager::FindEcoScheme() {
    const auto schemes = ListSchemes();
    for (const auto& s : schemes) if (s.guid == kProfiles[0].token) return s.guid;
    for (const auto& s : schemes) {
        const std::string lower = ToLower(s.name);
        if (lower.find("economia") != std::string::npos || lower.find("power") != std::string::npos ||
            lower.find("saver") != std::string::npos || lower.find("eco") != std::string::npos) {
            return s.guid;
        }
    }
    return std::string();
}

std::string PowerManager::FindBalancedScheme() {
    const auto schemes = ListSchemes();
    for (const auto& s : schemes) if (s.guid == kProfiles[1].token) return s.guid;
    for (const auto& s : schemes) {
        const std::string lower = ToLower(s.name);
        if (lower.find("equilibrado") != std::string::npos || lower.find("balanceado") != std::string::npos ||
            lower.find("balanced") != std::string::npos) {
            return s.guid;
        }
    }
    return std::string();
}

std::string PowerManager::FindHighPerformanceScheme() {
    const auto schemes = ListSchemes();
    for (const auto& s : schemes) if (s.guid == kProfiles[2].token) return s.guid;
    for (const auto& s : schemes) {
        const std::string lower = ToLower(s.name);
        if (lower.find("alto") != std::string::npos || lower.find("desempenho") != std::string::npos ||
            lower.find("performance") != std::string::npos) {
            return s.guid;
        }
    }
    return std::string();
}

bool PowerManager::ForceEcoPlan(bool enable, std::string& outAppliedName, std::string& outStatusMsg) {
    if (enable) {
        const PowerScheme current = GetActiveScheme();
        const std::string ecoGuid = FindEcoScheme();
        if (ecoGuid.empty()) {
            outStatusMsg = "Indisponivel: nenhum plano economico compativel localizado no Windows.";
            OPIT_LOG_WARN("[POWER][Linux] Forcar plano economico: nenhum governor de economia "
                          "exposto por este kernel (medido, nao suposto).");
            return false;
        }
        if (!m_isEcoPlanForced) m_preForcedEcoSchemeGuid = current.guid;

        if (!SetActiveScheme(ecoGuid)) {
            outStatusMsg = "Falha ao aplicar plano economico no Windows.";
            return false;
        }
        const PowerScheme verified = GetActiveScheme();
        m_isEcoPlanForced = true;
        outAppliedName = verified.name;
        outStatusMsg = "Aplicado com sucesso: " + verified.name;
        return true;
    }
    if (m_isEcoPlanForced && !m_preForcedEcoSchemeGuid.empty()) {
        SetActiveScheme(m_preForcedEcoSchemeGuid);
        const PowerScheme restored = GetActiveScheme();
        outAppliedName = restored.name;
        outStatusMsg = "Restaurado plano anterior: " + restored.name;
    } else {
        outStatusMsg = "Imposicao desativada.";
    }
    m_isEcoPlanForced = false;
    return true;
}

// ============================================================================
// Tetos de CPU -- a funcionalidade central
// ============================================================================
uint32_t PowerManager::GetProcessorThrottleLimit() {
    const auto policies = CpuFreqPolicies();
    bool any = false;
    uint32_t worst = 100;
    for (const std::string& p : policies) {
        CpufreqState st;
        if (!ReadPolicyState(p, st) || st.cpuinfoMaxKHz == 0) continue;
        const unsigned long long eff = st.maxKHz ? st.maxKHz : st.cpuinfoMaxKHz;
        const uint32_t pct = KHzToPercent(eff, st.cpuinfoMaxKHz);
        worst = any ? std::min(worst, pct) : pct;
        any = true;
    }
    // Sem nenhuma politica lida nao ha teto escrito por cpufreq; 100 e a resposta que nao
    // acusa uma limitacao que nao existe (o WARN de Initialize ja disse o que falta).
    return any ? worst : 100;
}

bool PowerManager::SetProcessorThrottleLimit(uint32_t percent) {
    if (percent > 100) percent = 100;
    if (percent < 20) percent = 20;   // mesmo piso de seguranca do ramo Windows

    const auto policies = CpuFreqPolicies();
    if (policies.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] SetProcessorThrottleLimit: sem cpufreq, nada escrito.");
        return false;
    }
    if (GetProcessorThrottleLimit() == percent) return true;

    BandOutcome o;
    for (const std::string& p : policies) ApplyBandToPolicy(p, false, 0, percent, o);
    if (o.refused == 0 && o.policies > 0) return true;
    return FailWith(o, "SetProcessorThrottleLimit", percent);
}

bool PowerManager::SetProcessorThrottleRange(uint32_t minPercent, uint32_t maxPercent) {
    if (minPercent > 100 || maxPercent > 100) return false;
    if (minPercent > maxPercent) {
        OPIT_LOG_WARN("[POWER][Linux] SetProcessorThrottleRange: minimo acima do maximo, "
                      "rejeitado sem correcao.");
        return false;
    }
    const auto policies = CpuFreqPolicies();
    if (policies.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] SetProcessorThrottleRange: sem cpufreq, nada escrito.");
        return false;
    }
    BandOutcome o;
    for (const std::string& p : policies) ApplyBandToPolicy(p, true, minPercent, maxPercent, o);
    if (o.refused == 0 && o.policies > 0) return true;
    return FailWith(o, "SetProcessorThrottleRange", maxPercent);
}

// ============================================================================
// Core parking = nucleos offline, com a lista exata do que esta instancia desligou
// ============================================================================
bool PowerManager::SetCoreParking(uint32_t minCoresPercent) {
    if (minCoresPercent > 100) minCoresPercent = 100;
    if (minCoresPercent < 5) minCoresPercent = 5;

    auto cpus = ScanCpuOnline();
    if (cpus.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] SetCoreParking: nenhum cpu encontrado em " +
            EnvRoot("OPITCORE_CPU_ROOT", "/sys/devices/system/cpu") + "; nada escrito.");
        return false;
    }

    long total = 0, online = 0;
    for (const auto& c : cpus) { ++total; if (c.online) ++online; }
    const long alvo = std::max(1L, (total * static_cast<long>(minCoresPercent) + 50L) / 100L);

    if (alvo >= online) {
        // Pedido igual ou acima do que esta ligado: so se religam os que esta instancia
        // desligou -- nao se acorda um nucleo que ja estava offline antes de abrirmos o
        // programa, porque esse estado nao e nosso para desfazer.
        const std::string root = EnvRoot("OPITCORE_CPU_ROOT", "/sys/devices/system/cpu");
        for (size_t i = m_linuxParkedCpus.size(); i-- > 0; ) {
            const int id = m_linuxParkedCpus[i];
            const std::string f = root + "/cpu" + std::to_string(id) + "/online";
            unsigned long long back = 0;
            std::string err;
            if (WriteNumberAndVerify(f, 1ULL, back, err) == WriteResult::Refused) {
                OPIT_LOG_WARN("[POWER][Linux] SetCoreParking: cpu" + std::to_string(id) +
                              " nao religou -> " + err);
                continue;
            }
            m_linuxParkedCpus.erase(m_linuxParkedCpus.begin() + static_cast<long>(i));
        }
        return true;
    }

    // Desligar do id mais alto para o mais baixo, como o escalonador trata os nucleos de
    // menor prioridade; e nao se desliga um cpu que nao tenha o ficheiro escrevivel.
    size_t recusas = 0;
    for (auto it = cpus.end(); it != cpus.begin(); ) {
        --it;
        if (online <= alvo) break;
        CpuEntry& c = *it;
        if (c.onlineFile.empty() || !c.online) continue;
        if (!c.writable) {
            ++recusas;
            continue;
        }
        unsigned long long back = 0;
        std::string err;
        const WriteResult r = WriteNumberAndVerify(c.onlineFile, 0ULL, back, err);
        if (r == WriteResult::Refused) {
            ++recusas;
            OPIT_LOG_WARN("[POWER][Linux] SetCoreParking: cpu" + std::to_string(c.id) +
                          " -> " + err);
            continue;
        }
        if (back == 0) {
            m_linuxParkedCpus.push_back(c.id);
            --online;
        } else {
            ++recusas;
            OPIT_LOG_WARN("[POWER][Linux] SetCoreParking: cpu" + std::to_string(c.id) +
                          " pediu offline mas o kernel mantem " + std::to_string(back) +
                          " (nao pode conter tarefas).");
        }
    }

    if (recusas) {
        OPIT_LOG_WARN("[POWER][Linux] SetCoreParking(" + std::to_string(minCoresPercent) +
                      "%): " + std::to_string(recusas) + " nucleo(s) nao desligaram" +
                      PrivilegeHint() + "; ligados ficaram " + std::to_string(online) +
                      " de " + std::to_string(total) + ".");
        return false;
    }
    return true;
}

// ============================================================================
// Leitura/escrita por plano (linhas AC e DC)
// ============================================================================
namespace {

// Qual das duas linhas do Windows se aplica quando o kernel so tem uma faixa global. Nao se
// inventa uma linha: se nao ha power_supply legivel, diz-se que nada se leu e usa-se o valor
// AC (o do pedido principal do programa) como chao.
struct SupplyLine {
    bool known = false;
    bool ac = true;
    std::string describe;
    uint32_t pick(uint32_t acVal, uint32_t dcVal) const { return ac ? acVal : dcVal; }
};

SupplyLine ReadSupplyLine() {
    const PowerSupplyScan s = ScanPowerSupplies();
    SupplyLine l;
    l.known = s.anyRead;
    l.ac = s.acOnline;
    l.describe = s.anyRead
        ? (std::string("a linha atual e ") + (s.acOnline ? "AC (tomada)." : "DC (bateria)."))
        : std::string("nenhum power_supply legivel, por isso usa-se o valor da linha AC.");
    return l;
}

} // namespace

bool PowerManager::ReadSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t& minAc,
                                               uint32_t& maxAc, uint32_t& minDc, uint32_t& maxDc) {
    minAc = 5; maxAc = 100; minDc = 5; maxDc = 100;
    const auto policies = CpuFreqPolicies();
    if (policies.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] ReadSchemeProcessorThrottle(" + schemeGuid +
                      "): sem cpufreq, sem leitura real.");
        return false;
    }
    uint32_t loMin = 100, hiMax = 0;
    size_t lidas = 0;
    for (const std::string& p : policies) {
        CpufreqState st;
        if (!ReadPolicyState(p, st) || st.cpuinfoMaxKHz == 0) continue;
        const uint32_t mn = KHzToPercent(st.minKHz ? st.minKHz : st.cpuinfoMinKHz, st.cpuinfoMaxKHz);
        const uint32_t mx = KHzToPercent(st.maxKHz ? st.maxKHz : st.cpuinfoMaxKHz, st.cpuinfoMaxKHz);
        loMin = std::min(loMin, mn);
        hiMax = std::max(hiMax, mx);
        ++lidas;
    }
    if (lidas == 0) {
        OPIT_LOG_WARN("[POWER][Linux] ReadSchemeProcessorThrottle(" + schemeGuid +
                      "): nenhuma politica devolveu faixas legiveis.");
        return false;
    }
    // O kernel tem UMA faixa global por politica, nao duas linhas por plano: as duas linhas
    // recebem o mesmo numero lido e e isso que a UI mostra.
    minAc = minDc = loMin;
    maxAc = maxDc = hiMax;
    return true;
}

bool PowerManager::WriteSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t minAc, uint32_t maxAc,
                                                uint32_t minDc, uint32_t maxDc) {
    if (minAc > 100 || maxAc > 100 || minDc > 100 || maxDc > 100) return false;
    if (minAc > maxAc || minDc > maxDc) {
        OPIT_LOG_WARN("[POWER][Linux] WriteSchemeProcessorThrottle(" + schemeGuid +
                      "): minimo acima do maximo, rejeitado.");
        return false;
    }
    if (minAc == minDc && maxAc == maxDc) {
        return SetProcessorThrottleRange(minAc, maxAc);
    }
    const SupplyLine line = ReadSupplyLine();
    OPIT_LOG_WARN("[POWER][Linux] WriteSchemeProcessorThrottle(" + schemeGuid + "): o kernel nao tem "
                  "linhas AC/DC separadas para teto de CPU; " + line.describe + " Aplicou-se " +
                  std::to_string(line.pick(minAc, minDc)) + ".." + std::to_string(line.pick(maxAc, maxDc)) +
                  "% e a outra linha ficou por aplicar.");
    return SetProcessorThrottleRange(line.pick(minAc, minDc), line.pick(maxAc, maxDc));
}

bool PowerManager::ReadCoreParking(const std::string& schemeGuid, uint32_t& acVal, uint32_t& dcVal) {
    const auto cpus = ScanCpuOnline();
    if (cpus.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] ReadCoreParking(" + schemeGuid +
                      "): nenhum cpu com estado legivel, sem leitura real.");
        return false;
    }
    long total = 0, online = 0;
    for (const auto& c : cpus) { ++total; if (c.online) ++online; }
    const uint32_t pct = total > 0 ? static_cast<uint32_t>((online * 100L + total / 2) / total) : 100;
    // Estado global do kernel, nao por linha: a mesma percentagem vai para as duas colunas.
    acVal = dcVal = pct;
    return true;
}

bool PowerManager::SetCoreParkingLines(const std::string& schemeGuid, uint32_t acPercent, uint32_t dcPercent) {
    if (acPercent == dcPercent) return SetCoreParking(acPercent);
    const SupplyLine line = ReadSupplyLine();
    OPIT_LOG_WARN("[POWER][Linux] SetCoreParkingLines(" + schemeGuid + "): o kernel nao separa AC de DC "
                  "para estacionamento de nucleos; " + line.describe);
    return SetCoreParking(line.pick(acPercent, dcPercent));
}

bool PowerManager::SetCoreParkingForScheme(const std::string& schemeGuid, uint32_t minCoresPercent) {
    uint32_t ac = 0, dc = 0;
    if (ReadCoreParking(schemeGuid, ac, dc) && ac == minCoresPercent) return true;
    return SetCoreParking(minCoresPercent);
}

bool PowerManager::ForceMaxCoreParking(bool enable, uint32_t& outAppliedVal) {
    if (enable) {
        uint32_t curAc = 0, curDc = 0;
        if (ReadCoreParking(m_originalSchemeGuid, curAc, curDc) && !m_isMaxCoreParkingForced) {
            m_preForcedCoreParkingAc = curAc;
        }
        const bool ok = SetCoreParking(5);
        m_isMaxCoreParkingForced = true;
        uint32_t nowAc = 5, nowDc = 5;
        ReadCoreParking(m_originalSchemeGuid, nowAc, nowDc);
        outAppliedVal = nowAc;
        return ok;
    }
    if (m_isMaxCoreParkingForced) {
        SetCoreParking(m_preForcedCoreParkingAc);
        uint32_t nowAc = 100, nowDc = 100;
        ReadCoreParking(m_originalSchemeGuid, nowAc, nowDc);
        outAppliedVal = nowAc;
    } else {
        outAppliedVal = m_preForcedCoreParkingAc;
    }
    m_isMaxCoreParkingForced = false;
    return true;
}

// ============================================================================
// Discos
// ============================================================================
uint32_t PowerManager::GetDiskIdleTimeout(bool dc) {
    // O timeout de inatividade do disco nao tem interface em sysfs (vive no hdparm, util
    // externo que nao esta instalado). Devolver um numero seria dizer "lido" de um sitio que
    // nao existe; 0 e o valor neutro e o motivo fica dito uma vez.
    (void)dc;
    static bool warned = false;
    if (!warned) {
        warned = true;
        OPIT_LOG_WARN("[POWER][Linux] GetDiskIdleTimeout: sem interface sysfs para o timeout de "
                      "disco (hdparm ausente); 0 = nenhum valor lido, nao 'nunca desligar'.");
    }
    return 0;
}

bool PowerManager::SetDiskIdleTimeout(uint32_t seconds, bool dc) {
    (void)seconds;
    (void)dc;
    OPIT_LOG_WARN("[POWER][Linux] SetDiskIdleTimeout: o kernel nao expoe o timeout de disco; "
                  "nada escrito (a protecao contra desligar existe via runtime PM).");
    return false;
}

namespace {

// power/control de runtime PM por bloco. "on" = o kernel nunca suspende o dispositivo, que e
// o equivalente do "Nunca desligar" do plano Economico do Windows. No kernel medido existem
// sda..sdd com o ficheiro; loop*/ram* nao o tem e ficam fora da lista por facto lido.
std::vector<std::string> BlockPowerControlFiles() {
    const std::string root = EnvRoot("OPITCORE_BLOCK_ROOT", "/sys/class/block");
    std::vector<std::string> out;
    for (const std::string& n : ListNames(root)) {
        std::string v;
        const std::string f = root + "/" + n + "/device/power/control";
        if (ReadStrFile(f, v)) out.push_back(f);
    }
    return out;
}

// Captura o estado exato de cada ficheiro antes da primeira escrita desta instancia.
void CaptureOriginals(std::vector<std::pair<std::string, std::string>>& sink,
                      const std::vector<std::string>& files) {
    for (const std::string& f : files) {
        bool already = false;
        for (const auto& e : sink) if (e.first == f) { already = true; break; }
        if (already) continue;
        std::string v;
        if (ReadStrFile(f, v)) sink.emplace_back(f, v);
    }
}

size_t WriteControlOn(std::vector<std::pair<std::string, std::string>>& originals,
                      const std::vector<std::string>& files, const char* value) {
    CaptureOriginals(originals, files);
    size_t ok = 0;
    for (const std::string& f : files) {
        std::string err;
        if (WriteTextAndVerify(f, value, err) != WriteResult::Refused) ++ok;
    }
    return ok;
}

void RestoreControl(std::vector<std::pair<std::string, std::string>>& originals) {
    for (auto it = originals.begin(); it != originals.end(); ) {
        std::string err;
        if (WriteTextAndVerify(it->first, it->second, err) != WriteResult::Refused) {
            it = originals.erase(it);
        } else {
            OPIT_LOG_WARN("[POWER][Linux] restaurar " + it->first + " para '" + it->second +
                          "' falhou -> " + err);
            ++it;
        }
    }
}

} // namespace

bool PowerManager::PreventDiskPowerDown(bool prevent) {
    const auto files = BlockPowerControlFiles();
    if (files.empty()) {
        OPIT_LOG_WARN("[POWER][Linux] PreventDiskPowerDown: nenhum bloco com power/control "
                      "legivel em " + EnvRoot("OPITCORE_BLOCK_ROOT", "/sys/class/block") +
                      "; nada escrito.");
        return false;
    }
    if (prevent) {
        const size_t ok = WriteControlOn(m_linuxBlockControlOriginals, files, "on");
        m_isDiskPowerDownPrevented = ok > 0;
        if (ok < files.size()) {
            OPIT_LOG_WARN("[POWER][Linux] PreventDiskPowerDown: " +
                          std::to_string(files.size() - ok) + "/" + std::to_string(files.size()) +
                          " blocos recusaram 'on'" + PrivilegeHint() + ".");
            return false;
        }
        return true;
    }
    RestoreControl(m_linuxBlockControlOriginals);
    // Fica marcado como ainda prevenido enquanto houver ficheiros que o kernel nao deixou
    // voltar ao valor original -- restaurar a medo nao e restaurar.
    m_isDiskPowerDownPrevented = !m_linuxBlockControlOriginals.empty();
    return !m_isDiskPowerDownPrevented;
}

// ============================================================================
// Suspensao de periferos ociosos (USB + PCIe ASPM)
// ============================================================================
namespace {

std::vector<std::string> UsbPowerControlFiles() {
    const std::string root = EnvRoot("OPITCORE_USB_ROOT", "/sys/bus/usb/devices");
    std::vector<std::string> out;
    for (const std::string& n : ListNames(root)) {
        std::string v;
        const std::string f = root + "/" + n + "/power/control";
        if (ReadStrFile(f, v)) out.push_back(f);
    }
    return out;
}

std::string AspmPolicyFile() {
    return EnvRoot("OPITCORE_ASPM_POLICY", "/sys/module/pcie_aspm/parameters/policy");
}

// O ficheiro devolve a lista com o ativo entre colchetes: "[default] performance powersave
// powersupersave". Extrai-se o ativo e as opcoes reais, sem assumirem nomes fixos.
struct AspmPolicy {
    std::vector<std::string> options;
    std::string current;
    bool readable = false;
};

AspmPolicy ReadAspm() {
    AspmPolicy a;
    std::string raw;
    if (!ReadStrFile(AspmPolicyFile(), raw)) return a;
    a.readable = true;
    a.options = SplitWords(raw);
    const size_t open = raw.find('[');
    const size_t close = raw.find(']');
    if (open != std::string::npos && close > open) a.current = raw.substr(open + 1, close - open - 1);
    for (std::string& o : a.options) {
        o.erase(std::remove_if(o.begin(), o.end(), [](char c) { return c == '[' || c == ']'; }), o.end());
    }
    return a;
}

std::string PickAspm(const AspmPolicy& a, std::initializer_list<const char*> wanted) {
    for (const char* w : wanted) {
        for (const std::string& o : a.options) {
            if (ToLower(o) == ToLower(w)) return o;
        }
    }
    return std::string();
}

} // namespace

bool PowerManager::ConfigureIdleDeviceSuspension(const std::string& schemeGuid, bool pauseIdleDevices) {
    m_pauseIdleDevicesEco = pauseIdleDevices;

    const auto usbFiles = UsbPowerControlFiles();
    const AspmPolicy aspm = ReadAspm();
    if (usbFiles.empty() && !aspm.readable) {
        OPIT_LOG_WARN("[POWER][Linux] ConfigureIdleDeviceSuspension(" + schemeGuid +
                      "): nem USB (/sys/bus/usb/devices) nem pcie_aspm estao expostos neste "
                      "kernel; nada escrito.");
        return false;
    }

    if (!m_linuxAspmCaptured && aspm.readable) {
        m_linuxAspmOriginalPolicy = aspm.current;
        m_linuxAspmCaptured = true;
    }

    size_t usbOk = 0;
    if (!usbFiles.empty()) {
        usbOk = WriteControlOn(m_linuxUsbControlOriginals, usbFiles, pauseIdleDevices ? "auto" : "on");
        if (usbOk < usbFiles.size()) {
            OPIT_LOG_WARN("[POWER][Linux] ConfigureIdleDeviceSuspension: " +
                          std::to_string(usbFiles.size() - usbOk) + "/" +
                          std::to_string(usbFiles.size()) + " dispositivo(s) USB recusaram a "
                          "escrita" + PrivilegeHint() + ".");
        }
    }

    size_t aspmWrites = 0;
    if (aspm.readable) {
        const std::string want = pauseIdleDevices
            ? PickAspm(aspm, {"powersave", "powersupersave", "default"})
            : m_linuxAspmOriginalPolicy;
        if (!want.empty()) {
            std::string err;
            const WriteResult r = WriteTextAndVerify(AspmPolicyFile(), want, err);
            if (r == WriteResult::Refused) {
                OPIT_LOG_WARN("[POWER][Linux] ConfigureIdleDeviceSuspension: ASPM nao aceitou '" +
                              want + "' -> " + err + PrivilegeHint() + ".");
            } else {
                aspmWrites = 1;
            }
        }
    }

    return (usbOk > 0 || aspmWrites > 0);
}

// ============================================================================
// Bateria / alimentacao
// ============================================================================
bool SystemHasBattery() {
    return ScanPowerSupplies().hasBattery;
}

BatteryStatusInfo PowerManager::GetBatteryStatus() {
    BatteryStatusInfo info;
    const PowerSupplyScan s = ScanPowerSupplies();
    if (!s.anyRead) {
        // Mesmo caso do BatteryFlag 128/255 no Windows (VM e maquina sem o subsistema):
        // declara-se que nao ha bateria lida em vez de um 100% vindo de lado nenhum.
        info.hasBattery = false;
        info.isAcOnline = true;
        info.batteryPercent = -1;
        info.isCharging = false;
        return info;
    }
    info.hasBattery = s.hasBattery;
    info.isAcOnline = s.acOnline;
    info.batteryPercent = s.percent;
    info.isCharging = s.charging;
    return info;
}

// ============================================================================
// Restauracao do estado original
// ============================================================================
void PowerManager::RestoreOriginalSettings() {
    if (!m_initialized) return;

    if (m_isDiskPowerDownPrevented) PreventDiskPowerDown(false);
    if (!m_linuxUsbControlOriginals.empty()) RestoreControl(m_linuxUsbControlOriginals);

    // O plano Economico tambem mexe no ASPM, e o DrawControl acima nao o cobre: sem este
    // passo a maquina fica com "powersave" depois de o programa fechar (medido no fixture).
    if (m_linuxAspmCaptured && !m_linuxAspmOriginalPolicy.empty()) {
        const AspmPolicy agora = ReadAspm();
        if (agora.readable && agora.current != m_linuxAspmOriginalPolicy) {
            std::string err;
            if (WriteTextAndVerify(AspmPolicyFile(), m_linuxAspmOriginalPolicy, err) == WriteResult::Refused) {
                OPIT_LOG_WARN("[POWER][Linux] RestoreOriginalSettings: ASPM nao voltou a '" +
                              m_linuxAspmOriginalPolicy + "' -> " + err + PrivilegeHint());
            } else {
                m_linuxAspmCaptured = false;
                m_linuxAspmOriginalPolicy.clear();
            }
        } else {
            m_linuxAspmCaptured = false;
            m_linuxAspmOriginalPolicy.clear();
        }
    }

    // O desenho Windows restaurava escrevendo o GUID do plano; aqui restaura-se o par
    // governor+EPP exato que estava no kernel antes da primeira escrita, porque o mesmo GUID
    // de perfil pode corresponder a outro governor quando o kernel nao tem "performance".
    bool restoredProfile = false;
    if (!m_linuxOriginalPolicies.empty()) {
        size_t ok = 0;
        for (const LinuxPolicyState& lp : m_linuxOriginalPolicies) {
            std::string err;
            if (!lp.governor.empty() &&
                WriteTextAndVerify(lp.path + "/scaling_governor", lp.governor, err) != WriteResult::Refused) {
                ++ok;
            }
            if (lp.hasEpp && !lp.epp.empty()) {
                WriteTextAndVerify(lp.path + "/energy_performance_preference", lp.epp, err);
            }
        }
        restoredProfile = (ok == m_linuxOriginalPolicies.size() && ok > 0);
    }
    if (!restoredProfile && !m_originalSchemeGuid.empty()) {
        SetActiveScheme(m_originalSchemeGuid);
    }

    // A faixa de frequencias volta como o PAR exato que se leu, nao como percentagem:
    // SetProcessorThrottleLimit so escreve o teto e deixaria o chao onde a ultima escrita o
    // deixou (o que se via no fixture: min preso em 940 MHz depois de um Range(30,60)).
    // Ordem = a mesma do ApplyBandToPolicy, porque o kernel recusa min > max.
    if (!m_linuxOriginalPolicies.empty()) {
        for (const LinuxPolicyState& lp : m_linuxOriginalPolicies) {
            CpufreqState st;
            if (!ReadPolicyState(lp.path, st)) continue;
            unsigned long long dummy = 0;
            std::string err;
            if (lp.maxKHz >= st.maxKHz) {
                if (lp.maxKHz) WriteNumberAndVerify(lp.path + "/scaling_max_freq", lp.maxKHz, dummy, err);
                if (lp.minKHz) WriteNumberAndVerify(lp.path + "/scaling_min_freq", lp.minKHz, dummy, err);
            } else {
                if (lp.minKHz) WriteNumberAndVerify(lp.path + "/scaling_min_freq", lp.minKHz, dummy, err);
                if (lp.maxKHz) WriteNumberAndVerify(lp.path + "/scaling_max_freq", lp.maxKHz, dummy, err);
            }
        }
    }

    // Nucleos por esta instancia desligados religam-se sempre, e por ultimo: um cpu offline
    // nao recebe nada do plano nem do teto restaurado.
    if (!m_linuxParkedCpus.empty()) {
        const std::string root = EnvRoot("OPITCORE_CPU_ROOT", "/sys/devices/system/cpu");
        for (size_t i = m_linuxParkedCpus.size(); i-- > 0; ) {
            const int id = m_linuxParkedCpus[i];
            const std::string f = root + "/cpu" + std::to_string(id) + "/online";
            unsigned long long back = 0;
            std::string err;
            if (WriteNumberAndVerify(f, 1ULL, back, err) == WriteResult::Refused) {
                OPIT_LOG_WARN("[POWER][Linux] RestoreOriginalSettings: cpu" + std::to_string(id) +
                              " nao religou -> " + err);
                continue;
            }
            m_linuxParkedCpus.erase(m_linuxParkedCpus.begin() + static_cast<long>(i));
        }
    }
}

} // namespace opitcore
#endif // !_WIN32 (gemelo de PowerManager.cpp para cpufreq + sysfs)
