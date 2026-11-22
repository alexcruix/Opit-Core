#pragma once
#include "utils/Types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <windows.h> // DWORD no SetPriority (erro de OpenProcess/SetPriorityClass)

namespace opitcore {

enum class ProcessWindowState {
    Maximized,
    Normal,
    Minimized,
    Background,
    SystemTray,
    NoVisibleWindow
};

inline const char* ProcessWindowStateToString(ProcessWindowState state) {
    switch (state) {
        case ProcessWindowState::Maximized: return "Maximizada";
        case ProcessWindowState::Normal: return "Janela Normal";
        case ProcessWindowState::Minimized: return "Minimizada";
        case ProcessWindowState::Background: return "Segundo Plano";
        case ProcessWindowState::SystemTray: return "Area de Notificacao (Tray)";
        default: return "Sem Janela Visivel";
    }
}

class ProcessManager {
public:
    static std::vector<ProcessItem> EnumerateProcesses();
    static bool IsCriticalProcess(const std::string& processName);
    static bool SetPriority(uint32_t pid, uint32_t priorityClass, DWORD* lastErrorOut = nullptr);
    // Handle ja aberto (PROCESS_QUERY_LIMITED_INFORMATION): diz se o kernel protege o processo (PPL/PP),
    // caso em que nenhuma escrita de prioridade e possivel - nem admin, nem SYSTEM, nem certificado.
    static bool IsProtectedHandle(HANDLE hProcess);
    static bool SetAffinity(uint32_t pid, uint64_t affinityMask);
    static bool SetEcoQoS(uint32_t pid, bool enable);
    static bool Terminate(uint32_t pid);
    static bool OptimizeMemory(uint32_t pid);
    static std::string PriorityClassToString(uint32_t priorityClass);
    static uint32_t StringToPriorityClass(const std::string& name);

    // Deteccao de estado de janela e tarefas ativas
    static ProcessWindowState DetectWindowState(uint32_t pid);
    static bool HasActiveAudioOrMedia(uint32_t pid, const std::string& procName);

    // Uma unica passada de EnumWindows classifica todos os PIDs de uma vez. O caminho antigo chamava
    // EnumWindows um por processo (177 processos = 177 varreduras completas da estacao de janelas),
    // que era o item mais caro do ciclo e por isso o auto de processos so rodava a cada 5 s.
    static std::unordered_map<uint32_t, ProcessWindowState> BuildWindowStateMap();

    // Binario dentro de \Windows (System32 / SysWOW64 / WinSxS / raiz) = peca do proprio Windows.
    static bool IsWindowsSystemBinary(const std::string& imagePath);

    // PIDs com sessao de audio WASAPI audivel agora (renderizacao ou captura, nao mutada). A lista por
    // nome sozinha nao bastava: proteg ia 14 aplicativos conhecidos e deixava passar o resto tocando som.
    // Falhou qualquer passo do WASAPI, devolve conjunto vazio (nunca bloqueia a otimizacao por engano).
    static std::unordered_set<uint32_t> ProcessPidsWithActiveAudio();

    // Ordem das classes (Ociosa = 0 ... Tempo Real = 5) para comparar e aplicar tetos de seguranca.
    static int PriorityRank(uint32_t priorityClass);
    static uint32_t PriorityClassFromRank(int rank);
};

} // namespace opitcore
