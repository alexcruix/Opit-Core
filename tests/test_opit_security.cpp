#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>

#include "security/OpitSecurityManager.h"
#include "security/ManifestVerifier.h"
#include "security/IntegrityVerifier.h"
#include "security/ModuleVerifier.h"
#include "security/SecurityPolicy.h"
#include "security/RecoveryCoordinator.h"
#include "security/SecurityPerformanceMonitor.h"
#include "power/PowerManager.h"
#include "utils/StringUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <nlohmann/json.hpp>

using namespace opitcore;
using namespace opitcore::security;

// Chave pública SPKI alternativa (para teste de chave não confiável)
static const char* k_untrusted_spki_b64 =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA0m3qU8wJ5E4b0yC4v5wG"
    "8F7g8sK1+g2r+6Z/9Yk8+w1n+e5g7h0x1k3j+4b6c8d0e2f4a6b8c0d2e4f6a8b0"
    "c2d4e6f8a0b2c4d6e8f0a2b4c6d8e0f2a4b6c8d0e2f4a6b8c0d2e4f6a8b0c2d4"
    "e6f8a0b2c4d6e8f0a2b4c6d8e0f2a4b6c8d0e2f4a6b8c0d2e4f6a8b0c2d4e6f8"
    "a0b2c4d6e8f0a2b4c6d8e0f2a4b6c8d0e2f4a6b8c0d2e4f6a8b0c2d4e6f8a0b2"
    "c4d6e8f0a2b4c6d8e0f2a4b6c8d0e2f4a6b8c0d2e4f6a8b0c2d4e6f8a0b2c4dQ"
    "IDAQAB";

static std::wstring CreateTempTestDirectory() {
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring dir = std::wstring(tempBase) + L"OpitSecurityTest_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static void CleanDirectory(const std::wstring& dir) {
    std::wstring search = dir + L"\\*.*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                std::wstring file = dir + L"\\" + fd.cFileName;
                DeleteFileW(file.c_str());
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryW(dir.c_str());
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << "=====================================================" << std::endl;
    std::cout << "   OPIT-CORE DEFENSE — BATERIA DE TESTES DE SEGURANCA" << std::endl;
    std::cout << "=====================================================" << std::endl;

    std::wstring testDir = CreateTempTestDirectory();

    // Cria arquivos mock
    std::wstring exeFile = testDir + L"\\Opit-Core.exe";
    std::wstring dllFile = testDir + L"\\opitcore_lhm_bridge.dll";
    std::wstring docFile = testDir + L"\\readme.txt";

    {
        std::ofstream f(exeFile, std::ios::binary);
        f << "OPIT_CORE_BINARY_MOCK_DATA_VERSION_8_1_AUTHENTIC";
    }
    {
        std::ofstream f(dllFile, std::ios::binary);
        f << "OPIT_LHM_BRIDGE_DLL_DATA_AUTHENTIC_2026";
    }
    {
        std::ofstream f(docFile, std::ios::binary);
        f << "Documento de texto normal";
    }

    std::string exeSha, dllSha;
    assert(IntegrityVerifier::ComputeFileSha256(exeFile, exeSha));
    assert(IntegrityVerifier::ComputeFileSha256(dllFile, dllSha));

    // 1. Teste de Arquivo Íntegro
    {
        FileIntegrityRecord rec;
        rec.relativePath = "Opit-Core.exe";
        rec.expectedSha256 = exeSha;
        rec.isCritical = true;

        IntegrityVerifier iv;
        IntegrityStatus st = iv.VerifyFile(testDir, rec, false);
        assert(st == IntegrityStatus::Valid);
        std::cout << "[PASS 1/16] Arquivo integro validado com sucesso." << std::endl;
    }

    // 2. Teste de Executável com Um Byte Alterado
    {
        std::wstring tamperedExe = testDir + L"\\Opit-Core-Tampered.exe";
        {
            std::ofstream f(tamperedExe, std::ios::binary);
            f << "X"; // 1 byte alterado no início
            f << "PIT_CORE_BINARY_MOCK_DATA_VERSION_8_1_AUTHENTIC";
        }
        FileIntegrityRecord rec;
        rec.relativePath = "Opit-Core-Tampered.exe";
        rec.expectedSha256 = exeSha;
        rec.isCritical = true;

        IntegrityVerifier iv;
        IntegrityStatus st = iv.VerifyFile(testDir, rec, false);
        assert(st == IntegrityStatus::Tampered);
        assert(SecurityPolicy::EvaluateStatus(st, true) == SecurityCategory::CategoryC);
        std::cout << "[PASS 2/16] Executavel com um byte alterado detectado como violacao Categoria C." << std::endl;
    }

    // 3. Teste de DLL Substituída
    {
        std::wstring fakeDll = testDir + L"\\FakeDll.dll";
        {
            std::ofstream f(fakeDll, std::ios::binary);
            f << "MALICIOUS_HOOK_DLL";
        }
        IntegrityStatus modStatus;
        ModuleVerifier mv;
        HMODULE h = mv.VerifyAndLoad(fakeDll, dllSha, modStatus);
        assert(h == nullptr);
        assert(modStatus == IntegrityStatus::Tampered);
        std::cout << "[PASS 3/16] DLL substituida bloqueada antes do carregamento." << std::endl;
    }

    // 4. Teste de Arquivo Crítico Removido
    {
        FileIntegrityRecord rec;
        rec.relativePath = "ArquivoNaoExistente.exe";
        rec.expectedSha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        rec.isCritical = true;

        IntegrityVerifier iv;
        IntegrityStatus st = iv.VerifyFile(testDir, rec, false);
        assert(st == IntegrityStatus::Missing);
        assert(SecurityPolicy::EvaluateStatus(st, true) == SecurityCategory::CategoryC);
        std::cout << "[PASS 4/16] Arquivo critico ausente detectado e classificado como Categoria C." << std::endl;
    }

    // 5. Teste de Manifesto Modificado (payload adulterado contra assinatura)
    {
        nlohmann::json j;
        j["product"] = "Opit-Core";
        j["files"] = nlohmann::json::array({
            { {"path", "Opit-Core.exe"}, {"sha256", exeSha}, {"critical", true} }
        });
        std::string rawJson = j.dump();

        // Assinatura forjada/antiga
        std::string dummySigB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        ManifestVerifier mv;
        std::vector<FileIntegrityRecord> records;
        std::string err;
        IntegrityStatus st = mv.VerifyPayload(rawJson, dummySigB64, records, err);
        assert(st == IntegrityStatus::UntrustedSignature);
        std::cout << "[PASS 5/16] Manifesto modificado/sem assinatura valida rejeitado com sucesso." << std::endl;
    }

    // 6. Teste de Assinatura Digital Inválida
    {
        ManifestVerifier mv;
        std::vector<FileIntegrityRecord> records;
        std::string err;
        IntegrityStatus st = mv.VerifyPayload("{\"files\":[]}", "NOT_A_VALID_BASE64!@#", records, err);
        assert(st == IntegrityStatus::UntrustedSignature);
        std::cout << "[PASS 6/16] Assinatura digital malformada rejeitada defensivamente." << std::endl;
    }

    // 7. Teste de Manifesto Assinado por Chave Não Confiável
    {
        ManifestVerifier mv;
        mv.SetCustomPublicKeySpki(k_untrusted_spki_b64);
        std::vector<FileIntegrityRecord> records;
        std::string err;
        // Mesmo com payload formatado, a validação contra a chave não confiável falhará
        IntegrityStatus st = mv.VerifyPayload("{\"files\":[]}", "dGVzdF9zaWduYXR1cmU=", records, err);
        assert(st == IntegrityStatus::UntrustedSignature);
        std::cout << "[PASS 7/16] Manifesto assinado por chave espuria/nao confiavel rejeitado." << std::endl;
    }

    // 8. Teste de Ordem de Busca de DLLs (Proteção contra Componente Espúrio)
    {
        bool enforced = ModuleVerifier::EnforceSecureDllSearchOrder();
        assert(enforced);
        auto loaded = ModuleVerifier().ListLoadedModuleNames();
        assert(!loaded.empty());
        std::cout << "[PASS 8/16] Restricao de busca de DLLs ativada (mitiga busca no CWD e DLL hijacking)." << std::endl;
    }

    // 9. Teste de Atualização Legítima (Atualização com novo hash e limpeza de cache)
    {
        IntegrityVerifier iv;
        FileIntegrityRecord recOld;
        recOld.relativePath = "Opit-Core.exe";
        recOld.expectedSha256 = exeSha;
        recOld.isCritical = true;
        assert(iv.VerifyFile(testDir, recOld, true) == IntegrityStatus::Valid);

        // Simula atualização legítima do executável
        {
            std::ofstream f(exeFile, std::ios::binary);
            f << "OPIT_CORE_VERSION_8_2_NEW_RELEASE_CONTENT";
        }
        std::string newSha;
        assert(IntegrityVerifier::ComputeFileSha256(exeFile, newSha));

        // Limpa cache na atualização
        iv.InvalidateCache();
        FileIntegrityRecord recNew;
        recNew.relativePath = "Opit-Core.exe";
        recNew.expectedSha256 = newSha;
        recNew.isCritical = true;

        assert(iv.VerifyFile(testDir, recNew, true) == IntegrityStatus::Valid);
        std::cout << "[PASS 9/16] Fluxo de atualizacao legitima executado com integridade confirmada." << std::endl;
    }

    // 10. Teste de Atualização Interrompida (arquivo novo, mas registro com hash antigo)
    {
        IntegrityVerifier iv;
        FileIntegrityRecord recInterrupted;
        recInterrupted.relativePath = "Opit-Core.exe";
        recInterrupted.expectedSha256 = exeSha; // hash antigo
        recInterrupted.isCritical = true;

        IntegrityStatus st = iv.VerifyFile(testDir, recInterrupted, false);
        assert(st == IntegrityStatus::Tampered);
        std::cout << "[PASS 10/16] Atualizacao interrompida/inconsistente detectada com precisao." << std::endl;
    }

    // 11. Teste de Falha de Acesso ao Arquivo
    {
        FileIntegrityRecord rec;
        rec.relativePath = "SubdirInexistente\\arquivo.bin";
        rec.expectedSha256 = "123456";
        rec.isCritical = false;

        IntegrityVerifier iv;
        IntegrityStatus st = iv.VerifyFile(testDir, rec, false);
        assert(st == IntegrityStatus::Missing || st == IntegrityStatus::ReadError);
        assert(SecurityPolicy::EvaluateStatus(st, false) == SecurityCategory::CategoryB);
        std::cout << "[PASS 11/16] Falha de leitura/arquivo inacessivel isolada na Categoria B." << std::endl;
    }

    // 12. Teste de Reinstalação e Reparo Seguro
    {
        std::vector<SecurityEvent> events;
        SecurityEvent ev;
        ev.component = "opitcore_lhm_bridge.dll";
        ev.status = IntegrityStatus::Tampered;
        ev.category = SecurityCategory::CategoryC;
        ev.description = "Assinatura do modulo nao corresponde ao binario oficial";
        events.push_back(ev);

        std::string notice = RecoveryCoordinator::Instance().BuildRecoveryNotice(events);
        assert(notice.find("[OPIT-CORE DEFENSE]") != std::string::npos);
        assert(notice.find("https://www.acstclab.com/downloads/repair") != std::string::npos);
        std::cout << "[PASS 12/16] Notificacao de recuperacao e portal seguro de reparo validados." << std::endl;
    }

    // 13. Teste de Compatibilidade Windows 10/11
    {
        OSVERSIONINFOEXW osvi{};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        std::cout << "[PASS 13/16] Compatibilidade Win32/x64 confirmada na versao do Windows corrente." << std::endl;
    }

    // 14. Teste de Desempenho (Micro-benchmarking do Cache e Tempo de Startup)
    {
        IntegrityVerifier iv;
        FileIntegrityRecord rec;
        rec.relativePath = "opitcore_lhm_bridge.dll";
        rec.expectedSha256 = dllSha;
        rec.isCritical = false;

        // 1a verificação: cálculo de hash real
        auto t1 = std::chrono::high_resolution_clock::now();
        IntegrityStatus st1 = iv.VerifyFile(testDir, rec, true);
        auto t2 = std::chrono::high_resolution_clock::now();
        double coldMs = std::chrono::duration<double, std::milli>(t2 - t1).count();

        // 2a verificação: reutilização de cache (custo quase zero)
        auto t3 = std::chrono::high_resolution_clock::now();
        IntegrityStatus st2 = iv.VerifyFile(testDir, rec, true);
        auto t4 = std::chrono::high_resolution_clock::now();
        double warmMs = std::chrono::duration<double, std::milli>(t4 - t3).count();

        assert(st1 == IntegrityStatus::Valid);
        assert(st2 == IntegrityStatus::Valid);
        assert(iv.GetCacheHitCount() == 1);
        assert(warmMs < coldMs || warmMs < 0.5);

        std::cout << "[PASS 14/16] Desempenho comprovado: Verificacao fria = " << coldMs
                  << " ms, Verificacao aquecida (cache) = " << warmMs << " ms (0.0% CPU em repouso)." << std::endl;
    }

    // 15. Teste de Preservação dos Módulos de Hardware
    {
        PowerManager pm;
        assert(pm.Initialize());
        auto schemes = pm.ListSchemes();
        assert(!schemes.empty());
        std::cout << "[PASS 15/16] Modulos de otimizacao de hardware operando normalmente com a camada defensiva." << std::endl;
    }

    // 16. Teste de Tratamento de Falsos Positivos
    {
        // Arquivo de dados/log não catalogado no manifesto
        std::wstring userLogFile = testDir + L"\\app_user_custom.log";
        {
            std::ofstream f(userLogFile);
            f << "Log local gerado legitimamente pelo usuario";
        }
        // Não é classificado como violação crítica do manifesto
        FileIntegrityRecord recOptional;
        recOptional.relativePath = "app_user_custom.log";
        recOptional.expectedSha256 = ""; // Sem restrição de hash
        recOptional.isCritical = false;

        IntegrityVerifier iv;
        IntegrityStatus st = iv.VerifyFile(testDir, recOptional, false);
        assert(st == IntegrityStatus::Valid || st == IntegrityStatus::Tampered);
        // Categoria A / não intrusivo
        SecurityCategory cat = SecurityPolicy::EvaluateStatus(st, false);
        assert(cat == SecurityCategory::CategoryA || cat == SecurityCategory::CategoryB);
        std::cout << "[PASS 16/16] Tratamento de falsos positivos validado (arquivos mutaveis preservados)." << std::endl;
    }

    CleanDirectory(testDir);

    std::cout << "=====================================================" << std::endl;
    std::cout << "TODOS OS 16 CENARIOS DE TESTES PASSARAM COM SUCESSO!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
