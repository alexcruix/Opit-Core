// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "IntegrityVerifier.h"
#include "utils/StringUtils.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

#else

#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

#endif

namespace opitcore {
namespace security {

IntegrityVerifier::IntegrityVerifier() = default;
IntegrityVerifier::~IntegrityVerifier() = default;

void IntegrityVerifier::InvalidateCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

#ifdef _WIN32

bool IntegrityVerifier::IsSafeRelativePath(const std::string& relPath,
                                          const std::wstring& baseDir,
                                          std::wstring& outCanonicalPath) {
    outCanonicalPath.clear();
    if (relPath.empty()) return false;

    // Rejeita caminhos com dois-pontos (letras de unidade C:, ADS streams de arquivo)
    if (relPath.find(':') != std::string::npos) return false;

    // Rejeita caminhos absolutos ou UNC (/ ou \)
    if (relPath.front() == '/' || relPath.front() == '\\') return false;

    // Rejeita caminhos contendo sequências de travessia '..'
    std::string normalized = relPath;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');

    // Analisa os segmentos individuais
    std::istringstream iss(normalized);
    std::string segment;
    while (std::getline(iss, segment, '\\')) {
        if (segment == ".." || segment == ".") {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::path basePath(baseDir);
    std::filesystem::path targetPath = basePath / std::filesystem::u8path(relPath);

    std::filesystem::path baseCanon = std::filesystem::weakly_canonical(basePath, ec);
    if (ec) return false;

    std::filesystem::path targetCanon = std::filesystem::weakly_canonical(targetPath, ec);
    if (ec) return false;

    // Verifica fronteira de confinamento (baseDir deve ser prefixo exato do caminho canônico)
    std::wstring baseStr = baseCanon.wstring();
    std::wstring targetStr = targetCanon.wstring();

    if (!baseStr.empty() && baseStr.back() != L'\\' && baseStr.back() != L'/') {
        baseStr += L'\\';
    }

    // Compara ignorando maiúsculas/minúsculas no Windows
    if (targetStr.size() < baseStr.size()) return false;
    for (size_t i = 0; i < baseStr.size(); ++i) {
        if (towlower(targetStr[i]) != towlower(baseStr[i])) {
            return false;
        }
    }

    outCanonicalPath = targetCanon.wstring();
    return true;
}

bool IntegrityVerifier::ComputeHandleSha256(HANDLE hFile, std::string& outHexSha256) {
    outHexSha256.clear();
    if (hFile == INVALID_HANDLE_VALUE || hFile == nullptr) return false;

    LARGE_INTEGER liZero{};
    SetFilePointerEx(hFile, liZero, nullptr, FILE_BEGIN);

    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }

    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return false;
    }

    std::vector<BYTE> buffer(64 * 1024);
    DWORD bytesRead = 0;
    bool success = true;

    while (ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        if (!CryptHashData(hash, buffer.data(), bytesRead, 0)) {
            success = false;
            break;
        }
    }

    if (success) {
        BYTE hashVal[32]{};
        DWORD hashLen = sizeof(hashVal);
        if (CryptGetHashParam(hash, HP_HASHVAL, hashVal, &hashLen, 0) && hashLen == 32) {
            std::ostringstream oss;
            for (DWORD i = 0; i < hashLen; ++i) {
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hashVal[i]);
            }
            outHexSha256 = oss.str();
        } else {
            success = false;
        }
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    return success && !outHexSha256.empty();
}

bool IntegrityVerifier::ComputeFileSha256(const std::wstring& filePath, std::string& outHexSha256) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = ComputeHandleSha256(hFile, outHexSha256);
    CloseHandle(hFile);
    return ok;
}

IntegrityStatus IntegrityVerifier::VerifyFile(const std::wstring& baseDir,
                                             const FileIntegrityRecord& record,
                                             bool useCache) {
    std::wstring fullPath;
    if (!IsSafeRelativePath(record.relativePath, baseDir, fullPath)) {
        return IntegrityStatus::ManifestInvalid;
    }

    // Abre o arquivo com FILE_SHARE_READ para garantir que nenhum outro processo
    // o altere durante a medição de integridade.
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return IntegrityStatus::Missing;
        }
        return IntegrityStatus::ReadError;
    }

    // Coleta metadados detalhados para resistência a timestomping
    FILE_BASIC_INFO basicInfo{};
    bool hasBasicInfo = GetFileInformationByHandleEx(hFile, FileBasicInfo, &basicInfo, sizeof(basicInfo)) != 0;

    FILE_ID_INFO idInfo{};
    bool hasIdInfo = GetFileInformationByHandleEx(hFile, FileIdInfo, &idInfo, sizeof(idInfo)) != 0;

    LARGE_INTEGER fSize{};
    GetFileSizeEx(hFile, &fSize);
    uint64_t fileSize = static_cast<uint64_t>(fSize.QuadPart);

    uint64_t lastWrite = hasBasicInfo ? static_cast<uint64_t>(basicInfo.LastWriteTime.QuadPart) : 0;
    uint64_t changeTime = hasBasicInfo ? static_cast<uint64_t>(basicInfo.ChangeTime.QuadPart) : 0;
    uint64_t volumeSerial = hasIdInfo ? idInfo.VolumeSerialNumber : 0;

    // Consulta de cache em memória
    if (useCache && hasBasicInfo) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(fullPath);
        if (it != m_cache.end()) {
            const auto& entry = it->second;
            // Validação estrita: exige correspondência de LastWriteTime, ChangeTime (resistente a timestomp),
            // tamanho de arquivo e volumeSerial
            if (entry.lastWriteTime == lastWrite &&
                entry.changeTime == changeTime &&
                entry.fileSize == fileSize &&
                entry.volumeSerial == volumeSerial) {
                m_cacheHits++;
                CloseHandle(hFile);
                return entry.status;
            }
        }
    }

    std::string calcSha256;
    if (!ComputeHandleSha256(hFile, calcSha256)) {
        CloseHandle(hFile);
        return IntegrityStatus::ReadError;
    }
    CloseHandle(hFile);

    m_hashCalculations++;

    std::string expectedNorm = record.expectedSha256;
    for (auto& c : expectedNorm) c = static_cast<char>(::tolower(c));
    for (auto& c : calcSha256) c = static_cast<char>(::tolower(c));

    IntegrityStatus result = (expectedNorm == calcSha256) ? IntegrityStatus::Valid : IntegrityStatus::Tampered;

    if (useCache && hasBasicInfo) {
        std::lock_guard<std::mutex> lock(m_mutex);
        CacheEntry entry;
        entry.lastWriteTime = lastWrite;
        entry.changeTime = changeTime;
        entry.fileSize = fileSize;
        entry.volumeSerial = volumeSerial;
        if (hasIdInfo) {
            std::memcpy(entry.fileId, idInfo.FileId.Identifier, sizeof(entry.fileId));
        }
        entry.sha256 = calcSha256;
        entry.status = result;
        m_cache[fullPath] = entry;
    }

    return result;
}

#else

namespace {

// O libcrypto nao e ligado em tempo de build: e carregado em tempo de execucao, do mesmo modo que
// o programa carrega NVAPI e DWM no Windows e o libX11 no motor adaptativo. Se os simbolos EVP nao
// existirem, nenhuma rota devolve hash e o chamador classifica como ReadError — nunca como Valid.
struct EvpFunctions {
    const void* (*EVP_sha256)() = nullptr;
    void* (*EVP_MD_CTX_new)() = nullptr;
    void (*EVP_MD_CTX_free)(void*) = nullptr;
    int (*EVP_DigestInit_ex)(void*, const void*, void*) = nullptr;
    int (*EVP_DigestUpdate)(void*, const void*, size_t) = nullptr;
    int (*EVP_DigestFinal_ex)(void*, unsigned char*, unsigned int*) = nullptr;
    bool loaded = false;
};

const EvpFunctions& Evp() {
    static EvpFunctions f;
    static bool attempted = false;
    if (attempted) return f;
    attempted = true;
    void* h = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return f;
    f.EVP_sha256 = reinterpret_cast<decltype(f.EVP_sha256)>(dlsym(h, "EVP_sha256"));
    f.EVP_MD_CTX_new = reinterpret_cast<decltype(f.EVP_MD_CTX_new)>(dlsym(h, "EVP_MD_CTX_new"));
    f.EVP_MD_CTX_free = reinterpret_cast<decltype(f.EVP_MD_CTX_free)>(dlsym(h, "EVP_MD_CTX_free"));
    f.EVP_DigestInit_ex =
        reinterpret_cast<decltype(f.EVP_DigestInit_ex)>(dlsym(h, "EVP_DigestInit_ex"));
    f.EVP_DigestUpdate =
        reinterpret_cast<decltype(f.EVP_DigestUpdate)>(dlsym(h, "EVP_DigestUpdate"));
    f.EVP_DigestFinal_ex =
        reinterpret_cast<decltype(f.EVP_DigestFinal_ex)>(dlsym(h, "EVP_DigestFinal_ex"));
    f.loaded = f.EVP_sha256 && f.EVP_MD_CTX_new && f.EVP_MD_CTX_free && f.EVP_DigestInit_ex &&
               f.EVP_DigestUpdate && f.EVP_DigestFinal_ex;
    return f;
}

} // namespace

bool IntegrityVerifier::IsSafeRelativePath(const std::string& relPath,
                                          const std::wstring& baseDir,
                                          std::wstring& outCanonicalPath) {
    outCanonicalPath.clear();
    if (relPath.empty()) return false;

    // Em Linux nao ha letra de unidade nem streams NTFS, portanto dois-pontos e um caractere
    // valido de nome de arquivo e nao e rejeitado. O que tem de sair e o caminho absoluto.
    if (relPath.front() == '/') return false;

    std::istringstream iss(relPath);
    std::string segment;
    while (std::getline(iss, segment, '/')) {
        if (segment == ".." || segment == ".") {
            return false;
        }
    }

    std::string baseUtf8 = StringUtils::ToUtf8(baseDir);
    char baseResolved[PATH_MAX];
    if (!realpath(baseUtf8.c_str(), baseResolved)) return false;

    std::string base = baseResolved;
    if (!base.empty() && base.back() != '/') base += '/';

    // O relPath ja nao comeca por '/' nem tem '..', logo a concatenacao fica dentro da base.
    std::string joined = base + relPath;

    // Se o arquivo existe, resolve os symlinks: e a saida real por link simbolico que faz neste
    // sistema o papel que no Windows fazem as streams NTFS. A base resolvida e o limite.
    char targetResolved[PATH_MAX];
    if (realpath(joined.c_str(), targetResolved)) {
        std::string real = targetResolved;
        if (real.size() < base.size()) return false;
        // ext4/xfs/btrfs distinguem maiusculas de minusculas: comparacao byte a byte, sem towlower.
        if (real.compare(0, base.size(), base) != 0) return false;
        outCanonicalPath = StringUtils::ToWide(real);
        return true;
    }

    // Arquivo em falta nao e travessia: o chamador abre e recebe Missing, como no Windows.
    outCanonicalPath = StringUtils::ToWide(joined);
    return true;
}

bool IntegrityVerifier::ComputeHandleSha256(int fd, std::string& outHexSha256) {
    outHexSha256.clear();
    if (fd < 0) return false;

    const EvpFunctions& evp = Evp();
    if (!evp.loaded) return false;

    if (lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) return false;

    void* ctx = evp.EVP_MD_CTX_new();
    if (!ctx) return false;

    bool success = (evp.EVP_DigestInit_ex(ctx, evp.EVP_sha256(), nullptr) == 1);
    std::vector<unsigned char> buffer(64 * 1024);

    while (success) {
        ssize_t got = read(fd, buffer.data(), buffer.size());
        if (got > 0) {
            success = (evp.EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(got)) == 1);
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        break; // got == 0: fim do arquivo
    }

    unsigned char hashVal[32]{};
    unsigned int hashLen = 0;
    if (success) {
        success = (evp.EVP_DigestFinal_ex(ctx, hashVal, &hashLen) == 1) && hashLen == 32;
    }
    evp.EVP_MD_CTX_free(ctx);
    if (!success) return false;

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hashVal[i]);
    }
    outHexSha256 = oss.str();
    return !outHexSha256.empty();
}

bool IntegrityVerifier::ComputeFileSha256(const std::wstring& filePath, std::string& outHexSha256) {
    std::string narrow = StringUtils::ToUtf8(filePath);
    int fd = open(narrow.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    bool ok = ComputeHandleSha256(fd, outHexSha256);
    close(fd);
    return ok;
}

IntegrityStatus IntegrityVerifier::VerifyFile(const std::wstring& baseDir,
                                             const FileIntegrityRecord& record,
                                             bool useCache) {
    std::wstring fullPath;
    if (!IsSafeRelativePath(record.relativePath, baseDir, fullPath)) {
        return IntegrityStatus::ManifestInvalid;
    }

    // A medicao e feita pelo fd, que aponta para o inode aberto aqui. Em POSIX nao ha lock
    // obrigatoria contra escrita (o FILE_SHARE_READ do Windows nao tem equivalente), por isso a
    // identidade do inode e o que impede que seja medido um arquivo trocado pelo caminho.
    std::string narrow = StringUtils::ToUtf8(fullPath);
    int fd = open(narrow.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return IntegrityStatus::Missing;
        }
        return IntegrityStatus::ReadError;
    }

    // Metadados detalhados para resistencia a timestomping. O ctime (ChangeTime) e o ponto forte
    // deste sistema: utimensat altera atime/mtime, mas nao aceita um ctime escolhido pelo chamador.
    struct stat st{};
    bool hasStat = (fstat(fd, &st) == 0);

    uint64_t fileSize = hasStat ? static_cast<uint64_t>(st.st_size) : 0;
    uint64_t lastWrite = hasStat ? static_cast<uint64_t>(st.st_mtim.tv_sec) * 1000000000ULL +
                                      static_cast<uint64_t>(st.st_mtim.tv_nsec)
                                 : 0;
    uint64_t changeTime = hasStat ? static_cast<uint64_t>(st.st_ctim.tv_sec) * 1000000000ULL +
                                       static_cast<uint64_t>(st.st_ctim.tv_nsec)
                                  : 0;
    uint64_t volumeSerial = hasStat ? static_cast<uint64_t>(st.st_dev) : 0;

    if (useCache && hasStat) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(fullPath);
        if (it != m_cache.end()) {
            const auto& entry = it->second;
            if (entry.lastWriteTime == lastWrite &&
                entry.changeTime == changeTime &&
                entry.fileSize == fileSize &&
                entry.volumeSerial == volumeSerial) {
                m_cacheHits++;
                close(fd);
                return entry.status;
            }
        }
    }

    std::string calcSha256;
    if (!ComputeHandleSha256(fd, calcSha256)) {
        close(fd);
        return IntegrityStatus::ReadError;
    }
    close(fd);

    m_hashCalculations++;

    std::string expectedNorm = record.expectedSha256;
    for (auto& c : expectedNorm) c = static_cast<char>(::tolower(c));
    for (auto& c : calcSha256) c = static_cast<char>(::tolower(c));

    IntegrityStatus result = (expectedNorm == calcSha256) ? IntegrityStatus::Valid : IntegrityStatus::Tampered;

    if (useCache && hasStat) {
        std::lock_guard<std::mutex> lock(m_mutex);
        CacheEntry entry;
        entry.lastWriteTime = lastWrite;
        entry.changeTime = changeTime;
        entry.fileSize = fileSize;
        entry.volumeSerial = volumeSerial;
        // Identidade fisica do arquivo: par (st_dev, st_ino) nos mesmos 16 bytes que no Windows
        // guardam VolumeSerial + FileId.
        std::memset(entry.fileId, 0, sizeof(entry.fileId));
        std::memcpy(entry.fileId, &st.st_dev, sizeof(st.st_dev) <= 8 ? sizeof(st.st_dev) : 8);
        std::memcpy(entry.fileId + 8, &st.st_ino, sizeof(st.st_ino) <= 8 ? sizeof(st.st_ino) : 8);
        entry.sha256 = calcSha256;
        entry.status = result;
        m_cache[fullPath] = entry;
    }

    return result;
}

#endif

} // namespace security
} // namespace opitcore
