// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

// Gemelo Linux do ProcessManager. O contrato e o mesmo cabecalo partilhado
// (platform/windows/ProcessManager.h), incluido pelo caminho a partir de src: em Linux nao ha
// declaracao propria desta classe para nao haver duas definicoes do mesmo nome no mesmo
// programa. O que muda e so a origem de cada numero: /proc no lugar do CreateToolhelp32Snapshot,
// setpriority/sched_* no lugar do SetPriorityClass, ioprio_set no lugar do EcoQoS e X11 no lugar
// do EnumWindows.
#include "platform/windows/ProcessManager.h"
// Os seis degraus da escada vivem em ProcessGovernor.h (no Windows vem do windows.h, em Linux
// sao os #define do mesmo cabecalo). Incluir daqui e o que mantem os numeros iguais nos dois
// sistemas, que e a razao de eles estarem definidos uma so vez.
#include "platform/windows/ProcessGovernor.h"
#include "platform/linux/LinuxProcUtil.h"
#include "utils/StringUtils.h"
#include "core/Logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <mutex>
#include <sched.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace opitcore {
namespace {

// A escada de prioridades do Windows traduzida para `nice`, na mesma ordem (rank 0..5). Os
// numeros de escrita vao de 19 (Ociosa) a -10 (Alta); Tempo Real nao e escrito por nice mas
// recusado, exactamente como no Windows, onde tambem e recusado por estabilidade do sistema.
int NiceFromPriorityClass(uint32_t priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return 19;
        case BELOW_NORMAL_PRIORITY_CLASS: return 7;
        case NORMAL_PRIORITY_CLASS: return 0;
        case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
        case HIGH_PRIORITY_CLASS: return -10;
        case REALTIME_PRIORITY_CLASS: return 0;
        default: return 0;
    }
}

// Leitura pelo contrario: o que esta escrito em nice e devolvido no degrau mais proximo da
// escada. Nao e um facto do kernel, e a nossa régua, por isso esta aqui e nao num cabecalho.
uint32_t PriorityClassFromNice(int nice) {
    if (nice >= 13) return IDLE_PRIORITY_CLASS;
    if (nice >= 3) return BELOW_NORMAL_PRIORITY_CLASS;
    if (nice >= -3) return NORMAL_PRIORITY_CLASS;
    if (nice >= -8) return ABOVE_NORMAL_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS;
}

const char* BaseName(const std::string& s) {
    const size_t pos = s.find_last_of('/');
    return pos == std::string::npos ? s.c_str() : s.c_str() + pos + 1;
}

std::string ExePath(uint32_t pid) {
    char link[64];
    std::snprintf(link, sizeof(link), "/proc/%u/exe", pid);
    char buf[4096];
    const ssize_t n = ::readlink(link, buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    // Um binario apagado durante a execucao fica com " (desativado)" sufixado no link; sem
    // isto o nome sairia com espaco e nenhuma regra por nome casaria.
    std::string p(buf);
    const std::string dead = " (deleted)";
    if (p.size() > dead.size() && p.compare(p.size() - dead.size(), dead.size(), dead) == 0) {
        p.erase(p.size() - dead.size());
    }
    return p;
}

std::string CommName(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    return lnx::ReadFirstLine(path);
}

// Os campos 14 e 15 de /proc/<pid>/stat (utime, stime) vao em ticks de 1/HZ segundos; o
// ProcessItem pede unidades de 100 ns, como no Windows. HZ mede-se, nao se assume.
uint64_t CpuTime100ns(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    std::string raw;
    if (!lnx::ReadWholeFile(path, raw, 8192)) return 0;
    const size_t close = raw.rfind(')');
    if (close == std::string::npos || close + 2 >= raw.size()) return 0;
    std::vector<std::string> fields;
    std::istringstream rest(raw.substr(close + 2));
    std::string tok;
    while (rest >> tok) fields.push_back(tok);
    if (fields.size() < 13) return 0;
    const unsigned long long hz = static_cast<unsigned long long>(::sysconf(_SC_CLK_TCK));
    if (hz == 0) return 0;
    const unsigned long long utime = std::strtoull(fields[11].c_str(), nullptr, 10);
    const unsigned long long stime = std::strtoull(fields[12].c_str(), nullptr, 10);
    return (utime + stime) * (10000000ULL / hz);
}

// RSS em bytes: campo 2 de /proc/<pid>/statm sao paginas residentes. O campo 1 (vm_size) nao
// serve: conta memoria reservada e nunca tocada, que nao esta a ocupar RAM nenhuma.
uint64_t ResidentBytes(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/statm", pid);
    const std::string line = lnx::ReadFirstLine(path);
    if (line.empty()) return 0;
    std::istringstream ss(line);
    std::string sizeTok, residentTok;
    ss >> sizeTok >> residentTok;
    if (residentTok.empty()) return 0;
    const unsigned long long pages = std::strtoull(residentTok.c_str(), nullptr, 10);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0;
    return pages * static_cast<unsigned long long>(pageSize);
}

bool IsKernelThread(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/cmdline", pid);
    std::string raw;
    if (!lnx::ReadWholeFile(path, raw, 4096)) return true;
    for (char c : raw) {
        if (c != '\0') return false;
    }
    return true;
}

// Dono do processo: em Linux a pergunta "consigo escrever aqui?" e esta, e nao ha handle de
// proteccao (PPL) para consultar. UID desconhecido devolve -1, para nao ser trocado pelo root.
uid_t OwnerUid(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/status", pid);
    std::string raw;
    if (!lnx::ReadWholeFile(path, raw, 8192)) return static_cast<uid_t>(-1);
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.compare(0, 5, "Uid:") != 0) continue;
        std::istringstream fields(line.substr(4));
        long real = -1;
        if (fields >> real && real >= 0) return static_cast<uid_t>(real);
        break;
    }
    return static_cast<uid_t>(-1);
}

bool PriorityIsRealtime(uint32_t pid) {
    const int policy = ::sched_getscheduler(static_cast<pid_t>(pid));
    if (policy < 0) return false;
    const int base = policy & ~SCHED_RESET_ON_FORK;
    return base == SCHED_FIFO || base == SCHED_RR;
}

uint64_t AffinityOf(uint32_t pid) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(static_cast<pid_t>(pid), sizeof(set), &set) != 0) return 0;
    uint64_t mask = 0;
    const int limit = static_cast<int>(std::min<size_t>(64, CPU_SETSIZE));
    for (int cpu = 0; cpu < limit; ++cpu) {
        if (CPU_ISSET(cpu, &set)) mask |= (1ull << cpu);
    }
    return mask;
}

// ---------------------------------------------------------------------------
// X11: o estado de janela, sem depender de cabecalhos do X no build (mesmo padrao do
// AppProfileManager e do DWM/NVAPI no Windows). Sem X (Wayland puro, consola, VM de teste)
// o mapa sai vazio e quem decide trata "nao sei" como "nao mexo".
// ---------------------------------------------------------------------------
typedef unsigned long XWindowId;
typedef unsigned long XAtomId;

struct X11Functions {
    void* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(void*) = nullptr;
    XAtomId (*InternAtom)(void*, const char*, int) = nullptr;
    int (*GetWindowProperty)(void*, XWindowId, XAtomId, long, long, int, XAtomId, XAtomId*, int*,
                             unsigned long*, unsigned long*, unsigned char**) = nullptr;
    int (*Free)(void*) = nullptr;
    void* (*DefaultScreenOfDisplay)(void*) = nullptr;
    XWindowId (*RootWindowOfScreen)(void*) = nullptr;
    bool loaded = false;
};

const X11Functions& X11() {
    static X11Functions f;
    static bool attempted = false;
    if (attempted) return f;
    attempted = true;
    void* h = ::dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = ::dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return f;
    f.OpenDisplay = reinterpret_cast<decltype(f.OpenDisplay)>(::dlsym(h, "XOpenDisplay"));
    f.CloseDisplay = reinterpret_cast<decltype(f.CloseDisplay)>(::dlsym(h, "XCloseDisplay"));
    f.InternAtom = reinterpret_cast<decltype(f.InternAtom)>(::dlsym(h, "XInternAtom"));
    f.GetWindowProperty =
        reinterpret_cast<decltype(f.GetWindowProperty)>(::dlsym(h, "XGetWindowProperty"));
    f.Free = reinterpret_cast<decltype(f.Free)>(::dlsym(h, "XFree"));
    f.DefaultScreenOfDisplay =
        reinterpret_cast<decltype(f.DefaultScreenOfDisplay)>(::dlsym(h, "XDefaultScreenOfDisplay"));
    f.RootWindowOfScreen =
        reinterpret_cast<decltype(f.RootWindowOfScreen)>(::dlsym(h, "XRootWindowOfScreen"));
    f.loaded = f.OpenDisplay && f.CloseDisplay && f.InternAtom && f.GetWindowProperty && f.Free &&
               f.DefaultScreenOfDisplay && f.RootWindowOfScreen;
    return f;
}

// Le os itens de formato 32 de um property. AnyPropertyType (0) porque queremos o tipo real;
// property ausente ou de formato diferente devolve vazio, sem inventar nada.
std::vector<unsigned long> ReadXItems(void* dpy, XWindowId win, XAtomId prop,
                                      XAtomId* actualTypeOut = nullptr) {
    std::vector<unsigned long> items;
    const X11Functions& x = X11();
    XAtomId actualType = 0;
    int format = 0;
    unsigned long nitems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;
    const int rc = x.GetWindowProperty(dpy, win, prop, 0L, 1024L, 0, 0L, &actualType, &format,
                                       &nitems, &bytesAfter, &data);
    if (rc == 0 && data != nullptr) {
        if (format == 32 && actualType != 0) {
            // A Xlib entrega os itens de 32 bits como `long` (8 bytes em x86-64, 4 em x86-32).
            for (unsigned long i = 0; i < nitems; ++i) {
                items.push_back(static_cast<unsigned long>(reinterpret_cast<long*>(data)[i]));
            }
        }
        x.Free(data);
    }
    if (actualTypeOut) *actualTypeOut = actualType;
    return items;
}

void WarnOnceNoX11(const std::string& why) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    OPIT_LOG_WARN("ProcessManager (Linux): estado de janelas indisponivel: " + why +
                  ". O auto de processos fica sem os niveis por janela e nao rebaixa nada que "
                  "nao consiga classificar.");
}

#ifndef IOPRIO_WHO_PROCESS
#define IOPRIO_WHO_PROCESS 1
#endif
#ifndef IOPRIO_CLASS_SHIFT
#define IOPRIO_CLASS_SHIFT 13
#endif
#ifndef IOPRIO_CLASS_NONE
#define IOPRIO_CLASS_NONE 0
#define IOPRIO_CLASS_RT 1
#define IOPRIO_CLASS_BE 2
#define IOPRIO_CLASS_IDLE 3
#endif

} // namespace

// Os nomes criticos de Linux: o proprio init e os daemons do sistema, o compositor e o servidor
// de audio. Ao contrario da lista do Windows, esta nao precisa de ser exaustiva para as linhas
// de kernel (threads sem cmdline): essas sao tratadas pelo facto de nao terem binario.
static const std::unordered_set<std::string> kCriticalProcesses = {
    "systemd", "init", "kthreadd", "udevadm", "systemd-udevd", "systemd-logind",
    "systemd-journald", "systemd-userns-rese", "systemd-resolved", "systemd-networkd",
    "dbus-daemon", "dbus-broker", "polkitd", "sshd", "NetworkManager", "wpa_supplicant",
    "modemmanager", "accounts-daemon", "packagekitd", "fwupd", "gdm-session-worker",
    "gdm-x-session", "xorg", "xwayland", "wayland", "gnome-shell", "mutter-x11-frames",
    "kwin_x11", "kwin_wayland", "plasmashell", "pipewire", "pipewire-pulse", "wireplumber",
    "pulseaudio", "kerneloops", "cron", "crond", "rsyslogd", "journalctl", "opit-core"
};

bool ProcessManager::IsCriticalProcess(const std::string& processName) {
    const std::string lower = StringUtils::ToLower(StringUtils::Trim(processName));
    return kCriticalProcesses.find(lower) != kCriticalProcesses.end();
}

std::string ProcessManager::PriorityClassToString(uint32_t priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return "Ociosa (Baixa)";
        case BELOW_NORMAL_PRIORITY_CLASS: return "Abaixo do Normal";
        case NORMAL_PRIORITY_CLASS: return "Normal";
        case ABOVE_NORMAL_PRIORITY_CLASS: return "Acima do Normal";
        case HIGH_PRIORITY_CLASS: return "Alta";
        case REALTIME_PRIORITY_CLASS: return "Tempo Real";
        default: return "Normal";
    }
}

uint32_t ProcessManager::StringToPriorityClass(const std::string& name) {
    if (name == "Ociosa (Baixa)" || name == "Idle") return IDLE_PRIORITY_CLASS;
    if (name == "Abaixo do Normal" || name == "BelowNormal") return BELOW_NORMAL_PRIORITY_CLASS;
    if (name == "Acima do Normal" || name == "AboveNormal") return ABOVE_NORMAL_PRIORITY_CLASS;
    if (name == "Alta" || name == "High") return HIGH_PRIORITY_CLASS;
    return NORMAL_PRIORITY_CLASS;
}

int ProcessManager::PriorityRank(uint32_t priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return 0;
        case BELOW_NORMAL_PRIORITY_CLASS: return 1;
        case NORMAL_PRIORITY_CLASS: return 2;
        case ABOVE_NORMAL_PRIORITY_CLASS: return 3;
        case HIGH_PRIORITY_CLASS: return 4;
        case REALTIME_PRIORITY_CLASS: return 5;
        default: return 2;
    }
}

uint32_t ProcessManager::PriorityClassFromRank(int rank) {
    if (rank <= 0) return IDLE_PRIORITY_CLASS;
    if (rank == 1) return BELOW_NORMAL_PRIORITY_CLASS;
    if (rank == 2) return NORMAL_PRIORITY_CLASS;
    if (rank == 3) return ABOVE_NORMAL_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS; // Tempo Real recusado em SetPriority, como no Windows.
}

std::vector<ProcessItem> ProcessManager::EnumerateProcesses() {
    std::vector<ProcessItem> result;
    DIR* d = ::opendir("/proc");
    if (!d) return result;

    static const uid_t selfUid = ::geteuid();
    while (const dirent* e = ::readdir(d)) {
        char* end = nullptr;
        const long pidRaw = std::strtol(e->d_name, &end, 10);
        if (end == e->d_name || *end != '\0' || pidRaw <= 0) continue;
        const uint32_t pid = static_cast<uint32_t>(pidRaw);

        ProcessItem item;
        item.pid = pid;
        const std::string exe = ExePath(pid);
        if (!exe.empty()) {
            item.name = BaseName(exe);
        } else {
            item.name = CommName(pid);
        }
        if (item.name.empty()) continue; // ja terminou entre o readdir e a leitura
        item.imagePath = exe;
        item.workingSetBytes = ResidentBytes(pid);
        item.cpuTime100ns = CpuTime100ns(pid);
        item.affinityMask = AffinityOf(pid);

        const bool kernelThread = IsKernelThread(pid);
        item.isSystemCritical = kernelThread || IsCriticalProcess(item.name);

        if (PriorityIsRealtime(pid)) {
            item.priorityClass = REALTIME_PRIORITY_CLASS;
        } else {
            errno = 0;
            const int niceRaw = ::getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
            if (niceRaw == -1 && errno != 0) {
                // Sem leitura (processo de outra pessoa com hardened /proc, ou ja terminado):
                // registamos Normal mas marcamos como nao tocavel, para ninguem escrever em cima.
                item.priorityClass = NORMAL_PRIORITY_CLASS;
                item.priorityName = "Protegido/Sistema";
                item.protectedByWindows = true;
                result.push_back(item);
                continue;
            }
            item.priorityClass = PriorityClassFromNice(niceRaw);
        }
        item.priorityName = PriorityClassToString(item.priorityClass);

        const uid_t owner = OwnerUid(pid);
        item.protectedByWindows = kernelThread || (owner != static_cast<uid_t>(-1) && owner != selfUid);
        result.push_back(item);
    }
    ::closedir(d);
    return result;
}

bool ProcessManager::SetPriority(uint32_t pid, uint32_t priorityClass, uint32_t* lastErrorOut) {
    if (lastErrorOut) *lastErrorOut = 0;
    auto deny = [&](uint32_t err) {
        if (lastErrorOut) *lastErrorOut = err;
        return false;
    };
    // 0, 1 e 2 (init e as linhas de kernel) nunca se tocam; no Windows o corte era nos pid <= 4.
    if (pid <= 2) return deny(EINVAL);
    if (priorityClass == REALTIME_PRIORITY_CLASS) {
        return deny(EPERM); // recusado por estabilidade do sistema, tal como no Windows
    }

    const std::string exe = ExePath(pid);
    if (!exe.empty() && IsCriticalProcess(BaseName(exe))) return deny(EACCES);
    if (exe.empty() && IsKernelThread(pid)) return deny(EACCES);

    const int target = NiceFromPriorityClass(priorityClass);
    errno = 0;
    if (::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), target) != 0) {
        return deny(errno ? static_cast<uint32_t>(errno) : static_cast<uint32_t>(EINVAL));
    }
    // Confirmar por leitura, porque descer o nice de outra pessoa pode ser aceite e ignorado.
    errno = 0;
    const int readBack = ::getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if (readBack == -1 && errno != 0) return deny(static_cast<uint32_t>(errno));
    if (readBack != target) return deny(ERANGE);
    return true;
}

bool ProcessManager::SetAffinity(uint32_t pid, uint64_t affinityMask) {
    if (affinityMask == 0 || pid <= 2) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu = 0; cpu < 64 && cpu < CPU_SETSIZE; ++cpu) {
        if (affinityMask & (1ull << cpu)) CPU_SET(cpu, &set);
    }
    return ::sched_setaffinity(static_cast<pid_t>(pid), sizeof(set), &set) == 0;
}

bool ProcessManager::SetEcoQoS(uint32_t pid, bool enable) {
    if (pid <= 2) return false;
#ifdef __NR_ioprio_set
    // Em Linux nao ha classe de "execucao ecologica" por processo: o que existe e a prioridade
    // de E/S (IOPRIO_CLASS_IDLE), que e a parte que faz esperar um background que escreve em
    // disco. A parte de CPU ja e o SetPriority. Sem este syscall (kernel sem ioprio) devolvemos
    // false em vez de dizer que pusemos eco.
    const int value = (enable ? IOPRIO_CLASS_IDLE : IOPRIO_CLASS_NONE) << IOPRIO_CLASS_SHIFT;
    if (::syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, static_cast<int>(pid), value) != 0) {
        return false;
    }
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool ProcessManager::Terminate(uint32_t pid) {
    if (pid <= 2) return false;
    const std::string exe = ExePath(pid);
    if (!exe.empty() && IsCriticalProcess(BaseName(exe))) return false;
    if (exe.empty() && IsKernelThread(pid)) return false;
    // SIGTERM, nao SIGKILL: em Linux o processo limpa-se a si proprio quando pode, e um
    // TerminalProcess immediato à Windows deixaria ficheiros temporarios e pipes pelo meio.
    return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool ProcessManager::OptimizeMemory(uint32_t pid) {
    static bool warned = false;
    if (!warned) {
        warned = true;
        OPIT_LOG_WARN("ProcessManager (Linux): nao ha equivalente ao EmptyWorkingSet do Windows. "
                      "O kernel nao tem chamada para devolver paginas de um processo de fora; "
                      "a alternativa (/proc/sys/vm/drop_caches) e global e penaliza toda a maquina. "
                      "A coluna fica em 'indisponivel' em vez de contar um sucesso falso.");
    }
    (void)pid;
    return false;
}

bool ProcessManager::IsWindowsSystemBinary(const std::string& imagePath) {
    // Nome partilhado com o Windows; em Linux a pergunta e "e peca da distribuicao?". Caminho
    // vazio trata-se como "do sistema": sem caminho nao ha como provar que e de terceiro, e o
    // efeito de errar para este lado e nao rebaixar, que e o erro seguro.
    if (imagePath.empty()) return true;
    static const char* const kDistroPrefixes[] = {
        "/usr/bin/", "/usr/sbin/", "/usr/lib/", "/usr/libexec/", "/usr/share/", "/lib/",
        "/sbin/", "/bin/", "/usr/lib64/"
    };
    for (const char* p : kDistroPrefixes) {
        if (imagePath.compare(0, std::strlen(p), p) == 0) return true;
    }
    return false; // /home, /opt, /snap, /app, AppImage, /usr/local: tudo de terceiro
}

std::unordered_map<uint32_t, ProcessWindowState> ProcessManager::BuildWindowStateMap() {
    std::unordered_map<uint32_t, ProcessWindowState> result;
    const X11Functions& x = X11();
    if (!x.loaded) {
        WarnOnceNoX11("libX11.so.6 nao esta no sistema");
        return result;
    }
    void* dpy = x.OpenDisplay(nullptr);
    if (!dpy) {
        WarnOnceNoX11("sem DISPLAY aberto (sessao Wayland pura ou consola)");
        return result;
    }

    void* screen = x.DefaultScreenOfDisplay ? x.DefaultScreenOfDisplay(dpy) : nullptr;
    const XWindowId root = screen ? x.RootWindowOfScreen(screen) : 0;
    if (root == 0) {
        x.CloseDisplay(dpy);
        WarnOnceNoX11("sem ecran por omissão");
        return result;
    }

    const XAtomId atomClientList = x.InternAtom(dpy, "_NET_CLIENT_LIST", 0);
    const XAtomId atomActiveWindow = x.InternAtom(dpy, "_NET_ACTIVE_WINDOW", 0);
    const XAtomId atomWmPid = x.InternAtom(dpy, "_NET_WM_PID", 0);
    const XAtomId atomWmState = x.InternAtom(dpy, "WM_STATE", 0);
    const XAtomId atomNetWmState = x.InternAtom(dpy, "_NET_WM_STATE", 0);
    const XAtomId atomMaxV = x.InternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
    const XAtomId atomMaxH = x.InternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
    const XAtomId atomHidden = x.InternAtom(dpy, "_NET_WM_STATE_HIDDEN", 0);

    const std::vector<unsigned long> clients = ReadXItems(dpy, root, atomClientList);
    const std::vector<unsigned long> activeList = ReadXItems(dpy, root, atomActiveWindow);
    const XWindowId active = activeList.empty() ? 0 : activeList[0];
    if (clients.empty()) {
        // Sem janelas na lista do gestor: pode ser gestor sem EWMH (fenstermaker) ou sessao sem
        // ambiente grafico. Devolvemos o mapa vazio, que vale "nao sei", nao "nao tem janelas".
        x.CloseDisplay(dpy);
        return result;
    }

    // Precedencia identica ao Windows: primeiro plano > visivel em segundo plano > minimizada >
    // bandeja. A geometria fora do ecra (o caso "empurrada para a bandeja" do Windows) nao e
    // medida aqui porque precisaria de XGetWindowAttributes; em X11 o sinal correspondente e o
    // estado HIDDEN do _NET_WM_STATE, e e esse que usamos.
    struct Acc {
        bool foreground = false;
        bool zoomed = false;
        bool iconic = false;
        bool hidden = false;
        bool onScreen = false;
    };
    std::unordered_map<uint32_t, Acc> byPid;

    for (XWindowId win : clients) {
        const std::vector<unsigned long> pidItems = ReadXItems(dpy, win, atomWmPid);
        if (pidItems.empty() || pidItems[0] == 0) continue;
        const uint32_t pid = static_cast<uint32_t>(pidItems[0]);

        Acc& st = byPid[pid];
        const std::vector<unsigned long> wmState = ReadXItems(dpy, win, atomWmState);
        // WM_STATE: 0 = Withdrawn, 1 = Normal, 3 = Iconic (codigos do ICCCM).
        const bool iconic = !wmState.empty() && wmState[0] == 3;
        const bool withdrawn = !wmState.empty() && wmState[0] == 0;
        const std::vector<unsigned long> states = ReadXItems(dpy, win, atomNetWmState);
        bool maxV = false, maxH = false, hidden = false;
        for (unsigned long a : states) {
            if (a == atomMaxV) maxV = true;
            else if (a == atomMaxH) maxH = true;
            else if (a == atomHidden) hidden = true;
        }
        const bool foreground = (win == active);

        if (iconic) st.iconic = true;
        if (hidden) st.hidden = true;
        if (foreground && !iconic) {
            st.foreground = true;
            st.zoomed = st.zoomed || (maxV && maxH);
            st.onScreen = true;
        } else if (!withdrawn && !iconic) {
            st.onScreen = true;
        }
    }
    x.CloseDisplay(dpy);

    for (const auto& kv : byPid) {
        const Acc& st = kv.second;
        ProcessWindowState state;
        if (st.foreground) {
            state = st.zoomed ? ProcessWindowState::Maximized : ProcessWindowState::Normal;
        } else if (st.onScreen) {
            state = ProcessWindowState::Background;
        } else if (st.iconic) {
            state = ProcessWindowState::Minimized;
        } else if (st.hidden) {
            state = ProcessWindowState::SystemTray;
        } else {
            state = ProcessWindowState::NoVisibleWindow;
        }
        result[kv.first] = state;
    }
    return result;
}

ProcessWindowState ProcessManager::DetectWindowState(uint32_t pid) {
    auto map = BuildWindowStateMap();
    const auto it = map.find(pid);
    return (it != map.end()) ? it->second : ProcessWindowState::NoVisibleWindow;
}

namespace {

// Os PIDs com som audivel agora. No Windows isto sai do WASAPI; em Linux nao ha uma API unica:
// 1) /proc/asound/*/pcm*/status tem "Owner PID" (funciona para quem usa ALSA directamente);
// 2) com PipeWire/Pulse, o dueno visivel e o proprio servidor de audio e o pw-dump, se existir,
//    publica o pid de cada cliente. Sem nenhum dos dois, conjunto vazio: tal como no Windows,
//    falhou a leitura, nao se bloqueia a otimizacao por suspeita.
std::unordered_set<uint32_t> CollectAudioPids() {
    std::unordered_set<uint32_t> pids;
    DIR* cards = ::opendir("/proc/asound");
    if (cards) {
        std::vector<std::string> dirs;
        while (const dirent* e = ::readdir(cards)) {
            const std::string n = e->d_name;
            if (n.compare(0, 4, "card") == 0) dirs.push_back(n);
        }
        ::closedir(cards);
        for (const std::string& card : dirs) {
            char path[256];
            for (int pcm = 0; pcm < 8; ++pcm) {
                for (int sub = 0; sub < 8; ++sub) {
                    for (const char* kind : {"playback", "capture"}) {
                        std::snprintf(path, sizeof(path), "/proc/asound/%s/pcm%i%s/sub%i/status",
                                      card.c_str(), pcm, kind, sub);
                        std::ifstream f(path);
                        std::string l;
                        while (std::getline(f, l)) {
                            if (l.rfind("Owner PID", 0) != 0) continue;
                            const size_t digits = l.find_first_of("0123456789");
                            if (digits == std::string::npos) continue;
                            const long v = std::atol(l.c_str() + digits);
                            if (v > 2) pids.insert(static_cast<uint32_t>(v));
                            break;
                        }
                    }
                }
            }
        }
    }

    if (pids.empty()) {
        std::string dump;
        int rc = -1;
        if (lnx::SpawnCapture({"pw-dump"}, dump, rc) && rc == 0) {
            size_t pos = 0;
            while ((pos = dump.find("\"client.pid\"", pos)) != std::string::npos) {
                const size_t colon = dump.find(':', pos + 12);
                if (colon == std::string::npos) break;
                const size_t digits = dump.find_first_of("0123456789", colon);
                if (digits == std::string::npos) break;
                const long v = std::atol(dump.c_str() + digits);
                if (v > 2) pids.insert(static_cast<uint32_t>(v));
                pos = digits;
            }
        }
    }
    return pids;
}

std::unordered_set<uint32_t> g_audioPids;
uint64_t g_audioPidsAgeMs = 0;
bool g_audioPidsValid = false;
std::mutex g_audioMutex;

uint64_t NowSteadyMsLocal() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

std::unordered_set<uint32_t> ProcessManager::ProcessPidsWithActiveAudio() {
    const uint64_t now = NowSteadyMsLocal();
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (!g_audioPidsValid || (now - g_audioPidsAgeMs) > 3000ULL) {
        g_audioPids = CollectAudioPids();
        g_audioPidsAgeMs = now;
        g_audioPidsValid = true;
    }
    return g_audioPids;
}

bool ProcessManager::HasActiveAudioOrMedia(uint32_t pid, const std::string& procName) {
    const std::string lower = StringUtils::ToLower(procName);
    // Equivalentes Linux dos leitores/streams que no Windows estao na lista: continuam na lista
    // porque podem estar a capturar sem fluxo audivel (OBS) e ai a leitura de audio nao diz nada.
    static const std::unordered_set<std::string> kMediaApps = {
        "spotify", "spotify.jar", "discord", "obs", "obs64", "obs-steam", "vlc", "mpv",
        "audacious", "rhythmbox", "celluloid", "firefox", "chromium",
        "chromium-browser", "google-chrome", "thunderbird", "telegram-desktop", "slack",
        "element", "msteams", "zoom", "pavucontrol", "pipewire", "wireplumber"
    };
    if (kMediaApps.find(lower) != kMediaApps.end()) return true;

    const auto audio = ProcessPidsWithActiveAudio();
    return audio.find(pid) != audio.end();
}

} // namespace opitcore
