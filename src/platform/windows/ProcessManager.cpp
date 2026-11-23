// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "ProcessManager.h"
#include "utils/StringUtils.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <mutex>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ole32.lib")

namespace opitcore {

static const std::unordered_set<std::string> kCriticalProcesses = {
    "system", "system idle process", "registry", "smss.exe", "csrss.exe",
    "wininit.exe", "services.exe", "lsass.exe", "svchost.exe", "fontdrvhost.exe",
    "winlogon.exe", "dwm.exe", "sihost.exe", "taskhostw.exe", "explorer.exe",
    "ctfmon.exe", "runtimebroker.exe", "wmiprvse.exe", "audiodg.exe",
    "searchindexer.exe", "searchapp.exe", "startmenuexperiencehost.exe",
    "shellexperiencehost.exe", "applicationframehost.exe", "conhost.exe",
    "dllhost.exe", "spoolsv.exe", "nvdisplay.container.exe", "nvcontainer.exe",
    "securityhealthservice.exe", "msmpeng.exe", "opit-core.exe"
};

bool ProcessManager::IsCriticalProcess(const std::string& processName) {
    std::string lower = StringUtils::ToLower(StringUtils::Trim(processName));
    return kCriticalProcesses.find(lower) != kCriticalProcesses.end();
}

std::string ProcessManager::PriorityClassToString(uint32_t priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return "Ociosa (Baixa)";
        case BELOW_NORMAL_PRIORITY_CLASS: return "Abaixo do Normal";
        case NORMAL_PRIORITY_CLASS: return "Normal";
        case ABOVE_NORMAL_PRIORITY_CLASS: return "Acima do Normal";
        case HIGH_PRIORITY_CLASS: return "Alta";
        case REALTIME_PRIORITY_CLASS: return "Tempo Real";
        default: return "Normal";
    }
}

uint32_t ProcessManager::StringToPriorityClass(const std::string& name) {
    if (name == "Ociosa (Baixa)" || name == "Idle") return IDLE_PRIORITY_CLASS;
    if (name == "Abaixo do Normal" || name == "BelowNormal") return BELOW_NORMAL_PRIORITY_CLASS;
    if (name == "Acima do Normal" || name == "AboveNormal") return ABOVE_NORMAL_PRIORITY_CLASS;
    if (name == "Alta" || name == "High") return HIGH_PRIORITY_CLASS;
    return NORMAL_PRIORITY_CLASS;
}

namespace {

// ProcessProtectionInformation = 61 no NtQueryInformationProcess. A classe 89 (ProcessProtectionLevel)
// devolve STATUS_INVALID_INFO_CLASS neste Windows, medido a 2026-11-20. O primeiro byte vale 0x31/0x51
// nos PPL do Defender e 0x00 em dwm/explorer/svchost.
using NtQueryProcessInfoFn = LONG(WINAPI *)(HANDLE, INT, PVOID, ULONG, PULONG);

NtQueryProcessInfoFn NtQueryProcessInfo() {
    static NtQueryProcessInfoFn fn = []() -> NtQueryProcessInfoFn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return nullptr;
        return reinterpret_cast<NtQueryProcessInfoFn>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
    }();
    return fn;
}

} // namespace

bool ProcessManager::IsProtectedHandle(HANDLE hProcess) {
    const auto fn = NtQueryProcessInfo();
    if (!fn || !hProcess) return false;
    BYTE info[8] = {};
    ULONG returned = 0;
    return fn(hProcess, 61, info, static_cast<ULONG>(sizeof(info)), &returned) == 0 && returned >= 1 && info[0] != 0;
}

std::vector<ProcessItem> ProcessManager::EnumerateProcesses() {
    std::vector<ProcessItem> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W pe32{};
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            ProcessItem item;
            item.pid = pe32.th32ProcessID;
            item.name = StringUtils::ToUtf8(pe32.szExeFile);
            item.isSystemCritical = IsCriticalProcess(item.name);

            // Tentar obter memória e prioridade abrindo o processo
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProc) {
                // O campo cb e obrigatorio: GetProcessMemoryInfo recusa a estrutura zeroada (cb = 0) e
                // devolve FALSE. Era isso que deixava a coluna "Memoria (MB)" em 0.0 MB para todos os
                // 177 processos da aba, inclusive os do proprio usuario.
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    item.workingSetBytes = pmc.WorkingSetSize;
                }

                // Tempo de CPU acumulado (kernel + user): e a base medida das duas decisoes do auto de
                // processos - ociosidade (para rebaixar) e fome (para subir de volta).
                FILETIME ftCreation{}, ftExit{}, ftKernel{}, ftUser{};
                if (GetProcessTimes(hProc, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    ULARGE_INTEGER k{};
                    k.LowPart = ftKernel.dwLowDateTime;
                    k.HighPart = ftKernel.dwHighDateTime;
                    ULARGE_INTEGER u{};
                    u.LowPart = ftUser.dwLowDateTime;
                    u.HighPart = ftUser.dwHighDateTime;
                    item.cpuTime100ns = k.QuadPart + u.QuadPart;
                }

                WCHAR image[MAX_PATH] = {0};
                DWORD imageLen = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, image, &imageLen)) {
                    item.imagePath = StringUtils::ToUtf8(std::wstring(image, imageLen));
                }

                item.priorityClass = GetPriorityClass(hProc);
                item.priorityName = PriorityClassToString(item.priorityClass);
                // A pergunta sai do mesmo handle, por isso nao custa uma passada extra por ciclo. Medir
                // o nivel de protecao e o unico caminho que nao envelhece quando a Microsoft renomeia
                // os binarios de seguranca; uma lista de nomes envelheceria.
                item.protectedByWindows = IsProtectedHandle(hProc);

                DWORD_PTR processAffinity = 0;
                DWORD_PTR systemAffinity = 0;
                if (GetProcessAffinityMask(hProc, &processAffinity, &systemAffinity)) {
                    item.affinityMask = processAffinity;
                }
                CloseHandle(hProc);
            } else {
                item.priorityClass = NORMAL_PRIORITY_CLASS;
                item.priorityName = "Protegido/Sistema";
            }

            result.push_back(item);
        } while (Process32NextW(snapshot, &pe32));
    }

    CloseHandle(snapshot);
    return result;
}

static bool EnableProcessDebugPrivilege() {
    static bool s_attempted = false;
    static bool s_enabled = false;
    if (s_attempted) return s_enabled;
    s_attempted = true;

    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LUID luid{};
        if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
                s_enabled = (GetLastError() == ERROR_SUCCESS);
            }
        }
        CloseHandle(hToken);
    }
    return s_enabled;
}

bool ProcessManager::SetPriority(uint32_t pid, uint32_t priorityClass, DWORD* lastErrorOut) {
    if (lastErrorOut) *lastErrorOut = ERROR_SUCCESS;
    auto deny = [&](DWORD err) {
        if (lastErrorOut) *lastErrorOut = err;
        return false;
    };
    if (pid <= 4) return deny(ERROR_INVALID_PARAMETER);

    // Habilita privilégio de depuração/administração no token do processo para permitir alteração entre processos
    EnableProcessDebugPrivilege();

    // Proibir prioridade em tempo real por padrão por estabilidade do SO
    if (priorityClass == REALTIME_PRIORITY_CLASS) {
        return deny(ERROR_INVALID_PARAMETER);
    }

    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    }
    if (!hProc) return deny(GetLastError());

    // Verificar se não é processo crítico antes de alterar
    WCHAR exeName[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, exeName, &size)) {
        std::wstring ws(exeName);
        size_t pos = ws.find_last_of(L"\\/");
        std::wstring base = (pos != std::wstring::npos) ? ws.substr(pos + 1) : ws;
        if (IsCriticalProcess(StringUtils::ToUtf8(base))) {
            CloseHandle(hProc);
            return deny(ERROR_ACCESS_DENIED);
        }
    }

    BOOL ok = SetPriorityClass(hProc, priorityClass);
    if (!ok && lastErrorOut) *lastErrorOut = GetLastError();
    CloseHandle(hProc);
    return (ok != 0);
}

bool ProcessManager::SetAffinity(uint32_t pid, uint64_t affinityMask) {
    if (affinityMask == 0) return false;

    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    BOOL ok = SetProcessAffinityMask(hProc, (DWORD_PTR)affinityMask);
    CloseHandle(hProc);
    return (ok != 0);
}

#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE, *PPROCESS_POWER_THROTTLING_STATE;
#endif

#ifndef ProcessPowerThrottling
#define ProcessPowerThrottling ((PROCESS_INFORMATION_CLASS)4)
#endif

bool ProcessManager::SetEcoQoS(uint32_t pid, bool enable) {
    if (pid <= 4) return false;

    EnableProcessDebugPrivilege();

    typedef BOOL (WINAPI *pfnSetProcessInformation)(
        HANDLE hProcess,
        PROCESS_INFORMATION_CLASS ProcessInformationClass,
        LPVOID ProcessInformation,
        DWORD ProcessInformationSize
    );

    static auto s_pfnSetProcessInfo = reinterpret_cast<pfnSetProcessInformation>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"));

    if (!s_pfnSetProcessInfo) return false;

    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    }
    if (!hProc) return false;

    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = enable ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

    BOOL ok = s_pfnSetProcessInfo(hProc, ProcessPowerThrottling, &throttling, sizeof(throttling));
    CloseHandle(hProc);
    return (ok != 0);
}

bool ProcessManager::Terminate(uint32_t pid) {
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    WCHAR exeName[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, exeName, &size)) {
        std::wstring ws(exeName);
        size_t pos = ws.find_last_of(L"\\/");
        std::wstring base = (pos != std::wstring::npos) ? ws.substr(pos + 1) : ws;
        if (IsCriticalProcess(StringUtils::ToUtf8(base))) {
            CloseHandle(hProc);
            return false; // Nunca encerra processos vitais
        }
    }

    BOOL ok = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return (ok != 0);
}

bool ProcessManager::OptimizeMemory(uint32_t pid) {
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return false;
        BOOL ok = EmptyWorkingSet(hProc);
        CloseHandle(hProc);
        return (ok != 0);
    }

    // Iterar sobre processos do sistema e liberar memoria inativa dos processos de usuario
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        HANDLE hCurrent = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
        if (hCurrent) {
            EmptyWorkingSet(hCurrent);
            CloseHandle(hCurrent);
        }
        return true;
    }

    PROCESSENTRY32W pe32{};
    pe32.dwSize = sizeof(pe32);
    int optimizedCount = 0;

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            std::string name = StringUtils::ToUtf8(pe32.szExeFile);
            if (pe32.th32ProcessID <= 4 || IsCriticalProcess(name)) {
                continue; // Nao interfere com processos vitais do Windows
            }

            HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProc) {
                if (EmptyWorkingSet(hProc)) {
                    optimizedCount++;
                }
                CloseHandle(hProc);
            }
        } while (Process32NextW(snapshot, &pe32));
    }

    CloseHandle(snapshot);
    EmptyWorkingSet(GetCurrentProcess());
    return true;
}

struct PidWindowState {
    bool foreground = false;
    bool zoomed = false;
    bool onScreenNormal = false;
    bool minimized = false;
    bool offScreenOrTool = false;
};

struct WindowCollectCtx {
    HWND foregroundWnd = nullptr;
    LONG virtLeft = 0;
    LONG virtTop = 0;
    LONG virtRight = 0;
    LONG virtBottom = 0;
    std::unordered_map<DWORD, PidWindowState> byPid;
};

static void MergeWindowIntoCtx(WindowCollectCtx& ctx, HWND hwnd, DWORD pid, const RECT& rect,
                               bool iconic, bool foreground, bool zoomed) {
    if (pid == 0) return;
    PidWindowState& st = ctx.byPid[pid];

    if (foreground && !iconic) {
        st.foreground = true;
        st.zoomed = st.zoomed || zoomed;
        st.onScreenNormal = true;
        return;
    }
    if (iconic) {
        st.minimized = true;
        return;
    }

    const bool sized = (rect.right - rect.left) > 8 && (rect.bottom - rect.top) > 8;
    const bool onScreen = sized && rect.right > ctx.virtLeft && rect.bottom > ctx.virtTop &&
                          rect.left < ctx.virtRight && rect.top < ctx.virtBottom;
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));

    if (onScreen && !(exStyle & WS_EX_TOOLWINDOW)) {
        st.onScreenNormal = true;
    } else if (onScreen || (exStyle & WS_EX_TOOLWINDOW)) {
        // Janela visivel fora da area util (empurrada para a bandeja) ou janela de ferramenta:
        // e o comportamento classico de "minimizar para a area de notificacao".
        st.offScreenOrTool = true;
    }
}

struct ChildCollectCtx {
    WindowCollectCtx* owner;
    RECT parentRect;
    bool parentIconic;
    bool parentForeground;
    bool parentZoomed;
};

static BOOL CALLBACK ChildWindowCallback(HWND hwnd, LPARAM lParam) {
    ChildCollectCtx* cc = reinterpret_cast<ChildCollectCtx*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    // O filho recebe o retangulo e o estado de primeiro plano do quadro: e assim que o processo real de
    // um aplicativo UWP herda a visibilidade da janela que pertence ao ApplicationFrameHost.
    MergeWindowIntoCtx(*cc->owner, hwnd, pid, cc->parentRect, cc->parentIconic, cc->parentForeground, cc->parentZoomed);
    return TRUE;
}

static BOOL CALLBACK TopLevelWindowCallback(HWND hwnd, LPARAM lParam) {
    WindowCollectCtx* ctx = reinterpret_cast<WindowCollectCtx*>(lParam);
    if (IsWindowVisible(hwnd) == FALSE) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    // Uma janela minimizada continua com WS_VISIBLE; so IsIconic separa os dois casos.
    const bool iconic = IsIconic(hwnd) != FALSE;
    const bool foreground = (hwnd == ctx->foregroundWnd);
    const bool zoomed = foreground && (IsZoomed(hwnd) != FALSE);

    MergeWindowIntoCtx(*ctx, hwnd, pid, rc, iconic, foreground, zoomed);

    // Aplicativos UWP/WinUI nao tem janela de primeiro plano propria: o que esta na tela e o quadro do
    // ApplicationFrameHost, e a janela real (CoreWindow) e filha dela. Sem descer nos filhos, o
    // Calculos/Configuracoes/Store em primeiro plano era classificado como "sem janela" e rebaixado.
    ChildCollectCtx cc{ ctx, rc, iconic, foreground, zoomed };
    EnumChildWindows(hwnd, ChildWindowCallback, reinterpret_cast<LPARAM>(&cc));
    return TRUE;
}

std::unordered_map<uint32_t, ProcessWindowState> ProcessManager::BuildWindowStateMap() {
    WindowCollectCtx ctx;
    ctx.foregroundWnd = GetForegroundWindow();
    ctx.virtLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    ctx.virtTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    ctx.virtRight = ctx.virtLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    ctx.virtBottom = ctx.virtTop + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    EnumWindows(TopLevelWindowCallback, reinterpret_cast<LPARAM>(&ctx));

    std::unordered_map<uint32_t, ProcessWindowState> result;
    result.reserve(ctx.byPid.size());
    for (const auto& kv : ctx.byPid) {
        const PidWindowState& st = kv.second;
        ProcessWindowState state;
        // Precedencia: primeiro plano > visivel na tela > minimizada > bandeja > sem janela.
        if (st.foreground) {
            state = st.zoomed ? ProcessWindowState::Maximized : ProcessWindowState::Normal;
        } else if (st.onScreenNormal) {
            state = ProcessWindowState::Background;
        } else if (st.minimized) {
            state = ProcessWindowState::Minimized;
        } else if (st.offScreenOrTool) {
            state = ProcessWindowState::SystemTray;
        } else {
            state = ProcessWindowState::NoVisibleWindow;
        }
        result[kv.first] = state;
    }
    return result;
}

ProcessWindowState ProcessManager::DetectWindowState(uint32_t pid) {
    auto map = BuildWindowStateMap();
    auto it = map.find(pid);
    return (it != map.end()) ? it->second : ProcessWindowState::NoVisibleWindow;
}

bool ProcessManager::IsWindowsSystemBinary(const std::string& imagePath) {
    if (imagePath.empty()) return true; // Sem caminho nao ha como provar que e de terceiro: tratar como do Windows.
    std::string lower = StringUtils::ToLower(imagePath);
    if (lower.find("\\windows\\system32\\") != std::string::npos) return true;
    if (lower.find("\\windows\\syswow64\\") != std::string::npos) return true;
    if (lower.find("\\windows\\winsxs\\") != std::string::npos) return true;
    // Raiz do Windows (explorer.exe, wininit.exe) e tambem \Windows\ imprevistos.
    size_t pos = lower.find("\\windows\\");
    if (pos != std::string::npos) {
        size_t after = pos + std::string("\\windows\\").size();
        if (lower.find('\\', after) == std::string::npos) return true;
    }
    return false;
}

int ProcessManager::PriorityRank(uint32_t priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return 0;
        case BELOW_NORMAL_PRIORITY_CLASS: return 1;
        case NORMAL_PRIORITY_CLASS: return 2;
        case ABOVE_NORMAL_PRIORITY_CLASS: return 3;
        case HIGH_PRIORITY_CLASS: return 4;
        case REALTIME_PRIORITY_CLASS: return 5;
        default: return 2;
    }
}

uint32_t ProcessManager::PriorityClassFromRank(int rank) {
    if (rank <= 0) return IDLE_PRIORITY_CLASS;
    if (rank == 1) return BELOW_NORMAL_PRIORITY_CLASS;
    if (rank == 2) return NORMAL_PRIORITY_CLASS;
    if (rank == 3) return ABOVE_NORMAL_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS; // Tempo Real e proibido por estabilidade do SO (SetPriority recusa).
}

// Conjunto de PIDs com som audivel agora, recalculado no maximo a cada 3 s: a varredura WASAPI nao e
// barata e o auto de processos a consulta para cada processo do ciclo.
static std::unordered_set<uint32_t> g_audioPids;
static uint64_t g_audioPidsAgeMs = 0;
static bool g_audioPidsValid = false;
static std::mutex g_audioMutex;

static uint64_t NowSteadyMsLocal() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static std::unordered_set<uint32_t> CollectAudioPids() {
    std::unordered_set<uint32_t> pids;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ownInit = SUCCEEDED(hrInit); // S_FALSE = ja havia STA/MTA nesta thread: nao podemos desfazer.

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(hr) && enumerator) {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) && device) {
            IAudioSessionManager2* sessionMgr = nullptr;
            HRESULT act = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&sessionMgr));
            if (SUCCEEDED(act) && sessionMgr) {
                IAudioSessionEnumerator* sessionEnum = nullptr;
                if (SUCCEEDED(sessionMgr->GetSessionEnumerator(&sessionEnum)) && sessionEnum) {
                    int count = 0;
                    sessionEnum->GetCount(&count);
                    for (int i = 0; i < count; ++i) {
                        IAudioSessionControl* ctrl = nullptr;
                        if (FAILED(sessionEnum->GetSession(i, &ctrl)) || !ctrl) continue;
                        AudioSessionState state = AudioSessionStateExpired;
                        ctrl->GetState(&state);
                        IAudioSessionControl2* ctrl2 = nullptr;
                        if (SUCCEEDED(ctrl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                           reinterpret_cast<void**>(&ctrl2))) && ctrl2) {
                            DWORD pid = 0;
                            // AudioSessionStateActive = esta renderizando agora. Estado de mute nao e
                            // consultado de proposito: superproteger um fluxo calado custa um nivel de
                            // reducao, errar o contrario corta o som do que a pessoa esta ouvindo.
                            bool sounding = (state == AudioSessionStateActive);
                            if (SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid > 4 && sounding) {
                                pids.insert(pid);
                            }
                            ctrl2->Release();
                        }
                        ctrl->Release();
                    }
                    sessionEnum->Release();
                }
                sessionMgr->Release();
            }
            device->Release();
        }
        enumerator->Release();
    }
    if (ownInit) CoUninitialize();
    return pids;
}

std::unordered_set<uint32_t> ProcessManager::ProcessPidsWithActiveAudio() {
    const uint64_t now = NowSteadyMsLocal();
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (!g_audioPidsValid || (now - g_audioPidsAgeMs) > 3000ULL) {
        g_audioPids = CollectAudioPids();
        g_audioPidsAgeMs = now;
        g_audioPidsValid = true;
    }
    return g_audioPids;
}

bool ProcessManager::HasActiveAudioOrMedia(uint32_t pid, const std::string& procName) {
    std::string lower = StringUtils::ToLower(procName);
    // Aplicativos conhecidos de audio, streaming, comunicacao e reproducao que nunca devem sofrer throttling
    // agressivo - continuam na lista porque podem estar capturan do sem sessao de renderizacao ativa (OBS).
    static const std::unordered_set<std::string> kMediaApps = {
        "spotify.exe", "discord.exe", "obs64.exe", "obs32.exe", "vlc.exe",
        "audacity.exe", "steamwebhelper.exe", "teams.exe", "zoom.exe",
        "foobar2000.exe", "musicbee.exe", "itunes.exe", "slack.exe", "telegram.exe"
    };

    if (kMediaApps.find(lower) != kMediaApps.end()) return true;

    const auto audio = ProcessPidsWithActiveAudio();
    return audio.find(pid) != audio.end();
}

} // namespace opitcore
