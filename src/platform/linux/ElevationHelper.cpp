// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "ElevationHelper.h"
#include "core/Logger.h"
#include "utils/FileUtils.h"
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace opitcore {
namespace {

std::string ReadLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::string SelfExePath() {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    return std::string(buf);
}

// XDG autostart e o equivalente funcional da chave Run do registo Windows: o gestor de
// sessao lanca cada .desktop deste diretorio quando o utilizador entra. O ambito admin
// muda apenas o diretorio (por sistema), e por isso exige escrita privilegiada.
std::string AutostartFilePath(bool systemScope) {
    if (systemScope) return "/etc/xdg/autostart/OpitCore.desktop";
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    const std::string base = (cfg && *cfg) ? std::string(cfg)
                                          : (std::string(std::getenv("HOME") ? std::getenv("HOME") : "/root") + "/.config");
    return base + "/autostart/OpitCore.desktop";
}

std::string DesktopEntry(const std::string& exe) {
    std::ostringstream ss;
    ss << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Version=1.0\n"
       << "Name=OpitCore\n"
       << "Exec=" << exe << "\n"
       << "X-GNOME-Autostart-enabled=true\n"
       << "NoDisplay=true\n"
       << "RunNotIfAlreadyRunning=false\n";
    return ss.str();
}

bool WriteDesktopEntry(bool systemScope) {
    const std::string exe = SelfExePath();
    if (exe.empty()) {
        OPIT_LOG_WARN("ElevationHelper(Linux): /proc/self/exe ilegivel, entrada de autostart nao escrita.");
        return false;
    }
    const std::string path = AutostartFilePath(systemScope);
    if (!FileUtils::EnsureDirectory(path.substr(0, path.find_last_of('/')))) {
        OPIT_LOG_WARN("ElevationHelper(Linux): nao foi possivel criar o diretorio de autostart: " + path);
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        OPIT_LOG_WARN("ElevationHelper(Linux): escrita recusada em " + path +
                      (ElevationHelper::IsElevated() ? "" : " (ambito de utilizador, sem root)"));
        return false;
    }
    const std::string body = DesktopEntry(exe);
    f << body;
    f.close();
    // Verificacao por leitura: so se diz "ligado" quando o ficheiro esta la com o Exec certo.
    std::ifstream back(path);
    std::string line;
    const std::string want = "Exec=" + exe;
    while (std::getline(back, line)) {
        if (line == want) return true;
    }
    OPIT_LOG_WARN("ElevationHelper(Linux): autostart escrito mas nao se le de volta em " + path);
    return false;
}

bool RemoveDesktopEntry(bool systemScope) {
    const std::string path = AutostartFilePath(systemScope);
    if (!FileUtils::FileExists(path)) return true;
    if (::remove(path.c_str()) != 0) {
        OPIT_LOG_WARN("ElevationHelper(Linux): nao se conseguiu remover " + path + ".");
        return false;
    }
    return !FileUtils::FileExists(path);
}

std::string OsReleaseField(const std::string& key) {
    std::ifstream f("/etc/os-release");
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(f, line)) {
        if (line.rfind(prefix, 0) != 0) continue;
        std::string v = line.substr(prefix.size());
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
            v = v.substr(1, v.size() - 2);
        }
        return v;
    }
    return std::string();
}

std::string MachineName() {
    struct utsname u{};
    if (::uname(&u) != 0) return std::string();
    return std::string(u.machine);
}

} // namespace

bool ElevationHelper::IsElevated() {
    return ::geteuid() == 0;
}

std::string ElevationHelper::GetOsVersionString() {
    std::string pretty = OsReleaseField("PRETTY_NAME");
    if (pretty.empty()) {
        const std::string name = OsReleaseField("NAME");
        const std::string ver = OsReleaseField("VERSION_ID");
        pretty = name.empty() ? std::string("Linux") : (name + (ver.empty() ? "" : " " + ver));
    }
    struct utsname u{};
    if (::uname(&u) == 0) {
        pretty += std::string(" (") + u.sysname + " " + u.release + ")";
    }
    return pretty;
}

bool ElevationHelper::Is64BitOs() {
    const std::string m = MachineName();
    if (m.empty()) return false;
    return m == "x86_64" || m == "aarch64" || m == "ppc64le" || m == "ppc64" ||
           m == "riscv64" || m == "s390x" || m == "loongarch64" ||
           (m.size() > 2 && m.compare(m.size() - 2, 2, "64") == 0);
}

bool ElevationHelper::SetRunAtStartup(bool enable) {
    return enable ? WriteDesktopEntry(false) : RemoveDesktopEntry(false);
}

bool ElevationHelper::IsRunAtStartup() {
    return FileUtils::FileExists(AutostartFilePath(false));
}

bool ElevationHelper::SetRunAtStartupAdmin(bool enable) {
    if (!IsElevated()) {
        // Nada se escreve sem privilegio: o programa pergunta, nao finge que aplicou.
        OPIT_LOG_WARN("ElevationHelper(Linux): autostart de sistema em " + AutostartFilePath(true) +
                      " exige root (uid atual=" + std::to_string(::geteuid()) + ").");
        return false;
    }
    return enable ? WriteDesktopEntry(true) : RemoveDesktopEntry(true);
}

bool ElevationHelper::IsRunAtStartupAdmin() {
    return FileUtils::FileExists(AutostartFilePath(true));
}

bool ElevationHelper::ApplySvchostGrouping(int groupingMode) {
    // Conceito exclusivo do Windows (agrupamento de svc-host). Nao ha mecanismo gemelo
    // no Linux, por isso devolve sempre false: quem chama reporta "nao aplicavel".
    (void)groupingMode;
    return false;
}

bool ElevationHelper::UnlockCoreParkingInRegistry() {
    // Gemelo funcional: no Linux o nucleo "estacionado" esta offline em
    // /sys/devices/system/cpu/cpuN/online. Destacar = escrever 1 em cada nucleo que
    // estiver em 0 e confirmar a leitura. Maquinas virtuais sem o ficheiro devolvem
    // false porque nao ha estado para confirmar.
    DIR* dir = ::opendir("/sys/devices/system/cpu");
    if (!dir) {
        OPIT_LOG_WARN("ElevationHelper(Linux): /sys/devices/system/cpu indisponivel, nucleos nao verificaveis.");
        return false;
    }
    std::vector<std::string> offlineFiles;
    bool anyOnlineFile = false;
    int parkeds = 0;
    while (dirent* e = ::readdir(dir)) {
        if (std::strncmp(e->d_name, "cpu", 3) != 0) continue;
        const char* digits = e->d_name + 3;
        if (*digits < '0' || *digits > '9') continue;
        const std::string f = std::string("/sys/devices/system/cpu/") + e->d_name + "/online";
        std::ifstream probe(f);
        if (!probe) continue;
        anyOnlineFile = true;
        int value = 1;
        probe >> value;
        if (value == 0) {
            ++parkeds;
            offlineFiles.push_back(f);
        }
    }
    ::closedir(dir);

    if (!anyOnlineFile) {
        OPIT_LOG_WARN("ElevationHelper(Linux): sem cpuN/online (virtualizado?), estacionamento nao confirmavel.");
        return false;
    }
    if (offlineFiles.empty()) {
        return true;  // todos os nucleos online: o estado pedido ja e o estado real
    }
    if (!IsElevated()) {
        OPIT_LOG_WARN("ElevationHelper(Linux): " + std::to_string(parkeds) +
                      " nucleo(s) offline; reactivar exige root, nada foi escrito.");
        return false;
    }
    for (const std::string& path : offlineFiles) {
        std::ofstream w(path, std::ios::binary | std::ios::trunc);
        if (!w) {
            OPIT_LOG_WARN("ElevationHelper(Linux): escrita recusada em " + path + ".");
            return false;
        }
        w << "1";
        w.close();
        if (ReadLine(path) != "1") {
            OPIT_LOG_WARN("ElevationHelper(Linux): " + path + " nao voltou a ler 1 depois da escrita.");
            return false;
        }
    }
    return true;
}

} // namespace opitcore
