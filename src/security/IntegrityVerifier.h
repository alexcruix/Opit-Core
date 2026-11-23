// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif
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
#ifdef _WIN32
    static bool ComputeHandleSha256(HANDLE hFile, std::string& outHexSha256);
#else
    // Em Linux o "handle" e o descritor de arquivo obtido por open(): mede o MESMO inode que o
    // chamador abriu, que e o que da a proteção contra troca de arquivo durante a medicao.
    static bool ComputeHandleSha256(int fd, std::string& outHexSha256);
#endif

    /**
     * Calcula o hash SHA-256 de um arquivo de forma segura e em streaming (blocos de 64KB).
     */
    static bool ComputeFileSha256(const std::wstring& filePath, std::string& outHexSha256);

    /**
     * Valida e sanitiza caminhos relativos contra ataques de Path Traversal e streams NTFS.
     * Em Linux nao ha streams NTFS: o equivalente protegido e a saida por symlink, resolvida
     * com realpath() antes da comparacao de fronteira.
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
#ifdef _WIN32
        BYTE fileId[16]{};
#else
        // Em Linux os mesmos 16 bytes guardam o par (st_dev, st_ino): a identidade fisica do inode.
        unsigned char fileId[16]{};
#endif
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
