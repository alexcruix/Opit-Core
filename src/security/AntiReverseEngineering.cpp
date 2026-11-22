#include "AntiReverseEngineering.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

namespace opitcore {
namespace security {

uint64_t AntiReverseEngineering::s_baselineTextChecksum = 0;
const uint8_t* AntiReverseEngineering::s_textSectionAddr = nullptr;
size_t AntiReverseEngineering::s_textSectionSize = 0;
bool AntiReverseEngineering::s_baselineInitialized = false;

uint64_t AntiReverseEngineering::ComputeSectionChecksum(const uint8_t* data, size_t size) {
    if (!data || size == 0) return 0;
    // Algoritmo FNV-1a 64-bit ultrarrápido
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool CheckPebDebugFlags() {
#if defined(_M_X64) || defined(__x86_64__)
    auto* pPeb = reinterpret_cast<const BYTE*>(__readgsqword(0x60));
    if (pPeb) {
        if (pPeb[2] != 0) return true; // BeingDebugged byte
        DWORD ntGlobalFlag = *reinterpret_cast<const DWORD*>(pPeb + 0xBC);
        // Flags configuradas por debuggers: FLG_HEAP_ENABLE_TAIL_CHECK (0x10) | FLG_HEAP_ENABLE_FREE_CHECK (0x20) | FLG_HEAP_VALIDATE_PARAMETERS (0x40)
        if ((ntGlobalFlag & 0x70) == 0x70) return true;
    }
#elif defined(_M_IX86) || defined(__i386__)
    auto* pPeb = reinterpret_cast<const BYTE*>(__readfsdword(0x30));
    if (pPeb) {
        if (pPeb[2] != 0) return true;
        DWORD ntGlobalFlag = *reinterpret_cast<const DWORD*>(pPeb + 0x68);
        if ((ntGlobalFlag & 0x70) == 0x70) return true;
    }
#endif
    return false;
}

static bool CheckHardwareDebugRegisters() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE hThread = GetCurrentThread();
    if (GetThreadContext(hThread, &ctx)) {
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0) {
            return true; // Hardware breakpoint ativo!
        }
    }
    return false;
}

bool AntiReverseEngineering::DetectDebugger() {
    if (IsDebuggerPresent()) return true;

    BOOL remote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote) {
        return true;
    }

    if (CheckPebDebugFlags()) {
        return true;
    }

    if (CheckHardwareDebugRegisters()) {
        return true;
    }

    return false;
}

bool AntiReverseEngineering::DetectTimingAnomaly() {
    unsigned __int64 start = __rdtsc();
    volatile int dummy = 0;
    for (int i = 0; i < 200; ++i) {
        dummy += (i * 7) ^ 0x3F;
    }
    unsigned __int64 end = __rdtsc();

    // Se um bloco de 200 operacoes basicas levar mais de 500.000 ciclos de clock,
    // ha forte indicio de stepping manual em debugger ou analise estendida em sandbox/hypervisor.
    return (end - start) > 500000;
}

bool AntiReverseEngineering::InitializeCodeSectionBaseline() {
    HMODULE hExe = GetModuleHandleW(nullptr);
    if (!hExe) return false;

    auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hExe);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<BYTE*>(hExe) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

    auto* section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            s_textSectionAddr = reinterpret_cast<const uint8_t*>(hExe) + section->VirtualAddress;
            s_textSectionSize = section->Misc.VirtualSize;
            s_baselineTextChecksum = ComputeSectionChecksum(s_textSectionAddr, s_textSectionSize);
            s_baselineInitialized = true;
            return true;
        }
    }
    return false;
}

bool AntiReverseEngineering::VerifyCodeSectionIntegrity() {
    if (!s_baselineInitialized || !s_textSectionAddr || s_textSectionSize == 0) {
        return InitializeCodeSectionBaseline();
    }

    uint64_t current = ComputeSectionChecksum(s_textSectionAddr, s_textSectionSize);
    return current == s_baselineTextChecksum;
}

std::string AntiReverseEngineering::DeobfuscateString(const uint8_t* cipher, size_t len, uint8_t key) {
    if (!cipher || len == 0) return {};
    std::string result;
    result.resize(len);
    for (size_t i = 0; i < len; ++i) {
        result[i] = static_cast<char>(cipher[i] ^ key);
    }
    return result;
}

} // namespace security
} // namespace opitcore
