#include "IntegrityVerifier.h"
#include "utils/StringUtils.h"

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

namespace opitcore {
namespace security {

IntegrityVerifier::IntegrityVerifier() = default;
IntegrityVerifier::~IntegrityVerifier() = default;

void IntegrityVerifier::InvalidateCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

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

} // namespace security
} // namespace opitcore
