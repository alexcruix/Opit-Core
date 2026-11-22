#include "Config.h"
#include "DatabaseManager.h"
#include "utils/FileUtils.h"
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace opitcore {

Config& Config::Instance() {
    static Config instance;
    return instance;
}

std::string Config::ProfileToString(Profile p) {
    switch (p) {
        case Profile::Eco: return "Eco";
        case Profile::HighPerformance: return "HighPerformance";
        default: return "Balanced";
    }
}

Config::Profile Config::StringToProfile(const std::string& str) {
    if (str == "Eco") return Profile::Eco;
    if (str == "HighPerformance") return Profile::HighPerformance;
    return Profile::Balanced;
}

void Config::AddCustomGame(const std::string& game) {
    if (game.empty()) return;
    for (const auto& g : m_customGames) {
        if (g == game) return;
    }
    m_customGames.push_back(game);
}

void Config::RemoveCustomGame(const std::string& game) {
    auto it = std::find(m_customGames.begin(), m_customGames.end(), game);
    if (it != m_customGames.end()) {
        m_customGames.erase(it);
    }
}

bool Config::Load(const std::string& path) {
    m_configPath = path;
    std::ifstream f(path);
    if (!f.is_open()) {
        // Criar configuração padrão
        Save(path);
        return true;
    }
    try {
        json j;
        f >> j;
        if (j.contains("profile")) {
            m_profile = StringToProfile(j["profile"]);
        }
        if (j.contains("autoPowerSwitch")) m_autoPowerSwitch = j["autoPowerSwitch"];
        if (j.contains("sampleIntervalMs")) m_sampleIntervalMs = j["sampleIntervalMs"];
        if (j.contains("thermalLimitC")) m_thermalLimitC = j["thermalLimitC"];
        if (j.contains("thermalProtectionEnabled")) m_thermalProtectionEnabled = j["thermalProtectionEnabled"];
        if (j.contains("autoGameModeEnabled")) m_autoGameModeEnabled = j["autoGameModeEnabled"];
        if (j.contains("startWithWindows")) m_startWithWindows = j["startWithWindows"];
        if (j.contains("startMinimized")) m_startMinimized = j["startMinimized"];
        if (j.contains("closeToTray")) m_closeToTray = j["closeToTray"];
        if (j.contains("processorThrottleCap")) m_processorThrottleCap = j["processorThrottleCap"];
        if (j.contains("lastPowerScheme")) m_lastPowerScheme = j["lastPowerScheme"];
        if (j.contains("customGames") && j["customGames"].is_array()) {
            m_customGames.clear();
            for (const auto& item : j["customGames"]) {
                m_customGames.push_back(item);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[OPIT] Erro ao carregar config: " << e.what() << std::endl;
        return false;
    }
}

bool Config::Save(const std::string& path) {
    m_configPath = path;
    try {
        json j;
        j["profile"] = ProfileToString(m_profile);
        j["autoPowerSwitch"] = m_autoPowerSwitch;
        j["sampleIntervalMs"] = m_sampleIntervalMs;
        j["thermalLimitC"] = m_thermalLimitC;
        j["thermalProtectionEnabled"] = m_thermalProtectionEnabled;
        j["autoGameModeEnabled"] = m_autoGameModeEnabled;
        j["startWithWindows"] = m_startWithWindows;
        j["startMinimized"] = m_startMinimized;
        j["closeToTray"] = m_closeToTray;
        j["processorThrottleCap"] = m_processorThrottleCap;
        j["lastPowerScheme"] = m_lastPowerScheme;
        j["customGames"] = m_customGames;

        std::ofstream f(path);
        f << j.dump(4);

        // Salvar também no SQLite se estiver disponível
        auto& db = DatabaseManager::Instance();
        if (db.IsOpen()) {
            db.SetSetting("profile", ProfileToString(m_profile));
            db.SetSetting("sampleIntervalMs", std::to_string(m_sampleIntervalMs));
            db.SetSetting("thermalLimitC", std::to_string(m_thermalLimitC));
            db.SetSetting("thermalProtectionEnabled", m_thermalProtectionEnabled ? "1" : "0");
            db.SetSetting("autoGameModeEnabled", m_autoGameModeEnabled ? "1" : "0");
            db.SetSetting("lastPowerScheme", m_lastPowerScheme);
        }

        return f.good();
    } catch (const std::exception& e) {
        std::cerr << "[OPIT] Erro ao salvar config: " << e.what() << std::endl;
        return false;
    }
}

} // namespace opitcore
