#include "core/CoreRuntime.h"
#include "ui/MainWindow.h"
#include "security/OpitSecurityManager.h"
#include "security/AntiReverseEngineering.h"
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <wchar.h>
#include <csignal>
#include <exception>
#include <stdlib.h>

// temporario: captura de morte silenciosa (VEH + minidump) — remover apos diagnostico
namespace opitcrashlog {
// CONTEXT nao tem o mesmo nome de registrador entre 32 e 64 bits (Eip/Rip, Esp/Rsp). Os macros
// abaixo dao ao mesmo codigo de crash log um endereco valido nas duas arquiteturas.
#if defined(_WIN64)
#define OPIT_CTX_IP(ctx) (static_cast<uintptr_t>((ctx)->Rip))
#define OPIT_CTX_SP(ctx) (static_cast<uintptr_t>((ctx)->Rsp))
#else
#define OPIT_CTX_IP(ctx) (static_cast<uintptr_t>((ctx)->Eip))
#define OPIT_CTX_SP(ctx) (static_cast<uintptr_t>((ctx)->Esp))
#endif
// trava de re-entrada: handlers rodam em stacks exaustas; sem ela o proprio logger
// recursa ate novo stack overflow (medido 2026-11-18: SO dentro de Backtrace/WriteLine/swprintf_s)
static volatile LONG g_crashBusy = 0;
struct CrashBusyGuard {
    bool owned;
    CrashBusyGuard() : owned(InterlockedCompareExchange(&g_crashBusy, 1, 0) == 0) {}
    ~CrashBusyGuard() { if (owned) InterlockedExchange(&g_crashBusy, 0); }
};
static void WriteLine(const wchar_t* line) {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 40) return;
    wcscat_s(path, MAX_PATH, L"\\OpitCore\\opit_crash.log");
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamped[640];
    swprintf_s(stamped, L"%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
               GetCurrentThreadId(), line);
    DWORD written = 0;
    WriteFile(h, stamped, static_cast<DWORD>(wcslen(stamped) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}

static LONG WINAPI VectoredLogger(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0xC00000FD) {
        CrashBusyGuard g;
        if (!g.owned) return EXCEPTION_CONTINUE_SEARCH;
        // temporario: escaneia a stack da thread em overflow por return-addresses do exe — remover
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t step = sizeof(uintptr_t);
        uintptr_t p = (OPIT_CTX_SP(ep->ContextRecord) + 0x1FFF) & ~uintptr_t(0xFFF);
        wchar_t line[640];
        int off = swprintf_s(line, L"STACK-OVERFLOW tid=%lu rip=exe+%#llx rsp=%p frames:",
                             GetCurrentThreadId(),
                             static_cast<unsigned long long>(OPIT_CTX_IP(ep->ContextRecord) - base),
                             reinterpret_cast<void*>(OPIT_CTX_SP(ep->ContextRecord)));
        int found = 0;
        const uintptr_t cap = p + 0x100000;
        while (p < cap && found < 36) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi)) ||
                mbi.State != MEM_COMMIT) {
                break;
            }
            const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            for (; p + step <= end && p < cap && found < 36; p += step) {
                const uintptr_t v = *reinterpret_cast<uintptr_t*>(p);
                if (v > base && v < base + 0x4000000) {
                    off += swprintf_s(line + off, static_cast<size_t>(640 - off), L" +%#llx",
                                      static_cast<unsigned long long>(v - base));
                    ++found;
                }
            }
        }
        WriteLine(line);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code == 0x80000003 || (code >> 16) == 0x4000) {
        return EXCEPTION_CONTINUE_SEARCH; // ruido benigno: breakpoint, notificacao
    }
    const void* extra = (ep->ExceptionRecord->NumberParameters >= 2)
                            ? reinterpret_cast<const void*>(ep->ExceptionRecord->ExceptionInformation[1])
                            : nullptr;
    wchar_t line[220];
    swprintf_s(line, L"first-chance code=0x%08X rip=0x%p addr=0x%p flags=0x%08lX",
               code, reinterpret_cast<void*>(OPIT_CTX_IP(ep->ContextRecord)), extra,
               ep->ExceptionRecord->ExceptionFlags);
    WriteLine(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI LastChanceDump(PEXCEPTION_POINTERS ep) {
    CrashBusyGuard g;
    if (!g.owned) return EXCEPTION_CONTINUE_SEARCH;
    wchar_t line[160];
    swprintf_s(line, L"LAST-CHANCE code=0x%08X rip=0x%p — escrevendo minidump",
               ep->ExceptionRecord->ExceptionCode,
               reinterpret_cast<void*>(OPIT_CTX_IP(ep->ContextRecord)));
    WriteLine(line);    HMODULE dbg = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbg) {
        typedef BOOL(WINAPI * MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                                   PMINIDUMP_EXCEPTION_INFORMATION,
                                                   PMINIDUMP_USER_STREAM_INFORMATION,
                                                   PMINIDUMP_CALLBACK_INFORMATION);
        auto write = reinterpret_cast<MiniDumpWriteDump_t>(GetProcAddress(dbg, "MiniDumpWriteDump"));
        if (write) {
            wchar_t path[MAX_PATH];
            DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
            if (n != 0 && n < MAX_PATH - 40) {
                wcscat_s(path, MAX_PATH, L"\\OpitCore\\opit_crash.dmp");
                HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), ep, FALSE };
                    write(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory),
                          &mei, nullptr, nullptr);
                    CloseHandle(f);
                    WriteLine(L"minidump escrito");
                }
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace opitcrashlog

// temporario: captura das rotas de abort()/terminate (fail-fast silencioso) — remover apos diagnostico
namespace opitcrashlog {
static void Backtrace(const char* tag) {
    CrashBusyGuard g;
    if (!g.owned) return;
    void* frames[32];
    WORD n = CaptureStackBackTrace(1, 32, frames, nullptr);
    wchar_t line[2048];
    int off = swprintf_s(line, L"%hs tid=%lu frames=%u:", tag, GetCurrentThreadId(), static_cast<unsigned>(n));
    for (WORD i = 0; i < n && off < 1900; ++i) {
        HMODULE mod = nullptr;
        wchar_t modName[64] = L"?";
        wchar_t* base = modName;
        DWORD rva = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[i]), &mod) && mod) {
            GetModuleFileNameW(mod, modName, 64);
            for (wchar_t* c = modName; *c; ++c) {
                if (*c == L'\\') base = c + 1;
            }
            uintptr_t modBase = reinterpret_cast<uintptr_t>(mod);
            rva = static_cast<DWORD>(reinterpret_cast<uintptr_t>(frames[i]) - modBase);
        }
        off += swprintf_s(line + off, static_cast<size_t>(2048 - off), L" %p(%s+%#lx)",
                          frames[i], base, rva);
    }
    WriteLine(line);
}
static void __cdecl SigAbortHandler(int) { Backtrace("SIGABRT"); }
static void InvalidParamHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    Backtrace("CRT-invalid-param");
}
static void PurecallHandler() { Backtrace("purecall"); }
static void TerminateHandler() {
    Backtrace("std::terminate");
    {
        CrashBusyGuard g;
        if (g.owned) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                wchar_t line[600];
                int conv = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, line, 300);
                if (conv <= 0) line[0] = L'\0';
                wchar_t out[640];
                swprintf_s(out, L"terminate-exception type=std::exception what=%s", line);
                WriteLine(out);
            } catch (...) {
                WriteLine(L"terminate-exception tipo desconhecido (nao-std)");
            }
        }
    }
    std::abort();
}
} // namespace opitcrashlog

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // temporario: registro da captura de morte silenciosa — remover apos diagnostico
    AddVectoredExceptionHandler(1, &opitcrashlog::VectoredLogger);
    SetUnhandledExceptionFilter(&opitcrashlog::LastChanceDump);
    signal(SIGABRT, opitcrashlog::SigAbortHandler);
    _set_invalid_parameter_handler(opitcrashlog::InvalidParamHandler);
    _set_purecall_handler(opitcrashlog::PurecallHandler);
    std::set_terminate(opitcrashlog::TerminateHandler);
    opitcrashlog::WriteLine(L"capture ativo");

    // 0. Identidade do aplicativo na Barra de Tarefas do Windows (Shell)
    typedef HRESULT (WINAPI *SetAppIdFn)(PCWSTR);
    HMODULE hShell = GetModuleHandleW(L"shell32.dll");
    if (!hShell) hShell = LoadLibraryW(L"shell32.dll");
    if (hShell) {
        auto pfn = (SetAppIdFn)GetProcAddress(hShell, "SetCurrentProcessExplicitAppUserModelID");
        if (pfn) pfn(L"ACSTCLab.OpitCore.App");
    }

    // 1. Defesa ativa contra DLL Search Order Hijacking
    opitcore::security::ModuleVerifier::EnforceSecureDllSearchOrder();

    // 1.1. Baseline da .text o mais cedo possivel: se fosse tirada na primeira auditoria
    // (lazily), um patch de memoria anterior viraria a propria referencia. Eager aqui fecha
    // essa janela. Nota honesta: o checksum e da memoria carregada (com relocations de ASLR
    // aplicadas), nao do arquivo em disco — comparar bytes de disco com memoria daria falso
    // positivo; a copia em disco ja e coberta pelo manifesto assinado abaixo.
    opitcore::security::AntiReverseEngineering::InitializeCodeSectionBaseline();

    // 2. Garantir instância única
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"OpitCoreSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"OpitCoreMainWindowClass", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    // 3. Verificação defensiva de integridade e assinatura do manifesto de binários
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t slashPos = exeDir.find_last_of(L"\\/");
    std::wstring appDir = (slashPos != std::wstring::npos) ? exeDir.substr(0, slashPos) : L".";

    // 3. Verificação de integridade e assinatura do manifesto de binários: o resultado e
    // registrado no log de seguranca (VerifyInstallationIntegrity ja suma o que encontrou) e
    // nao barra a abertura do programa — donationware: instalou, usou.
    opitcore::security::OpitSecurityManager::Instance().Initialize(appDir);
    opitcore::security::OpitSecurityManager::Instance().VerifyInstallationIntegrity();

    // Inicializar o núcleo operacional e todos os subsistemas
    if (!opitcore::CoreRuntime::Instance().Initialize()) {
        MessageBoxW(nullptr, L"Falha crítica na inicialização dos subsistemas do Opit-Core.", L"Opit-Core Erro", MB_OK | MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    // Criar e apresentar a interface de controle do usuário
    if (!opitcore::MainWindow::Instance().Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Falha ao criar a interface gráfica do Opit-Core.", L"Opit-Core Erro", MB_OK | MB_ICONERROR);
        opitcore::CoreRuntime::Instance().Shutdown();
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    // Loop de mensagens Win32
    int exitCode = opitcore::MainWindow::Instance().RunMessageLoop();

    // Encerramento ordenado e restauração das configurações
    opitcore::CoreRuntime::Instance().Shutdown();

    if (mutex) {
        CloseHandle(mutex);
    }

    return exitCode;
}
