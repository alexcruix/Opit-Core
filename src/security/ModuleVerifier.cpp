// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "ModuleVerifier.h"
#include "IntegrityVerifier.h"
#include "utils/StringUtils.h"

#ifdef _WIN32

#include <psapi.h>

#pragma comment(lib, "psapi.lib")

#else

#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

#endif

namespace opitcore {
namespace security {

#ifdef _WIN32

bool ModuleVerifier::EnforceSecureDllSearchOrder() {
    // Desativa caixas de diálogo modais do sistema em caso de erro de carregamento
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // Restringe busca de DLLs apenas à pasta do executável e à System32
    BOOL ok = SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    // Remove o Current Working Directory da rota de busca legada
    SetDllDirectoryW(L"");
    return ok == TRUE;
}

HMODULE ModuleVerifier::VerifyAndLoad(const std::wstring& moduleFullPath,
                                     const std::string& expectedSha256,
                                     IntegrityStatus& outStatus) {
    outStatus = IntegrityStatus::Valid;

    // Garante que o Windows nao abra modal de dialogo em caso de erro de PE
    UINT oldErrMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    if (moduleFullPath.empty()) {
        outStatus = IntegrityStatus::ReadError;
        SetErrorMode(oldErrMode);
        return nullptr;
    }

    // 1. Abre o arquivo com FILE_SHARE_READ exclusivo contra escrita (impede que outro processo
    //    escreva, renomeie ou delete o arquivo enquanto validamos o hash - mitigacao TOCTOU)
    HANDLE hFile = CreateFileW(moduleFullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        outStatus = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) ?
                    IntegrityStatus::Missing : IntegrityStatus::ReadError;
        SetErrorMode(oldErrMode);
        return nullptr;
    }

    // 2. Coleta identificacao fisica unica do arquivo (VolumeSerial + FileIndex)
    BY_HANDLE_FILE_INFORMATION preLoadInfo{};
    bool hasPreLoadInfo = (GetFileInformationByHandle(hFile, &preLoadInfo) != 0);

    // 3. Calcula SHA-256 no handle com lock de leitura compartilhado
    std::string calcSha256;
    if (!IntegrityVerifier::ComputeHandleSha256(hFile, calcSha256)) {
        CloseHandle(hFile);
        outStatus = IntegrityStatus::ReadError;
        SetErrorMode(oldErrMode);
        return nullptr;
    }

    if (!expectedSha256.empty()) {
        std::string expNorm = expectedSha256;
        for (auto& c : expNorm) c = static_cast<char>(::tolower(c));
        for (auto& c : calcSha256) c = static_cast<char>(::tolower(c));

        if (expNorm != calcSha256) {
            CloseHandle(hFile);
            outStatus = IntegrityStatus::Tampered;
            SetErrorMode(oldErrMode);
            return nullptr;
        }
    }

    // 4. Carrega a DLL enquanto o handle hFile continua aberto impedindo escritas
    HMODULE hMod = LoadLibraryExW(moduleFullPath.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hMod) {
        // Fallback seguro usando caminho absoluto confiavel
        hMod = LoadLibraryExW(moduleFullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    // 5. Verificacao pos-carregamento: garante que o modulo mapeado em memoria
    //    corresponde exatamente ao inode/file-index verificado no passo 2
    if (hMod && hasPreLoadInfo) {
        wchar_t loadedModPath[MAX_PATH] = {};
        if (GetModuleFileNameW(hMod, loadedModPath, MAX_PATH) > 0) {
            HANDLE hLoadedFile = CreateFileW(loadedModPath, GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             nullptr, OPEN_EXISTING, 0, nullptr);
            if (hLoadedFile != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION postLoadInfo{};
                if (GetFileInformationByHandle(hLoadedFile, &postLoadInfo)) {
                    if (preLoadInfo.dwVolumeSerialNumber != postLoadInfo.dwVolumeSerialNumber ||
                        preLoadInfo.nFileIndexHigh != postLoadInfo.nFileIndexHigh ||
                        preLoadInfo.nFileIndexLow != postLoadInfo.nFileIndexLow) {
                        // Inconsistencia detectada: arquivo mapeado difere do verificado
                        FreeLibrary(hMod);
                        hMod = nullptr;
                        outStatus = IntegrityStatus::Tampered;
                    }
                }
                CloseHandle(hLoadedFile);
            }
        }
    }

    // Libera o lock de leitura do arquivo apos a conclusao do carregamento seguro
    CloseHandle(hFile);

    if (!hMod && outStatus == IntegrityStatus::Valid) {
        outStatus = IntegrityStatus::ReadError;
    }

    SetErrorMode(oldErrMode);
    return hMod;
}

std::vector<std::string> ModuleVerifier::ListLoadedModuleNames() {
    std::vector<std::string> names;
    HANDLE hProcess = GetCurrentProcess();
    HMODULE hMods[1024];
    DWORD cbNeeded = 0;

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        for (size_t i = 0; i < count; ++i) {
            wchar_t szModName[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t))) {
                names.push_back(StringUtils::ToUtf8(szModName));
            }
        }
    }
    return names;
}

#else

bool ModuleVerifier::EnforceSecureDllSearchOrder() {
    // O mecanismo do Windows retira o diretorio de trabalho e o PATH da busca de DLLs. A glibc nao
    // pesquisa no diretorio de trabalho: as dependencias proprias resolvem por RPATH/RUNPATH e so
    // depois por LD_LIBRARY_PATH. Nao ha, por isso, o que desativar, e o risco equivalente fica
    // coberto pelo VerifyAndLoad abaixo, que recusa qualquer caminho que nao seja absoluto.
    return true;
}

void* ModuleVerifier::VerifyAndLoad(const std::wstring& moduleFullPath,
                                   const std::string& expectedSha256,
                                   IntegrityStatus& outStatus) {
    outStatus = IntegrityStatus::Valid;

    std::string path = StringUtils::ToUtf8(moduleFullPath);
    if (path.empty()) {
        outStatus = IntegrityStatus::ReadError;
        return nullptr;
    }
    if (path.front() != '/') {
        // So se carrega pelo caminho absoluto ja resolvido: um caminho relativo passaria pela
        // pesquisa de LD_LIBRARY_PATH, que e exatamente o que esta rota tem de impedir.
        outStatus = IntegrityStatus::ManifestInvalid;
        return nullptr;
    }

    // O fd e medido antes de qualquer dlopen: daqui ate ao carregamento o conteudo ja esta provado
    // e o par (st_dev, st_ino) fica registado para a comprobacao pos-carregamento.
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        outStatus = (errno == ENOENT || errno == ENOTDIR) ? IntegrityStatus::Missing
                                                         : IntegrityStatus::ReadError;
        return nullptr;
    }

    struct stat pre{};
    bool hasPre = (fstat(fd, &pre) == 0);

    std::string calcSha256;
    if (!IntegrityVerifier::ComputeHandleSha256(fd, calcSha256)) {
        close(fd);
        outStatus = IntegrityStatus::ReadError;
        return nullptr;
    }
    close(fd);

    if (!expectedSha256.empty()) {
        std::string expNorm = expectedSha256;
        for (auto& c : expNorm) c = static_cast<char>(::tolower(c));
        for (auto& c : calcSha256) c = static_cast<char>(::tolower(c));

        if (expNorm != calcSha256) {
            outStatus = IntegrityStatus::Tampered;
            return nullptr;
        }
    }

    void* hMod = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!hMod) {
        outStatus = IntegrityStatus::ReadError;
        return nullptr;
    }

    // Comprobacao pos-carregamento, o equivalente ao file-index reconsultado no Windows: se o
    // inode, o tamanho ou o mtime mudaram entre a medicao e o dlopen, o que foi mapeado na
    // memoria nao e o arquivo que foi verificado.
    if (hasPre) {
        struct stat post{};
        if (stat(path.c_str(), &post) == 0) {
            if (post.st_dev != pre.st_dev || post.st_ino != pre.st_ino ||
                post.st_size != pre.st_size || post.st_mtim.tv_sec != pre.st_mtim.tv_sec ||
                post.st_mtim.tv_nsec != pre.st_mtim.tv_nsec) {
                dlclose(hMod);
                outStatus = IntegrityStatus::Tampered;
                return nullptr;
            }
        }
    }

    return hMod;
}

std::vector<std::string> ModuleVerifier::ListLoadedModuleNames() {
    // O mapa de enderecos do proprio processo e o equivalente do EnumProcessModules: cada linha ja
    // carrega o caminho absoluto do arquivo mapeado. O mesmo arquivo aparece em varias linhas
    // (segmentos), por isso a lista devolve cada caminho uma vez, por ordem de aparecimento.
    std::vector<std::string> names;
    std::set<std::string> seen;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string path = line.substr(tab + 1);
        while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
        // So arquivos mapeados: as entradas anonimas do mapa ([heap], [vdso], memfd) nao comecam
        // por '/' e caem fora da lista, tal como as bibliotecas carregadas na memoria sem arquivo
        // caem fora do EnumProcessModules do Windows.
        if (path.empty() || path[0] != '/') continue;
        if (seen.insert(path).second) {
            names.push_back(path);
        }
    }
    return names;
}

#endif

} // namespace security
} // namespace opitcore
