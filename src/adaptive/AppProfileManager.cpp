#include "AppProfileManager.h"
#include "utils/StringUtils.h"
#include "core/Logger.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace opitcore {

AppProfileManager& AppProfileManager::Instance() {
    static AppProfileManager instance;
    return instance;
}

std::string AppProfileManager::ComputeFileIdentityHash(const std::wstring& path, uint64_t fileSize, uint64_t lastWrite) {
    // 64-bit FNV-1a hash combinando caminho, tamanho e timestamp de modificação
    uint64_t hash = 14695981039346656037ULL;
    std::string sPath = StringUtils::ToUtf8(path);
    for (char c : sPath) {
        hash ^= (uint64_t)(unsigned char)tolower(c);
        hash *= 1099511628211ULL;
    }
    hash ^= fileSize;
    hash *= 1099511628211ULL;
    hash ^= lastWrite;
    hash *= 1099511628211ULL;

    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

std::string AppProfileManager::ComputeAppId(const std::string& normalizedPath, const std::string& processName, const std::string& hash) {
    if (!hash.empty() && hash != "0000000000000000") {
        return processName + "_" + hash.substr(0, 8);
    }
    uint64_t fnv = 14695981039346656037ULL;
    for (char c : normalizedPath) {
        fnv ^= (uint64_t)(unsigned char)tolower(c);
        fnv *= 1099511628211ULL;
    }
    std::stringstream ss;
    ss << processName << "_" << std::hex << std::setw(8) << std::setfill('0') << (fnv & 0xFFFFFFFF);
    return ss.str();
}

AppIdentity AppProfileManager::IdentifyProcess(uint32_t pid) {
    AppIdentity id;
    id.pid = pid;
    if (pid <= 4) return id;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return id;

    wchar_t pathBuf[MAX_PATH * 2]{};
    DWORD pathLen = sizeof(pathBuf) / sizeof(wchar_t);
    if (QueryFullProcessImageNameW(hProc, 0, pathBuf, &pathLen)) {
        std::wstring wPath(pathBuf);
        id.normalizedPath = StringUtils::ToUtf8(wPath);
        std::replace(id.normalizedPath.begin(), id.normalizedPath.end(), '\\', '/');

        // Extrai nome do processo
        size_t lastSlash = id.normalizedPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            id.processName = id.normalizedPath.substr(lastSlash + 1);
        } else {
            id.processName = id.normalizedPath;
        }

        // Metadados do arquivo
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER uliSize;
            uliSize.LowPart = fad.nFileSizeLow;
            uliSize.HighPart = fad.nFileSizeHigh;
            id.fileSizeBytes = uliSize.QuadPart;

            ULARGE_INTEGER uliTime;
            uliTime.LowPart = fad.ftLastWriteTime.dwLowDateTime;
            uliTime.HighPart = fad.ftLastWriteTime.dwHighDateTime;
            id.lastWriteTime = uliTime.QuadPart;

            id.fileHash = ComputeFileIdentityHash(wPath, id.fileSizeBytes, id.lastWriteTime);
        }
    }
    CloseHandle(hProc);
    return id;
}

AppIdentity AppProfileManager::IdentifyForegroundWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return AppIdentity{};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid <= 4) return AppIdentity{};

    AppIdentity id = IdentifyProcess(pid);

    wchar_t titleBuf[256]{};
    if (GetWindowTextW(hwnd, titleBuf, 256) > 0) {
        id.windowTitle = StringUtils::ToUtf8(titleBuf);
    }
    return id;
}

AppProfile AppProfileManager::GetOrCreateProfile(const AppIdentity& app) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string appId = ComputeAppId(app.normalizedPath, app.processName, app.fileHash);

    auto it = m_profilesCache.find(appId);
    if (it != m_profilesCache.end()) {
        return it->second;
    }

    // Tentar carregar do banco SQLite
    AppProfileRecord rec;
    if (DatabaseManager::Instance().LoadAppProfile(appId, rec)) {
        AppProfile prof;
        prof.appId = rec.appId;
        prof.normalizedPath = rec.normalizedPath;
        prof.processName = rec.processName;
        prof.fileHash = rec.fileHash;
        prof.objective = static_cast<AppObjective>(rec.objective);
        prof.authorizedModules = rec.authorizedModules;
        prof.confidenceScore = rec.confidenceScore;
        prof.baselineScore = rec.baselineScore;
        prof.experimentCount = rec.experimentCount;
        prof.lastValidatedTimestamp = rec.lastValidated;

        m_profilesCache[appId] = prof;
        return prof;
    }

    // Perfil novo padrão
    AppProfile newProf;
    newProf.appId = appId;
    newProf.normalizedPath = app.normalizedPath;
    newProf.processName = app.processName;
    newProf.fileHash = app.fileHash;
    newProf.objective = AppObjective::Balance; // Padrão equilibrado
    newProf.authorizedModules = "power,cpu,core_parking,priority,services";
    newProf.confidenceScore = 0.5;
    newProf.baselineScore = 75.0;
    newProf.experimentCount = 0;

    m_profilesCache[appId] = newProf;

    // Salvar no banco
    rec.appId = newProf.appId;
    rec.normalizedPath = newProf.normalizedPath;
    rec.processName = newProf.processName;
    rec.fileHash = newProf.fileHash;
    rec.objective = static_cast<int>(newProf.objective);
    rec.authorizedModules = newProf.authorizedModules;
    rec.confidenceScore = newProf.confidenceScore;
    rec.baselineScore = newProf.baselineScore;
    rec.experimentCount = newProf.experimentCount;
    DatabaseManager::Instance().SaveAppProfile(rec);

    return newProf;
}

bool AppProfileManager::SaveProfile(const AppProfile& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_profilesCache[profile.appId] = profile;
    return SaveProfileLocked(profile);
}

bool AppProfileManager::SaveProfileLocked(const AppProfile& profile) {
    AppProfileRecord rec;
    rec.appId = profile.appId;
    rec.normalizedPath = profile.normalizedPath;
    rec.processName = profile.processName;
    rec.fileHash = profile.fileHash;
    rec.objective = static_cast<int>(profile.objective);
    rec.authorizedModules = profile.authorizedModules;
    rec.confidenceScore = profile.confidenceScore;
    rec.baselineScore = profile.baselineScore;
    rec.experimentCount = profile.experimentCount;
    rec.lastValidated = profile.lastValidatedTimestamp;

    return DatabaseManager::Instance().SaveAppProfile(rec);
}

bool AppProfileManager::UpdateObjective(const std::string& appId, AppObjective objective) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profilesCache.find(appId);
    if (it == m_profilesCache.end()) return false;

    it->second.objective = objective;
    // SaveProfile() tentaria tomar a mutex que ja esta tomada aqui: a janela de perfis travava sem mensagem.
    return SaveProfileLocked(it->second);
}

double AppProfileManager::EvaluateObjectiveScore(AppObjective obj, const ValidatedSystemTelemetry& t, double frameTimeMs) {
    // Escala de pontuação objetiva: [0.0, 100.0]
    // frameTimeMs: tempo real por quadro (DWM); 0.0 = indisponivel e NAO penaliza.
    double score = 100.0;

    // Fonte do frame time: metrica validada quando existe; o parametro e o caminho
    // para chamadas que ja trazem o valor medido sem telemetria validada.
    auto frameTimeUsable = [&t, frameTimeMs]() -> double {
        if (t.frameTimeMs.IsUsable()) return t.frameTimeMs.value;
        return (frameTimeMs > 0.0) ? frameTimeMs : 0.0;
    };

    switch (obj) {
    case AppObjective::MaxPerformance: {
        // Prioriza capacidade de CPU e ausência de restrições
        // Penalidades: throttling térmico (-35 pontos), temperatura excessiva, frame time acima de 60 Hz
        double cpuHeadroom = 100.0 - (t.cpuUsage.value * 0.15); // Carga pesada é esperada, leve penalidade
        if (t.isThermalThrottling) cpuHeadroom -= 35.0;
        if (t.cpuTemperatureC.IsUsable() && t.cpuTemperatureC.value > 85.0) {
            cpuHeadroom -= (t.cpuTemperatureC.value - 85.0) * 2.0;
        }
        double ft = frameTimeUsable();
        if (ft > 16.7) {
            cpuHeadroom -= std::min(30.0, (ft - 16.7) * 1.5);
        }
        score = std::clamp(cpuHeadroom, 0.0, 100.0);
        break;
    }
    case AppObjective::LowLatency: {
        // Prioriza tempo de resposta e ausência de contenção
        // Penalidades: saturação de disco (-30 pontos), fila de CPU 100% (-25 pontos), frame time alto
        double latencyScore = 100.0;
        if (t.cpuUsage.value > 90.0) latencyScore -= (t.cpuUsage.value - 90.0) * 2.5;
        if (t.diskBusyPercent.IsUsable() && t.diskBusyPercent.value > 70.0) {
            latencyScore -= (t.diskBusyPercent.value - 70.0) * 1.0;
        }
        if (t.isThermalThrottling) latencyScore -= 30.0;
        double ft = frameTimeUsable();
        if (ft > 16.7) {
            latencyScore -= std::min(35.0, (ft - 16.7) * 2.0);
        }
        score = std::clamp(latencyScore, 0.0, 100.0);
        break;
    }
    case AppObjective::Stability: {
        // Prioriza eliminação de engasgos (stutter) e margem térmica
        double stabScore = 100.0;
        if (t.isThermalThrottling) stabScore -= 40.0;
        if (t.cpuTemperatureC.IsUsable()) {
            if (t.cpuTemperatureC.value > 80.0) stabScore -= (t.cpuTemperatureC.value - 80.0) * 2.5;
        }
        if (t.cpuUsage.value > 95.0) stabScore -= 15.0;
        if (t.ramUsage.value > 90.0) stabScore -= 20.0;
        // Engasgo percebido: frame time acima de 33.3 ms e o limite pratico de 30 fps
        double ft = frameTimeUsable();
        if (ft > 33.3) {
            stabScore -= std::min(40.0, (ft - 33.3) * 2.0);
        }
        score = std::clamp(stabScore, 0.0, 100.0);
        break;
    }
    case AppObjective::Balance: {
        // Equilíbrio harmonioso entre temperatura, uso de recursos e eficiência
        double tempPart = 50.0;
        if (t.cpuTemperatureC.IsUsable()) {
            tempPart = 50.0 - std::max(0.0, (t.cpuTemperatureC.value - 60.0) * 1.25);
        }
        double loadPart = 50.0 - (t.cpuUsage.value * 0.25);
        if (t.isThermalThrottling) loadPart -= 25.0;
        score = std::clamp(tempPart + loadPart, 0.0, 100.0);
        break;
    }
    case AppObjective::Eco: {
        // Consumo e o que se quer reduzir: pontua por energia medida e frieza termica.
        // cpuUsage deixa de ser base de proposito: o estacionamento de nucleos (mecanismo da
        // propria eco) ELEVA o uso percentual dos nucleos restantes e se puniria sozinho.
        double ecoScore = 100.0;
        bool anySignal = false;
        if (t.gpuPowerWatts.IsUsable()) {
            anySignal = true;
            // 0 W neutro; ~150 W ja e carga pesada de desktop (-40 pts no teto)
            ecoScore -= std::min(40.0, t.gpuPowerWatts.value * 0.27);
        }
        if (t.cpuTemperatureC.IsUsable()) {
            anySignal = true;
            if (t.cpuTemperatureC.value > 60.0) {
                ecoScore -= std::min(30.0, (t.cpuTemperatureC.value - 60.0) * 1.5);
            }
        }
        if (!anySignal) {
            // Sem sensor de energia nem termico: consumo real e desconhecido. Nota neutra
            // e honesta (nem sucesso nem fracasso de eco), em vez de inferir consumo de cpuUsage.
            ecoScore = 50.0;
        }
        if (t.isThermalThrottling) ecoScore -= 20.0; // energia desperdicada em calor
        score = std::clamp(ecoScore, 0.0, 100.0);
        break;
    }
    }

    return score;
}

} // namespace opitcore
