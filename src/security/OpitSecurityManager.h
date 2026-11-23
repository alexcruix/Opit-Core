// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "SecurityTypes.h"
#include "ManifestVerifier.h"
#include "IntegrityVerifier.h"
#include "ModuleVerifier.h"
#include "SecurityPerformanceMonitor.h"

namespace opitcore {
namespace security {

class OpitSecurityManager {
public:
    static OpitSecurityManager& Instance();

    /**
     * Inicializa o subsistema de segurança defensiva:
     * - Configura ordem segura de busca de DLLs
     * - Define diretório base da aplicação
     */
    bool Initialize(const std::wstring& appDirectory);

    /**
     * Executa a verificação completa de manifesto e arquivos na inicialização.
     * Retorna true se a integridade estiver conforme.
     */
    bool VerifyInstallationIntegrity();

    /**
     * Valida um arquivo individual (ex: DLL antes de ser carregada dinamicamente).
     */
    IntegrityStatus VerifyModuleFile(const std::wstring& relativePath, const std::string& expectedSha256 = "");

    /**
     * Valida e carrega um modulo DLL usando a politica rigorosa de integridade e protecao TOCTOU.
     */
#ifdef _WIN32
    HMODULE LoadVerifiedModule(const std::wstring& relativePath, IntegrityStatus& outStatus);
#else
    // Em Linux o handle e o ponteiro do dlopen(); a politica e a mesma: hash provado no manifesto
    // assinado antes de qualquer carregamento.
    void* LoadVerifiedModule(const std::wstring& relativePath, IntegrityStatus& outStatus);
#endif

    /**
     * Verifica se um componente especifico esta autorizado para execucao (sem eventos graves pendentes).
     */
    bool IsComponentAuthorized(const std::string& componentOrRelativePath) const;

    /**
     * Executa a auditoria defensiva em tempo de execução:
     * - Checagem de anti-engenharia reversa e anti-debug
     * - Checagem de integridade da seção .text em memória contra patches
     */
    bool AuditRuntimeShield();

    bool IsSystemSecure() const { return m_isSecure; }
    SecurityCategory GetHighestViolationLevel() const { return m_highestCategory; }
    const std::vector<SecurityEvent>& GetSecurityEvents() const { return m_events; }
    SecurityMetrics GetMetrics() const;
    std::string GetRecoveryNotice() const;

    /**
     * Configuração para testes automatizados determinísticos.
     */
    void SetCustomPublicKeySpki(const std::string& spkiB64);
    void ResetState();

private:
    OpitSecurityManager();
    ~OpitSecurityManager() = default;

    std::wstring m_appDir;
    bool m_initialized{false};
    bool m_isSecure{true};
    SecurityCategory m_highestCategory{SecurityCategory::CategoryA};

    ManifestVerifier m_manifestVerifier;
    IntegrityVerifier m_integrityVerifier;
    ModuleVerifier m_moduleVerifier;
    SecurityPerformanceMonitor m_perfMonitor;

    std::vector<FileIntegrityRecord> m_records;
    std::vector<SecurityEvent> m_events;
};

} // namespace security
} // namespace opitcore
