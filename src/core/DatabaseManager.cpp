// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "DatabaseManager.h"
#include "HardwareKnowledgeBase.h"
#include "Logger.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"

#include "sqlite3/sqlite3.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace opitcore {

static std::string CurrentIsoTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    // A libc nao tem localtime_s: o equivalente reentrante chama-se localtime_r e leva os
    // argumentos na ordem inversa. Sem isto o carimbo de hora do banco nao compilava em Linux.
    localtime_r(&time, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

DatabaseManager& DatabaseManager::Instance() {
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::DatabaseManager() = default;

DatabaseManager::~DatabaseManager() {
    Close();
}

bool DatabaseManager::IsOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_db != nullptr);
}

void DatabaseManager::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        OPIT_LOG_INFO("DatabaseManager: Banco de dados fechado");
    }
}

bool DatabaseManager::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        return true;
    }

    if (path.empty()) {
        std::string dir = FileUtils::GetAppDataDirectory();
        FileUtils::EnsureDirectory(dir);
        m_dbPath = FileUtils::CombinePath(dir, "opitcore.db");
    } else {
        m_dbPath = path;
    }

    int rc = sqlite3_open(m_dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::string err = m_db ? sqlite3_errmsg(m_db) : "Erro de alocacao";
        OPIT_LOG_ERROR("DatabaseManager: Falha ao abrir banco (" + m_dbPath + "): " + err);
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return false;
    }

    OPIT_LOG_INFO("DatabaseManager: Banco aberto em: " + m_dbPath);
    return ApplyMigrations();
}

bool DatabaseManager::ApplyMigrations() {
    if (!m_db) return false;

    // 1. Tabela de controle de versão
    const char* sqlSchema = 
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version INTEGER PRIMARY KEY,"
        "  applied_at TEXT NOT NULL"
        ");";
    char* errmsg = nullptr;
    if (sqlite3_exec(m_db, sqlSchema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "";
        sqlite3_free(errmsg);
        OPIT_LOG_ERROR("DatabaseManager: Erro ao criar schema_version: " + err);
        return false;
    }

    // Obter versão atual
    int currentVer = 0;
    const char* sqlGetVer = "SELECT MAX(version) FROM schema_version;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlGetVer, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            currentVer = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    m_schemaVersion = currentVer;

    // Migração 1: Tabelas base
    if (m_schemaVersion < 1) {
        const char* migration1 = 
            "BEGIN TRANSACTION;"
            "CREATE TABLE IF NOT EXISTS settings ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS game_rules ("
            "  exe_name TEXT PRIMARY KEY,"
            "  profile TEXT NOT NULL,"
            "  enabled INTEGER DEFAULT 1,"
            "  created_at TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS audit_logs ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL,"
            "  level TEXT NOT NULL,"
            "  message TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS metrics_history ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL,"
            "  cpu_usage REAL NOT NULL,"
            "  ram_usage REAL NOT NULL,"
            "  temp_c REAL NOT NULL"
            ");"
            "INSERT INTO schema_version (version, applied_at) VALUES (1, datetime('now'));"
            "COMMIT;";

        if (sqlite3_exec(m_db, migration1, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            std::string err = errmsg ? errmsg : "";
            sqlite3_free(errmsg);
            OPIT_LOG_ERROR("DatabaseManager: Falha ao aplicar migracao 1: " + err);
            return false;
        }
        m_schemaVersion = 1;
        OPIT_LOG_INFO("DatabaseManager: Migracao 1 aplicada com sucesso");
    }

    // Migração 2: Base de conhecimento de hardware documentada
    if (m_schemaVersion < 2) {
        const char* migration2 = 
            "BEGIN TRANSACTION;"
            "CREATE TABLE IF NOT EXISTS hardware_specs ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  category TEXT NOT NULL,"
            "  family TEXT NOT NULL,"
            "  model TEXT NOT NULL,"
            "  cores_threads TEXT,"
            "  clock_base TEXT,"
            "  clock_boost TEXT,"
            "  tdp TEXT,"
            "  socket_or_bus TEXT,"
            "  details TEXT"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_hw_model ON hardware_specs(model);"
            "CREATE INDEX IF NOT EXISTS idx_hw_family ON hardware_specs(family);"
            "CREATE INDEX IF NOT EXISTS idx_hw_cat ON hardware_specs(category);"
            "INSERT INTO schema_version (version, applied_at) VALUES (2, datetime('now'));"
            "COMMIT;";

        if (sqlite3_exec(m_db, migration2, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            std::string err = errmsg ? errmsg : "";
            sqlite3_free(errmsg);
            OPIT_LOG_ERROR("DatabaseManager: Falha ao aplicar migracao 2: " + err);
            return false;
        }
        m_schemaVersion = 2;
        OPIT_LOG_INFO("DatabaseManager: Migracao 2 aplicada com sucesso (Tabela hardware_specs criada)");
    }

    // Migração 3: Motor de Inteligência Adaptativa, Diário de Recuperação e Perfis Individuais
    if (m_schemaVersion < 3) {
        const char* migration3 = 
            "BEGIN TRANSACTION;"
            "CREATE TABLE IF NOT EXISTS app_profiles ("
            "  app_id TEXT PRIMARY KEY,"
            "  normalized_path TEXT NOT NULL,"
            "  process_name TEXT NOT NULL,"
            "  file_hash TEXT,"
            "  objective INTEGER NOT NULL DEFAULT 3,"
            "  authorized_modules TEXT,"
            "  confidence_score REAL DEFAULT 0.5,"
            "  baseline_score REAL DEFAULT 0.0,"
            "  experiment_count INTEGER DEFAULT 0,"
            "  last_validated TEXT,"
            "  created_at TEXT NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_app_proc ON app_profiles(process_name);"
            "CREATE TABLE IF NOT EXISTS recovery_journal ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  change_id TEXT NOT NULL UNIQUE,"
            "  resource_type TEXT NOT NULL,"
            "  resource_key TEXT NOT NULL,"
            "  original_value TEXT NOT NULL,"
            "  applied_value TEXT NOT NULL,"
            "  status TEXT NOT NULL,"
            "  timestamp TEXT NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_journal_status ON recovery_journal(status);"
            "CREATE TABLE IF NOT EXISTS adaptive_experiments ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  app_id TEXT NOT NULL,"
            "  variable_name TEXT NOT NULL,"
            "  baseline_score REAL NOT NULL,"
            "  result_score REAL NOT NULL,"
            "  score_delta REAL NOT NULL,"
            "  verdict TEXT NOT NULL,"
            "  justification TEXT,"
            "  timestamp TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS decision_history ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  app_id TEXT,"
            "  action_type INTEGER NOT NULL,"
            "  target_resource TEXT,"
            "  hypothesis TEXT,"
            "  justification TEXT,"
            "  confidence REAL NOT NULL,"
            "  safety_override INTEGER DEFAULT 0,"
            "  timestamp TEXT NOT NULL"
            ");"
            "INSERT INTO schema_version (version, applied_at) VALUES (3, datetime('now'));"
            "COMMIT;";

        if (sqlite3_exec(m_db, migration3, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            std::string err = errmsg ? errmsg : "";
            sqlite3_free(errmsg);
            OPIT_LOG_ERROR("DatabaseManager: Falha ao aplicar migracao 3: " + err);
            return false;
        }
        m_schemaVersion = 3;
        OPIT_LOG_INFO("DatabaseManager: Migracao 3 aplicada com sucesso (Memoria adaptativa e diario de recuperacao criados)");
    }

    // Migracao 4: Expansao de colunas de perfis detalhados por jogo
    if (m_schemaVersion < 4) {
        const char* cols[] = {
            "ALTER TABLE game_rules ADD COLUMN game_name TEXT DEFAULT '';",
            "ALTER TABLE game_rules ADD COLUMN full_path TEXT DEFAULT '';",
            "ALTER TABLE game_rules ADD COLUMN cpu_profile TEXT DEFAULT 'HighPerformance';",
            "ALTER TABLE game_rules ADD COLUMN cpu_min INTEGER DEFAULT 5;",
            "ALTER TABLE game_rules ADD COLUMN cpu_max INTEGER DEFAULT 100;",
            "ALTER TABLE game_rules ADD COLUMN gpu_profile TEXT DEFAULT 'HighPerformance';",
            "ALTER TABLE game_rules ADD COLUMN gpu_power INTEGER DEFAULT 100;",
            "ALTER TABLE game_rules ADD COLUMN gpu_core_offset INTEGER DEFAULT 0;",
            "ALTER TABLE game_rules ADD COLUMN gpu_mem_offset INTEGER DEFAULT 0;",
            "ALTER TABLE game_rules ADD COLUMN power_scheme TEXT DEFAULT '';",
            "ALTER TABLE game_rules ADD COLUMN prevent_disk_sleep INTEGER DEFAULT 1;",
            "ALTER TABLE game_rules ADD COLUMN prevent_system_sleep INTEGER DEFAULT 1;"
        };
        for (const char* sqlCol : cols) {
            char* colErr = nullptr;
            sqlite3_exec(m_db, sqlCol, nullptr, nullptr, &colErr);
            if (colErr) sqlite3_free(colErr);
        }
        sqlite3_exec(m_db, "INSERT INTO schema_version (version, applied_at) VALUES (4, datetime('now'));", nullptr, nullptr, nullptr);
        m_schemaVersion = 4;
        OPIT_LOG_INFO("DatabaseManager: Migracao 4 aplicada com sucesso (Perfis expandidos de Jogos)");
    }

    // Migracao 5: normaliza exe_name para minusculas. GetGameProfile consulta em minusculas;
    // linhas gravadas por builds antigos ou externamente com maiusculas nunca casavam e o perfil
    // individual do jogo era ignorado em silencio (medido 2026-11-18: 'JogoTeste.exe' vs 'jogoteste.exe').
    if (m_schemaVersion < 5) {
        sqlite3_exec(m_db, "DELETE FROM game_rules WHERE rowid NOT IN (SELECT MIN(rowid) FROM game_rules GROUP BY lower(exe_name));", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "UPDATE game_rules SET exe_name = lower(exe_name);", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "INSERT INTO schema_version (version, applied_at) VALUES (5, datetime('now'));", nullptr, nullptr, nullptr);
        m_schemaVersion = 5;
        OPIT_LOG_INFO("DatabaseManager: Migracao 5 aplicada com sucesso (exe_name de jogos normalizado para minusculas)");
    }

    // Semente da base de conhecimento roda em TODA abertura: instalacoes ja migradas
    // (schema >= 2) ficavam congeladas com os modelos da epoca do primeiro seed —
    // expansoes da base nunca chegavam a elas. A funcao e idempotente (kb_meta.seed_version).
    SeedHardwareKnowledgeBase();

    return true;
}

bool DatabaseManager::SeedHardwareKnowledgeBase() {
    if (!m_db) return false;

    // Versao da semente: quantas vezes a base embutida ja foi expandida. Expansoes chegam
    // a instalacoes antigas (count > 0) porque o marcador kb_meta.seed_version fica atras,
    // nao porque a tabela esta vazia — o caminho antigo (seed so com COUNT=0) nunca
    // entregaria novos modelos a quem ja usava o programa.
    sqlite3_exec(m_db, "CREATE TABLE IF NOT EXISTS kb_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);", nullptr, nullptr, nullptr);
    int seededVersion = 0;
    {
        sqlite3_stmt* vStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT value FROM kb_meta WHERE key = 'seed_version';", -1, &vStmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(vStmt) == SQLITE_ROW) {
                const unsigned char* v = sqlite3_column_text(vStmt, 0);
                if (v) seededVersion = std::atoi(reinterpret_cast<const char*>(v));
            }
            sqlite3_finalize(vStmt);
        }
    }
    const int targetVersion = HardwareKnowledgeBase::GetSeedVersion();

    const auto& specs = HardwareKnowledgeBase::GetDocumentedSpecs();
    if (specs.empty()) return true;
    if (seededVersion >= targetVersion) {
        return true; // Semente atual nesta instalacao
    }

    char* errmsg = nullptr;
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Insere apenas modelos ausentes (case-insensitive): preserva linhas antigas e evita duplicatas.
    const char* sqlInsert =
        "INSERT INTO hardware_specs (category, family, model, cores_threads, clock_base, clock_boost, tdp, socket_or_bus, details) "
        "SELECT ?, ?, ?, ?, ?, ?, ?, ?, ? "
        "WHERE NOT EXISTS (SELECT 1 FROM hardware_specs WHERE model = ?3 COLLATE NOCASE);";
    sqlite3_stmt* insertStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &insertStmt, nullptr) == SQLITE_OK) {
        for (const auto& item : specs) {
            sqlite3_bind_text(insertStmt, 1, item.category, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 2, item.family, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 3, item.model, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 4, item.coresThreads, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 5, item.clockBase, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 6, item.clockBoost, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 7, item.tdp, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 8, item.socketOrBus, -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 9, item.details, -1, SQLITE_STATIC);

            sqlite3_step(insertStmt);
            sqlite3_reset(insertStmt);
        }
        sqlite3_finalize(insertStmt);
    }

    sqlite3_stmt* metaStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO kb_meta (key, value) VALUES ('seed_version', ?);", -1, &metaStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(metaStmt, 1, std::to_string(targetVersion).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(metaStmt);
        sqlite3_finalize(metaStmt);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    OPIT_LOG_INFO("DatabaseManager: Base de conhecimento atualizada para a semente " + std::to_string(targetVersion) +
                  " (" + std::to_string(specs.size()) + " modelos embutidos; ausentes inseridos)");
    return true;
}

DocumentedHardwareSpec DatabaseManager::LookupHardwareSpec(const std::string& modelName, const std::string& categoryPrefix) {
    std::lock_guard<std::mutex> lock(m_mutex);
    DocumentedHardwareSpec result;
    if (!m_db || modelName.empty()) return result;

    // Normalizar a string de busca: remover tags irrelevantes
    std::string s = modelName;
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll("(R)", " ");
    replaceAll("(TM)", " ");
    replaceAll("@", " ");
    replaceAll("-", " ");
    replaceAll("Processor", " ");
    replaceAll("CPU", " ");
    replaceAll("12-Core", " ");
    replaceAll("16-Core", " ");
    replaceAll("8-Core", " ");
    replaceAll("6-Core", " ");
    replaceAll("4-Core", " ");

    std::vector<std::string> tokens = StringUtils::Split(s, ' ');
    std::vector<std::string> words;
    for (const auto& t : tokens) {
        std::string trimT = StringUtils::Trim(t);
        if (trimT.length() > 1 && StringUtils::ToLower(trimT) != "amd" &&
            StringUtils::ToLower(trimT) != "intel" && StringUtils::ToLower(trimT) != "with" &&
            StringUtils::ToLower(trimT) != "graphics" && StringUtils::ToLower(trimT) != "ghz" &&
            StringUtils::ToLower(trimT) != "nvidia" && StringUtils::ToLower(trimT) != "geforce" &&
            StringUtils::ToLower(trimT) != "radeon" && StringUtils::ToLower(trimT) != "processor") {
            words.push_back(trimT);
        }
    }

    auto executeQuery = [&](const std::string& pattern) -> bool {
        std::string sql = "SELECT category, family, model, cores_threads, clock_base, clock_boost, tdp, socket_or_bus, details "
                          "FROM hardware_specs WHERE model LIKE ? ";
        if (!categoryPrefix.empty()) {
            sql += "AND category LIKE ? ";
        }
        // Modelo mais especifico primeiro: %4070% casa com "RTX 4070 SUPER" e com "RTX 4070";
        // sem esta ordenacao o LIMIT 1 podia devolver a variante errada.
        sql += "ORDER BY LENGTH(model) ASC LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        if (!categoryPrefix.empty()) {
            sqlite3_bind_text(stmt, 2, (categoryPrefix + "%").c_str(), -1, SQLITE_TRANSIENT);
        }
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto getText = [&](int col) -> std::string {
                const unsigned char* txt = sqlite3_column_text(stmt, col);
                return txt ? reinterpret_cast<const char*>(txt) : "";
            };
            result.found = true;
            result.category = getText(0);
            result.family = getText(1);
            result.model = getText(2);
            result.coresThreads = getText(3);
            result.clockBase = getText(4);
            result.clockBoost = getText(5);
            result.tdp = getText(6);
            result.socketOrBus = getText(7);
            result.details = getText(8);
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    };

    // 0. Padrao completo com todos os termos significativos na ordem (ex: "%RTX%4070%Ti%" para
    // "NVIDIA GeForce RTX 4070 Ti"). Distingue variantes ("4070 Ti SUPER" x "4070" x "4070 Ti")
    // e nomes mobile ("...4060 Laptop GPU") antes do casamento solto por numero.
    if (words.size() >= 2) {
        std::string fullPattern;
        for (const auto& w : words) {
            fullPattern += "%" + w;
        }
        fullPattern += "%";
        if (executeQuery(fullPattern)) {
            return result;
        }
    }

    // 1. Tokens numericos especificos (ex: "1700X", "7800X3D", "13700K", "5800X"), do mais
    // longo ao mais curto. Tokens curtos ("i7", "RX") sao excluidos: %i7% casa com qualquer
    // modelo Intel e devolveria um processador arbitrario antes de tentar "13700K".
    std::vector<std::string> digitTokens;
    for (const auto& w : words) {
        bool hasDigit = false;
        for (char c : w) {
            if (c >= '0' && c <= '9') { hasDigit = true; break; }
        }
        if (hasDigit && w.length() >= 4) {
            digitTokens.push_back(w);
        }
    }
    std::sort(digitTokens.begin(), digitTokens.end(),
              [](const std::string& a, const std::string& b) { return a.length() > b.length(); });
    for (const auto& w : digitTokens) {
        std::string pattern = "%" + w + "%";
        if (executeQuery(pattern)) {
            return result;
        }
    }

    // 2. Tentar busca combinada dos 2 primeiros termos significativos (ex: "%Ryzen%7%" ou "%Core%i7%")
    if (words.size() >= 2) {
        std::string pattern = "%" + words[0] + "%" + words[1] + "%";
        if (executeQuery(pattern)) {
            return result;
        }
    }

    // 3. Fallback: procurar por palavra isolada inicial
    if (!words.empty()) {
        std::string pattern = "%" + words[0] + "%";
        executeQuery(pattern);
    }

    return result;
}

std::vector<MetricHistoryRecord> DatabaseManager::GetMetricsHistory(int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MetricHistoryRecord> list;
    if (!m_db) return list;

    const char* sql = "SELECT id, timestamp, cpu_usage, ram_usage, temp_c FROM metrics_history ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 50);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MetricHistoryRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        const unsigned char* ts = sqlite3_column_text(stmt, 1);
        rec.timestamp = ts ? reinterpret_cast<const char*>(ts) : "";
        rec.cpuUsage = sqlite3_column_double(stmt, 2);
        rec.ramUsage = sqlite3_column_double(stmt, 3);
        rec.tempC = sqlite3_column_double(stmt, 4);
        list.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return list;
}

bool DatabaseManager::ClearMetricsHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;
    char* errmsg = nullptr;
    int rc = sqlite3_exec(m_db, "DELETE FROM metrics_history;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool DatabaseManager::SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT OR REPLACE INTO settings (key, value, updated_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::string DatabaseManager::GetSetting(const std::string& key, const std::string& defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return defaultValue;

    const char* sql = "SELECT value FROM settings WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return defaultValue;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string val = defaultValue;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) {
            val = reinterpret_cast<const char*>(text);
        }
    }
    sqlite3_finalize(stmt);
    return val;
}

bool DatabaseManager::AddGameRule(const std::string& exeName, const std::string& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT OR REPLACE INTO game_rules (exe_name, profile, enabled, created_at) VALUES (?, ?, 1, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    sqlite3_bind_text(stmt, 1, lower.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, profile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::RemoveGameRule(const std::string& exeName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "DELETE FROM game_rules WHERE exe_name = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    sqlite3_bind_text(stmt, 1, lower.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::SaveGameProfile(const GameProfileRecord& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql =
        "INSERT OR REPLACE INTO game_rules ("
        "  exe_name, profile, enabled, created_at, game_name, full_path, "
        "  cpu_profile, cpu_min, cpu_max, gpu_profile, gpu_power, "
        "  gpu_core_offset, gpu_mem_offset, power_scheme, prevent_disk_sleep, prevent_system_sleep"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    std::string lower = StringUtils::ToLower(StringUtils::Trim(profile.exeName));

    sqlite3_bind_text(stmt, 1, lower.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, profile.cpuProfile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, profile.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, profile.gameName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, profile.fullPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, profile.cpuProfile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, static_cast<int>(profile.cpuMinPercent));
    sqlite3_bind_int(stmt, 9, static_cast<int>(profile.cpuMaxPercent));
    sqlite3_bind_text(stmt, 10, profile.gpuProfile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, profile.gpuPowerLimitPercent);
    sqlite3_bind_int(stmt, 12, profile.gpuCoreOffsetMHz);
    sqlite3_bind_int(stmt, 13, profile.gpuMemOffsetMHz);
    sqlite3_bind_text(stmt, 14, profile.powerSchemeGuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 15, profile.preventDiskSleep ? 1 : 0);
    sqlite3_bind_int(stmt, 16, profile.preventSystemSleep ? 1 : 0);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::GetGameProfile(const std::string& exeName, GameProfileRecord& outProfile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql =
        "SELECT exe_name, game_name, full_path, cpu_profile, cpu_min, cpu_max, "
        "       gpu_profile, gpu_power, gpu_core_offset, gpu_mem_offset, "
        "       power_scheme, prevent_disk_sleep, prevent_system_sleep, enabled "
        "FROM game_rules WHERE exe_name = ? COLLATE NOCASE;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string lower = StringUtils::ToLower(StringUtils::Trim(exeName));
    sqlite3_bind_text(stmt, 1, lower.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        auto getTxt = [&](int col) -> std::string {
            const unsigned char* t = sqlite3_column_text(stmt, col);
            return t ? reinterpret_cast<const char*>(t) : "";
        };

        outProfile.exeName = getTxt(0);
        outProfile.gameName = getTxt(1);
        outProfile.fullPath = getTxt(2);
        outProfile.cpuProfile = getTxt(3);
        if (outProfile.cpuProfile.empty()) outProfile.cpuProfile = "HighPerformance";
        outProfile.cpuMinPercent = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        outProfile.cpuMaxPercent = static_cast<uint32_t>(sqlite3_column_int(stmt, 5));
        if (outProfile.cpuMaxPercent == 0) outProfile.cpuMaxPercent = 100;
        outProfile.gpuProfile = getTxt(6);
        if (outProfile.gpuProfile.empty()) outProfile.gpuProfile = "HighPerformance";
        outProfile.gpuPowerLimitPercent = sqlite3_column_int(stmt, 7);
        if (outProfile.gpuPowerLimitPercent == 0) outProfile.gpuPowerLimitPercent = 100;
        outProfile.gpuCoreOffsetMHz = sqlite3_column_int(stmt, 8);
        outProfile.gpuMemOffsetMHz = sqlite3_column_int(stmt, 9);
        outProfile.powerSchemeGuid = getTxt(10);
        outProfile.preventDiskSleep = (sqlite3_column_int(stmt, 11) != 0);
        outProfile.preventSystemSleep = (sqlite3_column_int(stmt, 12) != 0);
        outProfile.enabled = (sqlite3_column_int(stmt, 13) != 0);
    }

    sqlite3_finalize(stmt);
    return found;
}

std::vector<GameProfileRecord> DatabaseManager::GetAllGameProfiles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<GameProfileRecord> profiles;
    if (!m_db) return profiles;

    const char* sql =
        "SELECT exe_name, game_name, full_path, cpu_profile, cpu_min, cpu_max, "
        "       gpu_profile, gpu_power, gpu_core_offset, gpu_mem_offset, "
        "       power_scheme, prevent_disk_sleep, prevent_system_sleep, enabled "
        "FROM game_rules ORDER BY exe_name ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return profiles;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameProfileRecord p;
        auto getTxt = [&](int col) -> std::string {
            const unsigned char* t = sqlite3_column_text(stmt, col);
            return t ? reinterpret_cast<const char*>(t) : "";
        };

        p.exeName = getTxt(0);
        p.gameName = getTxt(1);
        p.fullPath = getTxt(2);
        p.cpuProfile = getTxt(3);
        if (p.cpuProfile.empty()) p.cpuProfile = "HighPerformance";
        p.cpuMinPercent = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        p.cpuMaxPercent = static_cast<uint32_t>(sqlite3_column_int(stmt, 5));
        if (p.cpuMaxPercent == 0) p.cpuMaxPercent = 100;
        p.gpuProfile = getTxt(6);
        if (p.gpuProfile.empty()) p.gpuProfile = "HighPerformance";
        p.gpuPowerLimitPercent = sqlite3_column_int(stmt, 7);
        if (p.gpuPowerLimitPercent == 0) p.gpuPowerLimitPercent = 100;
        p.gpuCoreOffsetMHz = sqlite3_column_int(stmt, 8);
        p.gpuMemOffsetMHz = sqlite3_column_int(stmt, 9);
        p.powerSchemeGuid = getTxt(10);
        p.preventDiskSleep = (sqlite3_column_int(stmt, 11) != 0);
        p.preventSystemSleep = (sqlite3_column_int(stmt, 12) != 0);
        p.enabled = (sqlite3_column_int(stmt, 13) != 0);

        profiles.push_back(p);
    }

    sqlite3_finalize(stmt);
    return profiles;
}

bool DatabaseManager::DeleteGameProfile(const std::string& exeName) {
    return RemoveGameRule(exeName);
}

std::vector<GameRuleRecord> DatabaseManager::GetGameRules() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<GameRuleRecord> rules;
    if (!m_db) return rules;

    const char* sql = "SELECT exe_name, profile, enabled FROM game_rules ORDER BY exe_name ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return rules;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameRuleRecord r;
        const unsigned char* t0 = sqlite3_column_text(stmt, 0);
        const unsigned char* t1 = sqlite3_column_text(stmt, 1);
        if (t0) r.exeName = reinterpret_cast<const char*>(t0);
        if (t1) r.profile = reinterpret_cast<const char*>(t1);
        r.enabled = (sqlite3_column_int(stmt, 2) != 0);
        rules.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rules;
}

bool DatabaseManager::LogEvent(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT INTO audit_logs (timestamp, level, message) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::RecordMetric(double cpuUsage, double ramUsage, double tempC) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT INTO metrics_history (timestamp, cpu_usage, ram_usage, temp_c) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, cpuUsage);
    sqlite3_bind_double(stmt, 3, ramUsage);
    sqlite3_bind_double(stmt, 4, tempC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::AddRecoveryJournalEntry(const RecoveryJournalRecord& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT OR REPLACE INTO recovery_journal (change_id, resource_type, resource_key, original_value, applied_value, status, timestamp) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string ts = entry.timestamp.empty() ? CurrentIsoTimestamp() : entry.timestamp;
    sqlite3_bind_text(stmt, 1, entry.changeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.resourceType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.resourceKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.originalValue.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.appliedValue.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, entry.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, ts.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::UpdateRecoveryJournalStatus(const std::string& changeId, const std::string& status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "UPDATE recovery_journal SET status = ? WHERE change_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, changeId.c_str(), -1, SQLITE_TRANSIENT);

    // Um UPDATE que nao encontra a linha e SQLITE_DONE: sem alterar linhas nao houve sucesso.
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(m_db) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::LoadRecoveryJournalEntry(const std::string& changeId, RecoveryJournalRecord& outEntry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "SELECT id, change_id, resource_type, resource_key, original_value, applied_value, status, timestamp "
                      "FROM recovery_journal WHERE change_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, changeId.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        outEntry.id = sqlite3_column_int64(stmt, 0);
        const unsigned char* t1 = sqlite3_column_text(stmt, 1);
        const unsigned char* t2 = sqlite3_column_text(stmt, 2);
        const unsigned char* t3 = sqlite3_column_text(stmt, 3);
        const unsigned char* t4 = sqlite3_column_text(stmt, 4);
        const unsigned char* t5 = sqlite3_column_text(stmt, 5);
        const unsigned char* t6 = sqlite3_column_text(stmt, 6);
        const unsigned char* t7 = sqlite3_column_text(stmt, 7);
        if (t1) outEntry.changeId = reinterpret_cast<const char*>(t1);
        if (t2) outEntry.resourceType = reinterpret_cast<const char*>(t2);
        if (t3) outEntry.resourceKey = reinterpret_cast<const char*>(t3);
        if (t4) outEntry.originalValue = reinterpret_cast<const char*>(t4);
        if (t5) outEntry.appliedValue = reinterpret_cast<const char*>(t5);
        if (t6) outEntry.status = reinterpret_cast<const char*>(t6);
        if (t7) outEntry.timestamp = reinterpret_cast<const char*>(t7);
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<RecoveryJournalRecord> DatabaseManager::GetPendingRecoveryEntries() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RecoveryJournalRecord> list;
    if (!m_db) return list;

    // Pendentes: preparadas (queda possivel durante a escrita), aplicadas ou confirmadas mas ainda nao validadas nem revertidas
    const char* sql = "SELECT id, change_id, resource_type, resource_key, original_value, applied_value, status, timestamp "
                      "FROM recovery_journal WHERE status IN ('PREPARED', 'APPLIED', 'CONFIRMED') ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RecoveryJournalRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        const unsigned char* t1 = sqlite3_column_text(stmt, 1);
        const unsigned char* t2 = sqlite3_column_text(stmt, 2);
        const unsigned char* t3 = sqlite3_column_text(stmt, 3);
        const unsigned char* t4 = sqlite3_column_text(stmt, 4);
        const unsigned char* t5 = sqlite3_column_text(stmt, 5);
        const unsigned char* t6 = sqlite3_column_text(stmt, 6);
        const unsigned char* t7 = sqlite3_column_text(stmt, 7);

        if (t1) r.changeId = reinterpret_cast<const char*>(t1);
        if (t2) r.resourceType = reinterpret_cast<const char*>(t2);
        if (t3) r.resourceKey = reinterpret_cast<const char*>(t3);
        if (t4) r.originalValue = reinterpret_cast<const char*>(t4);
        if (t5) r.appliedValue = reinterpret_cast<const char*>(t5);
        if (t6) r.status = reinterpret_cast<const char*>(t6);
        if (t7) r.timestamp = reinterpret_cast<const char*>(t7);

        list.push_back(r);
    }
    sqlite3_finalize(stmt);
    return list;
}

int DatabaseManager::PruneRecoveryJournalHistory(int keepLast) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db || keepLast < 0) return 0;

    // So elimina terminais (REVERTED/KEPT/FAILED_RECOVERY) fora da janela de retencao.
    // Entradas pendentes nunca sao alcancadas por este filtro, entao a poda jamais descarta uma mudanca por restaurar.
    const char* sql = "DELETE FROM recovery_journal "
                      "WHERE status IN ('REVERTED', 'KEPT', 'FAILED_RECOVERY') "
                      "  AND id < (SELECT COALESCE(MAX(id), 0) - ? FROM recovery_journal);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, keepLast);
    int removed = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        removed = sqlite3_changes(m_db);
    }
    sqlite3_finalize(stmt);
    return removed;
}

bool DatabaseManager::SaveAppProfile(const AppProfileRecord& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT OR REPLACE INTO app_profiles "
                      "(app_id, normalized_path, process_name, file_hash, objective, authorized_modules, confidence_score, baseline_score, experiment_count, last_validated, created_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, profile.appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, profile.normalizedPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, profile.processName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, profile.fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, profile.objective);
    sqlite3_bind_text(stmt, 6, profile.authorizedModules.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, profile.confidenceScore);
    sqlite3_bind_double(stmt, 8, profile.baselineScore);
    sqlite3_bind_int(stmt, 9, profile.experimentCount);
    sqlite3_bind_text(stmt, 10, profile.lastValidated.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, now.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::LoadAppProfile(const std::string& appId, AppProfileRecord& outProfile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "SELECT app_id, normalized_path, process_name, file_hash, objective, authorized_modules, confidence_score, baseline_score, experiment_count, last_validated "
                      "FROM app_profiles WHERE app_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, appId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        const unsigned char* t0 = sqlite3_column_text(stmt, 0);
        const unsigned char* t1 = sqlite3_column_text(stmt, 1);
        const unsigned char* t2 = sqlite3_column_text(stmt, 2);
        const unsigned char* t3 = sqlite3_column_text(stmt, 3);
        const unsigned char* t5 = sqlite3_column_text(stmt, 5);
        const unsigned char* t9 = sqlite3_column_text(stmt, 9);

        if (t0) outProfile.appId = reinterpret_cast<const char*>(t0);
        if (t1) outProfile.normalizedPath = reinterpret_cast<const char*>(t1);
        if (t2) outProfile.processName = reinterpret_cast<const char*>(t2);
        if (t3) outProfile.fileHash = reinterpret_cast<const char*>(t3);
        outProfile.objective = sqlite3_column_int(stmt, 4);
        if (t5) outProfile.authorizedModules = reinterpret_cast<const char*>(t5);
        outProfile.confidenceScore = sqlite3_column_double(stmt, 6);
        outProfile.baselineScore = sqlite3_column_double(stmt, 7);
        outProfile.experimentCount = sqlite3_column_int(stmt, 8);
        if (t9) outProfile.lastValidated = reinterpret_cast<const char*>(t9);
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<AppProfileRecord> DatabaseManager::GetAllAppProfiles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AppProfileRecord> list;
    if (!m_db) return list;

    const char* sql = "SELECT app_id, normalized_path, process_name, file_hash, objective, authorized_modules, confidence_score, baseline_score, experiment_count, last_validated FROM app_profiles;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AppProfileRecord p;
        const unsigned char* t0 = sqlite3_column_text(stmt, 0);
        const unsigned char* t1 = sqlite3_column_text(stmt, 1);
        const unsigned char* t2 = sqlite3_column_text(stmt, 2);
        const unsigned char* t3 = sqlite3_column_text(stmt, 3);
        const unsigned char* t5 = sqlite3_column_text(stmt, 5);
        const unsigned char* t9 = sqlite3_column_text(stmt, 9);

        if (t0) p.appId = reinterpret_cast<const char*>(t0);
        if (t1) p.normalizedPath = reinterpret_cast<const char*>(t1);
        if (t2) p.processName = reinterpret_cast<const char*>(t2);
        if (t3) p.fileHash = reinterpret_cast<const char*>(t3);
        p.objective = sqlite3_column_int(stmt, 4);
        if (t5) p.authorizedModules = reinterpret_cast<const char*>(t5);
        p.confidenceScore = sqlite3_column_double(stmt, 6);
        p.baselineScore = sqlite3_column_double(stmt, 7);
        p.experimentCount = sqlite3_column_int(stmt, 8);
        if (t9) p.lastValidated = reinterpret_cast<const char*>(t9);
        list.push_back(p);
    }
    sqlite3_finalize(stmt);
    return list;
}

bool DatabaseManager::RecordAdaptiveExperiment(const std::string& appId, const std::string& varName, double baseScore, double resultScore, double delta, const std::string& verdict, const std::string& justification) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT INTO adaptive_experiments (app_id, variable_name, baseline_score, result_score, score_delta, verdict, justification, timestamp) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, varName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, baseScore);
    sqlite3_bind_double(stmt, 4, resultScore);
    sqlite3_bind_double(stmt, 5, delta);
    sqlite3_bind_text(stmt, 6, verdict.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, justification.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, now.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::RecordDecisionHistory(const std::string& appId, int actionType, const std::string& targetResource, const std::string& hypothesis, const std::string& justification, double confidence, bool safetyOverride) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "INSERT INTO decision_history (app_id, action_type, target_resource, hypothesis, justification, confidence, safety_override, timestamp) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string now = CurrentIsoTimestamp();
    sqlite3_bind_text(stmt, 1, appId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, actionType);
    sqlite3_bind_text(stmt, 3, targetResource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, hypothesis.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, justification.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, confidence);
    sqlite3_bind_int(stmt, 7, safetyOverride ? 1 : 0);
    sqlite3_bind_text(stmt, 8, now.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace opitcore
