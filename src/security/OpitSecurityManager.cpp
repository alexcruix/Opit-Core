// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "OpitSecurityManager.h"
#include "SecurityPolicy.h"
#include "RecoveryCoordinator.h"
#include "AntiReverseEngineering.h"
#include "core/Logger.h"
#include "utils/StringUtils.h"
#include "security/OpitSecrets.h"

#ifdef _WIN32

#include <windows.h>

#else

#include <limits.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>

namespace {

// Equivalente do GetTickCount64 do Windows para as marcas de tempo dos eventos de seguranca.
uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

#endif

namespace opitcore {
namespace security {

OpitSecurityManager& OpitSecurityManager::Instance() {
    static OpitSecurityManager instance;
    return instance;
}

OpitSecurityManager::OpitSecurityManager() = default;

void OpitSecurityManager::SetCustomPublicKeySpki(const std::string& spkiB64) {
    m_manifestVerifier.SetCustomPublicKeySpki(spkiB64);
}

void OpitSecurityManager::ResetState() {
    m_initialized = false;
    m_isSecure = true;
    m_highestCategory = SecurityCategory::CategoryA;
    m_records.clear();
    m_events.clear();
    m_integrityVerifier.InvalidateCache();
}

bool OpitSecurityManager::Initialize(const std::wstring& appDirectory) {
    m_appDir = appDirectory;
    ModuleVerifier::EnforceSecureDllSearchOrder();
    m_initialized = true;
#ifdef _WIN32
    OPIT_LOG_INFO("[SECURITY] Modulo OpitSecurity inicializado com busca restrita de DLLs.");
#else
    OPIT_LOG_INFO("[SECURITY] Modulo OpitSecurity inicializado; o carregamento dinamico exige "
                  "caminho absoluto e hash provado.");
#endif
    return true;
}

bool OpitSecurityManager::VerifyInstallationIntegrity() {
    if (!m_initialized && m_appDir.empty()) {
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring ws(exePath);
        size_t pos = ws.find_last_of(L"\\/");
        m_appDir = (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
#else
        // O link /proc/self/exe cumpre o papel do GetModuleFileNameW: da o caminho do executavel
        // em execucao, sejam quais forem o CWD e o PATH com que foi lancado.
        char exePath[PATH_MAX];
        ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (exeLen > 0) {
            exePath[exeLen] = '\0';
            std::string narrow(exePath, static_cast<size_t>(exeLen));
            size_t pos = narrow.find_last_of('/');
            m_appDir = (pos != std::string::npos) ? StringUtils::ToWide(narrow.substr(0, pos)) : L".";
        } else {
            m_appDir = L".";
        }
#endif
    }

    m_events.clear();
    m_perfMonitor.StartStartupTimer();

#ifdef _WIN32
    std::wstring manifestPath = m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_file_name());
    std::wstring signaturePath = m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_sig_name());
#else
    std::wstring manifestPath = m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_file_name());
    std::wstring signaturePath = m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_sig_name());
#endif

    std::string loadErr;
    IntegrityStatus manifestStatus = m_manifestVerifier.VerifyAndLoad(manifestPath, signaturePath, m_records, loadErr);

    if (manifestStatus != IntegrityStatus::Valid) {
        SecurityEvent ev;
        ev.component = "Manifest";
        ev.status = manifestStatus;
        ev.description = loadErr.empty() ? IntegrityStatusToString(manifestStatus) : loadErr;
        ev.category = SecurityPolicy::EvaluateStatus(manifestStatus, true);
        m_events.push_back(ev);

        Logger::Instance().Log(LogLevel::Error, "security",
            "[SECURITY] Falha ao verificar manifesto de integridade: " + ev.description);
    } else {
        OPIT_LOG_INFO("[SECURITY] Assinatura digital RSA-SHA256 do manifesto validada com sucesso.");

        // Verifica cada arquivo catalogado
        for (const auto& rec : m_records) {
            uint32_t prevHits = m_integrityVerifier.GetCacheHitCount();
            uint32_t prevCalcs = m_integrityVerifier.GetHashCalculationCount();

            IntegrityStatus fileStatus = m_integrityVerifier.VerifyFile(m_appDir, rec, true);

            bool hit = (m_integrityVerifier.GetCacheHitCount() > prevHits);
            bool calc = (m_integrityVerifier.GetHashCalculationCount() > prevCalcs);
            m_perfMonitor.RecordCheck(hit, calc);

            if (fileStatus != IntegrityStatus::Valid) {
                SecurityEvent ev;
                ev.component = rec.componentId.empty() ? rec.relativePath : rec.componentId;
                ev.status = fileStatus;
                ev.description = "Integridade violada no arquivo: " + rec.relativePath;
                ev.category = SecurityPolicy::EvaluateStatus(fileStatus, rec.isCritical);
                m_events.push_back(ev);

                Logger::Instance().Log(LogLevel::Warning, "security",
                    "[SECURITY] Arquivo corrompido ou modificado: " + rec.relativePath +
                    " (Status: " + IntegrityStatusToString(fileStatus) + ")");
            }
        }
    }

    m_perfMonitor.StopStartupTimer();

    bool shouldBlock = false;
    SecurityPolicy::EvaluateOverall(m_events, shouldBlock, m_highestCategory);
    m_isSecure = !shouldBlock;

    auto metrics = m_perfMonitor.GetMetrics();
    OPIT_LOG_INFO("[SECURITY] Verificacao de integridade concluida em " +
                  std::to_string(metrics.startupCheckTimeMs) + " ms (" +
                  std::to_string(metrics.totalFilesChecked) + " arquivos verificados, " +
                  std::to_string(metrics.cacheHits) + " cache hits). Seguro: " + (m_isSecure ? "SIM" : "NAO"));

    return m_isSecure;
}

IntegrityStatus OpitSecurityManager::VerifyModuleFile(const std::wstring& relativePath, const std::string& expectedSha256) {
    if (m_appDir.empty()) {
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring ws(exePath);
        size_t pos = ws.find_last_of(L"\\/");
        m_appDir = (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
#else
        // O link /proc/self/exe cumpre o papel do GetModuleFileNameW: da o caminho do executavel
        // em execucao, sejam quais forem o CWD e o PATH com que foi lancado.
        char exePath[PATH_MAX];
        ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (exeLen > 0) {
            exePath[exeLen] = '\0';
            std::string narrow(exePath, static_cast<size_t>(exeLen));
            size_t pos = narrow.find_last_of('/');
            m_appDir = (pos != std::string::npos) ? StringUtils::ToWide(narrow.substr(0, pos)) : L".";
        } else {
            m_appDir = L".";
        }
#endif
    }

    FileIntegrityRecord rec;
    rec.relativePath = StringUtils::ToUtf8(relativePath);
    rec.expectedSha256 = expectedSha256;
    rec.isCritical = true;

    if (rec.expectedSha256.empty()) {
        if (m_records.empty()) {
            // Se o manifesto nao foi carregado, tenta carregar agora
            std::string loadErr;
#ifdef _WIN32
            m_manifestVerifier.VerifyAndLoad(m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_file_name()),
                                             m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_sig_name()),
                                             m_records, loadErr);
#else
            m_manifestVerifier.VerifyAndLoad(m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_file_name()),
                                             m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_sig_name()),
                                             m_records, loadErr);
#endif
        }

        for (const auto& item : m_records) {
            if (item.relativePath == rec.relativePath) {
                rec.expectedSha256 = item.expectedSha256;
                rec.isCritical = item.isCritical;
                break;
            }
        }
    }

    if (rec.expectedSha256.empty()) {
        // Arquivo nao catalogado no manifesto assinado: recusa por padrao (Defesa em Profundidade)
        return IntegrityStatus::UntrustedSignature;
    }

    return m_integrityVerifier.VerifyFile(m_appDir, rec, true);
}

#ifdef _WIN32
HMODULE OpitSecurityManager::LoadVerifiedModule(const std::wstring& relativePath, IntegrityStatus& outStatus) {
#else
void* OpitSecurityManager::LoadVerifiedModule(const std::wstring& relativePath, IntegrityStatus& outStatus) {
#endif
    outStatus = IntegrityStatus::Missing;

    if (m_appDir.empty()) {
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring ws(exePath);
        size_t pos = ws.find_last_of(L"\\/");
        m_appDir = (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
#else
        // O link /proc/self/exe cumpre o papel do GetModuleFileNameW: da o caminho do executavel
        // em execucao, sejam quais forem o CWD e o PATH com que foi lancado.
        char exePath[PATH_MAX];
        ssize_t exeLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (exeLen > 0) {
            exePath[exeLen] = '\0';
            std::string narrow(exePath, static_cast<size_t>(exeLen));
            size_t pos = narrow.find_last_of('/');
            m_appDir = (pos != std::string::npos) ? StringUtils::ToWide(narrow.substr(0, pos)) : L".";
        } else {
            m_appDir = L".";
        }
#endif
    }

    std::string relUtf8 = StringUtils::ToUtf8(relativePath);
    std::string expSha;
    bool isCritical = false;

    if (m_records.empty()) {
        std::string loadErr;
#ifdef _WIN32
        m_manifestVerifier.VerifyAndLoad(m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_file_name()),
                                         m_appDir + L"\\" + StringUtils::ToWide(secrets::manifest_sig_name()),
                                         m_records, loadErr);
#else
        m_manifestVerifier.VerifyAndLoad(m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_file_name()),
                                         m_appDir + L"/" + StringUtils::ToWide(secrets::manifest_sig_name()),
                                         m_records, loadErr);
#endif
    }

    for (const auto& item : m_records) {
        if (item.relativePath == relUtf8) {
            expSha = item.expectedSha256;
            isCritical = item.isCritical;
            break;
        }
    }

    if (expSha.empty()) {
        // Binario desconhecido ou fora do manifesto assinado: recusa de carregamento
        outStatus = IntegrityStatus::UntrustedSignature;
        SecurityEvent ev;
        ev.component = relUtf8;
        ev.status = outStatus;
        ev.description = "Tentativa de carregamento de modulo nao catalogado no manifesto: " + relUtf8;
        ev.category = SecurityCategory::CategoryB;
        m_events.push_back(ev);
        return nullptr;
    }

    std::wstring canonicalPath;
    if (!IntegrityVerifier::IsSafeRelativePath(relUtf8, m_appDir, canonicalPath)) {
        outStatus = IntegrityStatus::ManifestInvalid;
        SecurityEvent ev;
        ev.component = relUtf8;
        ev.status = outStatus;
        ev.description = "Path traversal detectado na requisicao de carregamento de modulo: " + relUtf8;
        ev.category = SecurityCategory::CategoryB;
        m_events.push_back(ev);
        return nullptr;
    }

#ifdef _WIN32
    HMODULE hMod = m_moduleVerifier.VerifyAndLoad(canonicalPath, expSha, outStatus);
#else
    void* hMod = m_moduleVerifier.VerifyAndLoad(canonicalPath, expSha, outStatus);
#endif
    if (outStatus != IntegrityStatus::Valid) {
        SecurityEvent ev;
        ev.component = relUtf8;
        ev.status = outStatus;
        ev.description = "Falha de integridade ao carregar modulo dinamico: " + relUtf8 +
                         " (" + IntegrityStatusToString(outStatus) + ")";
        ev.category = SecurityPolicy::EvaluateStatus(outStatus, isCritical);
        m_events.push_back(ev);
    }

    return hMod;
}

bool OpitSecurityManager::IsComponentAuthorized(const std::string& componentOrRelativePath) const {
    if (!m_isSecure) return false;
    for (const auto& ev : m_events) {
        if (ev.component == componentOrRelativePath && ev.status != IntegrityStatus::Valid) {
            return false;
        }
    }
    return true;
}

SecurityMetrics OpitSecurityManager::GetMetrics() const {
    return m_perfMonitor.GetMetrics();
}

std::string OpitSecurityManager::GetRecoveryNotice() const {
    return RecoveryCoordinator::Instance().BuildRecoveryNotice(m_events);
}

bool OpitSecurityManager::AuditRuntimeShield() {
    bool passed = true;

    // 1. Checagem de Depuradores
    if (AntiReverseEngineering::DetectDebugger()) {
        SecurityEvent ev;
        ev.category = SecurityCategory::CategoryC;
        ev.component = "AntiReverseEngineering";
        ev.description = "Depurador ativo ou registradores de hardware (DR0-DR3) detectados no processo.";
        ev.status = IntegrityStatus::Tampered;
#ifdef _WIN32
        ev.timestampMs = GetTickCount64();
#else
        ev.timestampMs = NowMs();
#endif
        m_events.push_back(ev);
        m_isSecure = false;
        m_highestCategory = SecurityCategory::CategoryC;
        Logger::Instance().Log(LogLevel::Critical, "security",
            "[SECURITY CRITICAL] Tentativa de depuracao / invasao detectada: " + ev.description);
        passed = false;
    }

    // 2. Checagem de Integridade da Secao de Codigo (.text) em Memoria
    if (!AntiReverseEngineering::VerifyCodeSectionIntegrity()) {
        SecurityEvent ev;
        ev.category = SecurityCategory::CategoryC;
        ev.component = "AntiReverseEngineering";
        ev.description = "Adulteracao de memoria na secao .text do executavel (patch de bytes/hooks/shellcode).";
        ev.status = IntegrityStatus::Tampered;
#ifdef _WIN32
        ev.timestampMs = GetTickCount64();
#else
        ev.timestampMs = NowMs();
#endif
        m_events.push_back(ev);
        m_isSecure = false;
        m_highestCategory = SecurityCategory::CategoryC;
        Logger::Instance().Log(LogLevel::Critical, "security",
            "[SECURITY CRITICAL] Violacao de integridade em memoria: " + ev.description);
        passed = false;
    }

    return passed;
}

} // namespace security
} // namespace opitcore
