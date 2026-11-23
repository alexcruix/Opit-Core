// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace opitcore {
namespace security {

class AntiReverseEngineering {
public:
    /**
     * Detecta se o processo está sob inspeção de depuradores ativos
     * (x64dbg, IDA Pro, Visual Studio, Cheat Engine, etc.).
     * Vetores: IsDebuggerPresent, CheckRemoteDebuggerPresent, PEB BeingDebugged / NtGlobalFlag,
     * e registradores de hardware (DR0-DR3).
     */
    static bool DetectDebugger();

    /**
     * Detecta anomalias de temporização (Single-Stepping / Tracing interativo ou sandbox lenta).
     * Mede ciclos de relógio RDTSC em bloco matemático constante.
     */
    static bool DetectTimingAnomaly();

    /**
     * Mapeia a seção .text do módulo executável principal e grava a assinatura de baseline.
     */
    static bool InitializeCodeSectionBaseline();

    /**
     * Compara a seção .text em memória contra a baseline gravada no início da execução.
     * Retorna true se a memória estiver íntegra, false se detectou patch de bytes,
     * breakpoints de software (0xCC), hooks ou adulterações.
     */
    static bool VerifyCodeSectionIntegrity();

    /**
     * Desofusca strings protegidas via XOR efêmero em tempo de execução.
     */
    static std::string DeobfuscateString(const uint8_t* cipher, size_t len, uint8_t key);

private:
    static uint64_t ComputeSectionChecksum(const uint8_t* data, size_t size);

    static uint64_t s_baselineTextChecksum;
    static const uint8_t* s_textSectionAddr;
    static size_t s_textSectionSize;
    static bool s_baselineInitialized;
};

} // namespace security
} // namespace opitcore
