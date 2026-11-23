// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
// Pecas partilhadas pelos gemelos Linux de platform/linux (ProcessManager, ServiceManager).
// Ficam num cabecalho inline de proposito: nao acrescentam aresta de enlace a nenhuma
// biblioteca e nao ha alvo novo para declarar.
#ifdef __linux__
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace opitcore {
namespace lnx {

// Executa um binario com argv literal, sem passar por shell: nada do que vem do sistema
// (nome de unit, caminho, filtro da UI) pode virar comando. Devolve false se o fork/exec
// falhou; com true, `out` e o stdout e `rc` e o codigo de saida do processo.
inline bool SpawnCapture(const std::vector<std::string>& argv, std::string& out, int& rcOut) {
    out.clear();
    rcOut = -1;
    if (argv.empty()) return false;
    int pipeFd[2] = {-1, -1};
    if (::pipe2(pipeFd, O_CLOEXEC) != 0) return false;

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipeFd[0]);
        ::close(pipeFd[1]);
        return false;
    }
    if (child == 0) {
        ::dup2(pipeFd[1], STDOUT_FILENO);
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDERR_FILENO);
            if (devNull > 2) ::close(devNull);
        }
        ::close(pipeFd[0]);
        ::close(pipeFd[1]);
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        ::execvp(args[0], args.data());
        ::_exit(127);
    }
    ::close(pipeFd[1]);

    // poll em vez de read bloqueado: um binario que pendure nao pode pendurar o ciclo.
    bool timedOut = false;
    char buf[4096];
    while (true) {
        pollfd pfd{pipeFd[0], POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 5000);
        if (pr == 0) { timedOut = true; break; }
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        const ssize_t got = ::read(pipeFd[0], buf, sizeof(buf));
        if (got <= 0) break;
        out.append(buf, static_cast<size_t>(got));
        if (out.size() > (1u << 20)) break; // trava de 1 MB: o systemctl --all pode crescer
    }
    if (timedOut) ::kill(child, SIGKILL);
    int status = 0;
    const pid_t waited = ::waitpid(child, &status, 0);
    ::close(pipeFd[0]);
    if (waited < 0) return false;
    if (timedOut || !WIFEXITED(status)) {
        rcOut = -1; // cortado por tempo ou morto por sinal: nao ha codigo de saida confiavel
        return false;
    }
    rcOut = WEXITSTATUS(status);
    return true;
}

// Primeira linha de um ficheiro de /proc ou /sys. Estes ficheiros nao se podem abrir com
// ifstream quando o processo ainda esta a nascer (devem vazio), por isso a leitura e uma
// abre-le-fecha simples e o resultado vazio significa "nao lemos nada", nao "esta vazio".
inline std::string ReadFirstLine(const std::string& path) {
    std::string s;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return s;
    char buf[1024];
    const ssize_t got = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (got <= 0) return s;
    buf[got] = '\0';
    s.assign(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Lê um ficheiro inteiro pequeno (/sys/class/..., /proc/<pid>/cmdline). false = nao abriu.
inline bool ReadWholeFile(const std::string& path, std::string& out, size_t maxBytes = 65536) {
    out.clear();
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[4096];
    while (out.size() < maxBytes) {
        const ssize_t got = ::read(fd, buf, sizeof(buf));
        if (got < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        if (got == 0) break;
        out.append(buf, static_cast<size_t>(got));
    }
    ::close(fd);
    return true;
}

// Escreve um numero num atributo de /sys ou /proc, exactamente uma vez, e devolve o errno.
// Sem O_TRUNC: os atributos de sysfs ignoram as flags mas recusam combinacoes ReadOnly/WriteOnly.
inline bool WriteSysfsValue(const std::string& path, const std::string& value, int& errnoOut) {
    errnoOut = 0;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        errnoOut = errno;
        return false;
    }
    size_t off = 0;
    bool ok = true;
    while (off < value.size()) {
        const ssize_t w = ::write(fd, value.data() + off, value.size() - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            ok = false;
            errnoOut = errno;
            break;
        }
        off += static_cast<size_t>(w);
    }
    ::close(fd);
    return ok;
}

} // namespace lnx
} // namespace opitcore
#endif // __linux__
