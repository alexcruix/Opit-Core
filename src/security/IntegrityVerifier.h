#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <windows.h>
#include "SecurityTypes.h"

namespace opitcore {
namespace security {

class IntegrityVerifier {
public:
    IntegrityVerifier();
    ~IntegrityVerifier();

    /**
     * Calcula o hash SHA-256 de um handle aberto (leitura sequencial em streaming).
     */
    static bool ComputeHandleSha256(HANDLE hFile, std::string& outHexSha256);

    /**
     * Calcula o hash SHA-256 de um arquivo de forma segura e em streaming (blocos de 64KB).
     */
    static bool ComputeFileSha256(const std::wstring& filePath, std::string& outHexSha256);

    /**
     * Valida e sanitiza caminhos relativos contra ataques de Path Traversal e streams NTFS.
     */
    static bool IsSafeRelativePath(const std::string& relPath,
                                   const std::wstring& baseDir,
                                   std::wstring& outCanonicalPath);

    /**
     * Verifica um arquivo individual contra o hash esperado (com protecao contra timestomping).
     */
    IntegrityStatus VerifyFile(const std::wstring& baseDir,
                               const FileIntegrityRecord& record,
                               bool useCache = true);

    /**
     * Limpa o cache em memoria (ex: apos atualizacoes legitimas).
     */
    void InvalidateCache();

    uint32_t GetCacheHitCount() const { return m_cacheHits; }
    uint32_t GetHashCalculationCount() const { return m_hashCalculations; }

private:
    struct CacheEntry {
        uint64_t lastWriteTime{0};
        uint64_t changeTime{0};
        uint64_t fileSize{0};
        uint64_t volumeSerial{0};
        BYTE fileId[16]{};
        std::string sha256;
        IntegrityStatus status{IntegrityStatus::Valid};
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, CacheEntry> m_cache;
    uint32_t m_cacheHits{0};
    uint32_t m_hashCalculations{0};
};

} // namespace security
} // namespace opitcore
