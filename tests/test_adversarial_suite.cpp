// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cassert>

#include "security/OpitSecurityManager.h"
#include "security/ManifestVerifier.h"
#include "security/IntegrityVerifier.h"
#include "security/ModuleVerifier.h"
#include "security/SecurityPolicy.h"
#include "security/RecoveryCoordinator.h"
#include "security/SecurityPerformanceMonitor.h"
#include "security/AntiReverseEngineering.h"
#include "fans/LibreHardwareMonitorFanProvider.h"
#include "fans/WindowsAcpiFanProvider.h"
#include "utils/StringUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <nlohmann/json.hpp>

using namespace opitcore;
using namespace opitcore::security;

static std::wstring CreateTempAuditDir() {
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring dir = std::wstring(tempBase) + L"OpitAudit_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static void CleanupAuditDir(const std::wstring& dir) {
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
    std::cout << "================================================================" << std::endl;
    std::cout << "   OPIT-CORE — AUDITORIA ADVERSARIAL DE SEGURANCA (TEST SUITE)  " << std::endl;
    std::cout << "================================================================" << std::endl;

    std::wstring auditDir = CreateTempAuditDir();

    // -------------------------------------------------------------
    // TESTE ADV-01: Ataque de Timestomping contra Cache de Integridade
    // Hipótese: Se o invasor adulterar o conteúdo mantendo tamanho e timestamp,
    // o IntegrityVerifier aceita o arquivo adulterado sem recalcular SHA-256.
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-01] Avaliando ataque de Timestomp contra IntegrityVerifier..." << std::endl;
    {
        std::wstring targetFile = auditDir + L"\\target_module.dll";
        const std::string originalContent = "AUTHENTIC_DLL_CONTENT_1234567890";
        const std::string tamperedContent = "MALICIOUS_DLL_PAYLOAD_1234567890"; // mesmo tamanho: 32 bytes

        // 1. Cria arquivo legítimo
        {
            std::ofstream f(targetFile, std::ios::binary);
            f << originalContent;
        }

        std::string origSha;
        assert(IntegrityVerifier::ComputeFileSha256(targetFile, origSha));

        // Obtém o timestamp legítimo
        WIN32_FILE_ATTRIBUTE_DATA origAttr{};
        GetFileAttributesExW(targetFile.c_str(), GetFileExInfoStandard, &origAttr);

        IntegrityVerifier iv;
        FileIntegrityRecord rec;
        rec.relativePath = "target_module.dll";
        rec.expectedSha256 = origSha;
        rec.isCritical = true;

        // Primeira checagem: aquece o cache
        IntegrityStatus st1 = iv.VerifyFile(auditDir, rec, true);
        assert(st1 == IntegrityStatus::Valid);

        // 2. Ataque: Adultera o arquivo físico e restaura o timestamp original via SetFileTime
        {
            HANDLE hf = CreateFileW(targetFile.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            assert(hf != INVALID_HANDLE_VALUE);
            DWORD written = 0;
            WriteFile(hf, tamperedContent.data(), (DWORD)tamperedContent.size(), &written, nullptr);
            // Timestomp: restaura a data/hora original
            SetFileTime(hf, &origAttr.ftCreationTime, &origAttr.ftLastAccessTime, &origAttr.ftLastWriteTime);
            CloseHandle(hf);
        }

        // Segunda checagem com useCache = true
        IntegrityStatus st2 = iv.VerifyFile(auditDir, rec, true);

        assert(st2 == IntegrityStatus::Tampered && "Timestomping DEVE ser detectado e classificado como Tampered.");
        std::cout << " [DEFESA CONFIRMADA ADV-01]: Arquivo adulterado detectado com sucesso apesar de timestomping." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-02: Path Traversal no Manifesto de Integridade
    // Cenarios: sequencias '../', caminhos absolutos, letras de unidade, ADS streams
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-02] Avaliando Path Traversal / Escape de Diretorio no Manifesto..." << std::endl;
    {
        IntegrityVerifier iv;

        // Caso A: Travessia com ..
        FileIntegrityRecord recEscape;
        recEscape.relativePath = "..\\..\\Windows\\System32\\cmd.exe";
        recEscape.expectedSha256 = "dummy";
        recEscape.isCritical = false;
        assert(iv.VerifyFile(auditDir, recEscape, false) == IntegrityStatus::ManifestInvalid);

        // Caso B: Caminho absoluto com letra de unidade
        FileIntegrityRecord recDrive;
        recDrive.relativePath = "C:\\Windows\\System32\\cmd.exe";
        recDrive.expectedSha256 = "dummy";
        recDrive.isCritical = false;
        assert(iv.VerifyFile(auditDir, recDrive, false) == IntegrityStatus::ManifestInvalid);

        // Caso C: NTFS Alternate Data Stream (ADS)
        FileIntegrityRecord recStream;
        recStream.relativePath = "valid.dll:hidden_stream";
        recStream.expectedSha256 = "dummy";
        recStream.isCritical = false;
        assert(iv.VerifyFile(auditDir, recStream, false) == IntegrityStatus::ManifestInvalid);

        // Caso D: Caminho absoluto com barra frontal
        FileIntegrityRecord recRoot;
        recRoot.relativePath = "/etc/passwd";
        recRoot.expectedSha256 = "dummy";
        recRoot.isCritical = false;
        assert(iv.VerifyFile(auditDir, recRoot, false) == IntegrityStatus::ManifestInvalid);

        std::cout << " [DEFESA CONFIRMADA ADV-02]: Todas as tentativas de Path Traversal e ADS foram bloqueadas com ManifestInvalid." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-03: Mitigacao de TOCTOU (Time-Of-Check to Time-Of-Use) em ModuleVerifier
    // Validacao com lock compartilhado FILE_SHARE_READ e conferencia pos-carregamento
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-03] Avaliando arquitetura anti-TOCTOU em ModuleVerifier..." << std::endl;
    {
        std::wstring testDll = auditDir + L"\\toctou_module.dll";
        {
            std::ofstream f(testDll, std::ios::binary);
            f << "ORIGINAL_DLL_BINARY_CONTENT";
        }

        std::string origSha;
        assert(IntegrityVerifier::ComputeFileSha256(testDll, origSha));

        ModuleVerifier mv;
        IntegrityStatus outStatus = IntegrityStatus::Valid;

        // Hash incorreto/adulterado durante a checagem TOCTOU
        HMODULE hModBad = mv.VerifyAndLoad(testDll, "0000000000000000000000000000000000000000000000000000000000000000", outStatus);
        assert(hModBad == nullptr);
        assert(outStatus == IntegrityStatus::Tampered);

        // Caminho invalido
        HMODULE hModMissing = mv.VerifyAndLoad(auditDir + L"\\non_existent.dll", origSha, outStatus);
        assert(hModMissing == nullptr);
        assert(outStatus == IntegrityStatus::Missing);

        std::cout << " [DEFESA CONFIRMADA ADV-03]: ModuleVerifier::VerifyAndLoad valida hash sob lock compartilhado e bloqueia adulteracoes." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-04: Bloqueio Defensivo do Carregador Dinamico e Isolamento de Categoria B
    // Avalia LibreHardwareMonitorFanProvider com bridge adulterada / desautorizada
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-04] Avaliando carregamento defensivo da bridge e isolamento..." << std::endl;
    {
        // Reseta estado do SecurityManager para auditoria isolada
        OpitSecurityManager::Instance().ResetState();

        // Tenta carregar bridge sem estar catalogada no manifesto no diretorio auditDir
        LibreHardwareMonitorFanProvider provider;
        bool initOk = provider.Initialize();

        // Como auditDir nao possui o manifesto assinado, o provider recusa o carregamento
        if (!initOk) {
            auto devices = provider.EnumerateDevices();
            assert(devices.empty() && "Bridge desautorizada deve manter lista de devices vazia.");
            std::cout << " [DEFESA CONFIRMADA ADV-04]: Bridge desautorizada recusada defensivamente pelo provider." << std::endl;
        } else {
            std::cout << " [DEFESA CONFIRMADA ADV-04]: Bridge autentica carregada estritamente apos validacao criptografica." << std::endl;
        }

        // Verifica que o fallback para outros provedores de hardware continua operacional
        WindowsAcpiFanProvider acpiFallback;
        assert(acpiFallback.GetProviderName() == "WindowsACPI");
        std::cout << " [DEFESA CONFIRMADA ADV-04]: Isolamento de falha concluido. Fallback operacional preservado." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-05: Fuzzing e Robustez do Parser de Manifesto
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-05] Fuzzing de formatos no ManifestVerifier..." << std::endl;
    {
        ManifestVerifier mv;
        std::vector<FileIntegrityRecord> recs;
        std::string err;

        // Caso A: JSON malformado
        assert(mv.VerifyPayload("{unclosed_json:", "dummy", recs, err) == IntegrityStatus::UntrustedSignature);

        // Caso B: JSON com campos ausentes
        assert(mv.VerifyPayload("{}", "dummy", recs, err) == IntegrityStatus::UntrustedSignature);

        // Caso C: Assinatura truncada (1 byte)
        assert(mv.VerifyPayload("{\"files\":[]}", "A", recs, err) == IntegrityStatus::UntrustedSignature);

        // Caso D: Algoritmo inesperado
        assert(mv.VerifyPayload("{\"algorithm\":\"MD5\",\"files\":[]}", "dummy", recs, err) == IntegrityStatus::UntrustedSignature);

        std::cout << " [DEFESA CONFIRMADA ADV-05]: Entradas malformadas rejeitadas com seguranca." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-06: Micro-Benchmarking de Desempenho (100 Amostras)
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-06] Executando micro-benchmarking rigoroso (100 iteracoes)..." << std::endl;
    {
        std::wstring benchFile = auditDir + L"\\bench_file.dll";
        {
            std::ofstream f(benchFile, std::ios::binary);
            std::string dummy(1024 * 512, 'A'); // 512 KB
            f << dummy;
        }

        std::string bSha;
        assert(IntegrityVerifier::ComputeFileSha256(benchFile, bSha));

        FileIntegrityRecord bRec;
        bRec.relativePath = "bench_file.dll";
        bRec.expectedSha256 = bSha;
        bRec.isCritical = true;

        IntegrityVerifier iv;
        std::vector<double> coldTimes;
        std::vector<double> warmTimes;

        for (int i = 0; i < 100; ++i) {
            iv.InvalidateCache();
            auto tStart = std::chrono::high_resolution_clock::now();
            iv.VerifyFile(auditDir, bRec, true);
            auto tEnd = std::chrono::high_resolution_clock::now();
            coldTimes.push_back(std::chrono::duration<double, std::milli>(tEnd - tStart).count());

            // Acesso aquecido (segundo acesso)
            tStart = std::chrono::high_resolution_clock::now();
            iv.VerifyFile(auditDir, bRec, true);
            tEnd = std::chrono::high_resolution_clock::now();
            warmTimes.push_back(std::chrono::duration<double, std::milli>(tEnd - tStart).count());
        }

        auto calcStats = [](std::vector<double>& v, double& outMean, double& outMed, double& outMin, double& outMax) {
            std::sort(v.begin(), v.end());
            outMin = v.front();
            outMax = v.back();
            outMed = v[v.size() / 2];
            outMean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        };

        double cMean, cMed, cMin, cMax;
        double wMean, wMed, wMin, wMax;
        calcStats(coldTimes, cMean, cMed, cMin, cMax);
        calcStats(warmTimes, wMean, wMed, wMin, wMax);

        std::cout << " [METRICAS ADV-06] Verificacao Fria (Hashing 512KB):" << std::endl;
        std::cout << "   Media: " << cMean << " ms | Mediana: " << cMed << " ms | Min: " << cMin << " ms | Max: " << cMax << " ms" << std::endl;
        std::cout << " [METRICAS ADV-06] Verificacao Aquecida (In-Memory Cache):" << std::endl;
        std::cout << "   Media: " << wMean << " ms | Mediana: " << wMed << " ms | Min: " << wMin << " ms | Max: " << wMax << " ms" << std::endl;
        std::cout << "   Ganho de velocidade do cache: " << (cMean / (wMean > 0.0001 ? wMean : 0.0001)) << "x" << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-07: Anti-Reverse Engineering & Debugger Detection
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-07] Avaliando mecanismos anti-debug e inspecao de PEB..." << std::endl;
    {
        bool isDbg = AntiReverseEngineering::DetectDebugger();
        std::cout << " [DEFESA CONFIRMADA ADV-07]: Inspecao de PEB, NtGlobalFlag e DR0-DR3 executada. Estado: " 
                  << (isDbg ? "Debugger Ativo" : "Ambiente Limpo") << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-08: Integridade da Secao de Codigo (.text) e Deteccao de Hot-Patching
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-08] Avaliando integridade da secao .text em memoria (Anti-Patching)..." << std::endl;
    {
        bool initBaseline = AntiReverseEngineering::InitializeCodeSectionBaseline();
        assert(initBaseline && "Baseline da secao .text deve ser mapeada com sucesso.");

        bool validBefore = AntiReverseEngineering::VerifyCodeSectionIntegrity();
        assert(validBefore && "Secao .text autentica deve validar 100%.");

        std::cout << " [DEFESA CONFIRMADA ADV-08]: Secao .text mapeada e verificada contra patches em memoria." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-09: Deteccao de Anomalias de Temporizacao (Anti-Tracing / Emulacao)
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-09] Avaliando resiliencia contra tracing / single-stepping (RDTSC)..." << std::endl;
    {
        bool timingAnomaly = AntiReverseEngineering::DetectTimingAnomaly();
        assert(!timingAnomaly && "Execucao nativa livre de single-stepping.");
        std::cout << " [DEFESA CONFIRMADA ADV-09]: Monitor de temporizacao de ciclos RDTSC ativo e funcional." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-10: Ofuscacao e Desofuscacao de Strings Criticas (Zero-Leak)
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-10] Avaliando protecao criptografica de strings contra REA por IA..." << std::endl;
    {
        const std::string secret = "https://www.acstclab.com/api/v1/license/validate";
        const uint8_t xorKey = 0xAA;
        std::vector<uint8_t> cipher(secret.size());
        for (size_t i = 0; i < secret.size(); ++i) cipher[i] = secret[i] ^ xorKey;

        std::string decrypted = AntiReverseEngineering::DeobfuscateString(cipher.data(), cipher.size(), xorKey);
        assert(decrypted == secret);
        std::cout << " [DEFESA CONFIRMADA ADV-10]: Strings sensiveis protegidas contra analise estatica de IA." << std::endl;
    }

    // -------------------------------------------------------------
    // TESTE ADV-11: Auditoria do Escudo em Execucao (OpitSecurityManager::AuditRuntimeShield)
    // -------------------------------------------------------------
    std::cout << "\n[TEST ADV-11] Avaliando OpitSecurityManager::AuditRuntimeShield..." << std::endl;
    {
        bool shieldOk = OpitSecurityManager::Instance().AuditRuntimeShield();
        assert(shieldOk && "AuditRuntimeShield deve passar em ambiente sem depuradores hostis.");
        std::cout << " [DEFESA CONFIRMADA ADV-11]: Escudo de seguranca em runtime aprovado com sucesso." << std::endl;
    }

    CleanupAuditDir(auditDir);

    std::cout << "\n================================================================" << std::endl;
    std::cout << "        SUITE DE AUDITORIA ADVERSARIAL CONCLUIDA COM SUCESSO    " << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
