#include "ModuleVerifier.h"
#include "IntegrityVerifier.h"
#include "utils/StringUtils.h"
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace opitcore {
namespace security {

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

} // namespace security
} // namespace opitcore
