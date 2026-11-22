#include "AiAssistantManager.h"
#include "core/DatabaseManager.h"
#include "core/Logger.h"
#include "core/ModuleAdvancedSettings.h"
#include "core/NotificationManager.h"
#include "core/CoreRuntime.h"
#include "core/Config.h"
#include "platform/windows/ServiceManager.h"
#include "power/PowerManager.h"
#include "hardware/GpuControlManager.h"
#include "adaptive/AdaptiveEngine.h"
#include "utils/Types.h"
#include "utils/StringUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace opitcore {

namespace {

struct HttpResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

// DPAPI: a chave de API da IA nunca fica em texto puro no banco (mesma tecnica do SessionManager).
static std::string BytesToHex(const BYTE* data, DWORD len) {
    static constexpr char k_digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (DWORD i = 0; i < len; ++i) {
        s.push_back(k_digits[(data[i] >> 4) & 0x0F]);
        s.push_back(k_digits[data[i] & 0x0F]);
    }
    return s;
}

static std::vector<BYTE> HexToBytes(const std::string& hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<BYTE> out;
    if (hex.size() % 2 != 0) return out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = nibble(hex[i]);
        int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<BYTE>((hi << 4) | lo));
    }
    return out;
}

static std::string ProtectApiKeyDpapi(const std::string& plain) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Opit-Core AI API Key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::string hex = BytesToHex(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return hex;
}

static std::string UnprotectApiKeyDpapi(const std::string& hex) {
    std::vector<BYTE> enc = HexToBytes(hex);
    if (enc.empty()) return {};
    DATA_BLOB in{};
    in.pbData = enc.data();
    in.cbData = static_cast<DWORD>(enc.size());
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::string plain(reinterpret_cast<char*>(out.pbData), reinterpret_cast<char*>(out.pbData) + out.cbData);
    LocalFree(out.pbData);
    return plain;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

HttpResult ExecuteHttp(const std::string& method, const std::string& url,
                        const std::string& body,
                        const std::vector<std::string>& extraHeaders) {
    HttpResult res;
    std::wstring wUrl = Utf8ToWide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[4096]{};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) {
        res.error = "URL invalida ou malformada: " + url;
        return res;
    }

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = uc.nPort ? uc.nPort : (isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET hSession = WinHttpOpen(L"OpitCore-AiClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.error = "Falha ao inicializar sessao WinHTTP (" + std::to_string(GetLastError()) + ")";
        return res;
    }

    WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, port, 0);
    if (!hConnect) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hSession);
        res.error = "Falha ao conectar no servidor de IA (" + std::to_string(err) + ")";
        return res;
    }

    std::wstring wMethod = Utf8ToWide(method);
    DWORD openFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), pathBuf, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, openFlags);
    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error = "Falha ao criar requisicao HTTP (" + std::to_string(err) + ")";
        return res;
    }

    // Monta headers
    std::wstring allHeaders = L"Accept: application/json\r\n";
    if (!body.empty()) {
        allHeaders += L"Content-Type: application/json; charset=utf-8\r\n";
    }
    for (const auto& h : extraHeaders) {
        allHeaders += Utf8ToWide(h) + L"\r\n";
    }

    LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
    DWORD bodyLen = (DWORD)body.size();

    BOOL sent = WinHttpSendRequest(hRequest, allHeaders.c_str(), (DWORD)-1L, bodyPtr, bodyLen, bodyLen, 0);
    if (!sent) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error = "Falha ao enviar requisicao para a IA (" + std::to_string(err) + ")";
        return res;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error = "Tempo limite excedido aguardando resposta da IA (" + std::to_string(err) + ")";
        return res;
    }

    DWORD statusCode = 0;
    DWORD statusSz = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSz, WINHTTP_NO_HEADER_INDEX);
    res.status = (int)statusCode;

    std::vector<char> respData;
    DWORD bytesRead = 0;
    do {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable) || bytesAvailable == 0) {
            break;
        }
        size_t offset = respData.size();
        respData.resize(offset + bytesAvailable);
        if (WinHttpReadData(hRequest, respData.data() + offset, bytesAvailable, &bytesRead)) {
            respData.resize(offset + bytesRead);
        } else {
            break;
        }
    } while (bytesRead > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    res.body = std::string(respData.begin(), respData.end());
    res.ok = (res.status >= 200 && res.status < 300);
    return res;
}

} // namespace

AiAssistantManager& AiAssistantManager::Instance() {
    static AiAssistantManager instance;
    return instance;
}

AiAssistantManager::AiAssistantManager() {
    Initialize();
}

void AiAssistantManager::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;
    LoadSettings();
    m_initialized = true;
}

void AiAssistantManager::SetApiKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_apiKey = StringUtils::Trim(key);
}

std::string AiAssistantManager::GetApiKey() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_apiKey;
}

bool AiAssistantManager::HasApiKey() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_apiKey.empty();
}

void AiAssistantManager::SetProvider(AiProvider provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_provider = provider;
    if (m_selectedModel.empty()) {
        switch (m_provider) {
            case AiProvider::Gemini: m_selectedModel = "gemini-1.5-flash"; break;
            case AiProvider::OpenAi: m_selectedModel = "gpt-4o-mini"; break;
            case AiProvider::Anthropic: m_selectedModel = "claude-3-5-sonnet-20241022"; break;
            case AiProvider::DeepSeek: m_selectedModel = "deepseek-chat"; break;
            case AiProvider::Groq: m_selectedModel = "llama-3.3-70b-versatile"; break;
            case AiProvider::Custom: m_selectedModel = "gpt-4o-mini"; break;
        }
    }
}

AiProvider AiAssistantManager::GetProvider() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_provider;
}

std::string AiAssistantManager::GetProviderName() const {
    return ProviderToString(GetProvider());
}

std::string AiAssistantManager::ProviderToString(AiProvider prov) {
    switch (prov) {
        case AiProvider::Gemini: return "Google Gemini";
        case AiProvider::OpenAi: return "OpenAI ChatGPT";
        case AiProvider::Anthropic: return "Anthropic Claude";
        case AiProvider::DeepSeek: return "DeepSeek";
        case AiProvider::Groq: return "Groq";
        case AiProvider::Custom: return "Personalizado / Compativel";
        default: return "Google Gemini";
    }
}

AiProvider AiAssistantManager::StringToProvider(const std::string& str) {
    std::string s = StringUtils::ToLower(StringUtils::Trim(str));
    if (s.find("gemini") != std::string::npos) return AiProvider::Gemini;
    if (s.find("openai") != std::string::npos || s.find("chatgpt") != std::string::npos) return AiProvider::OpenAi;
    if (s.find("anthropic") != std::string::npos || s.find("claude") != std::string::npos) return AiProvider::Anthropic;
    if (s.find("deepseek") != std::string::npos) return AiProvider::DeepSeek;
    if (s.find("groq") != std::string::npos) return AiProvider::Groq;
    if (s.find("custom") != std::string::npos || s.find("compativel") != std::string::npos) return AiProvider::Custom;
    return AiProvider::Gemini;
}

void AiAssistantManager::SetCustomEndpoint(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_customEndpoint = StringUtils::Trim(endpoint);
}

std::string AiAssistantManager::GetCustomEndpoint() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_customEndpoint;
}

void AiAssistantManager::SetSelectedModel(const std::string& model) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedModel = StringUtils::Trim(model);
}

std::string AiAssistantManager::GetSelectedModel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedModel;
}

void AiAssistantManager::SetPeriodicEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_periodicEnabled = enabled;
}

bool AiAssistantManager::IsPeriodicEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_periodicEnabled;
}

void AiAssistantManager::SetPeriodicIntervalMinutes(int minutes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_periodicIntervalMinutes = std::clamp(minutes, 5, 1440);
}

int AiAssistantManager::GetPeriodicIntervalMinutes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_periodicIntervalMinutes;
}

std::string AiAssistantManager::GetLastRecommendation() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastRecommendation;
}

void AiAssistantManager::TickPeriodicOptimization(const SystemMetricsSnapshot& snap) {
    bool enabled = false;
    int intervalMin = 30;
    std::string key;
    AiProvider prov = AiProvider::Gemini;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        enabled = m_periodicEnabled;
        intervalMin = m_periodicIntervalMinutes;
        key = m_apiKey;
        prov = m_provider;
    }

    if (!enabled) return;
    if (key.empty() && prov != AiProvider::Custom) return;

    static auto s_lastRun = std::chrono::steady_clock::now() - std::chrono::hours(24);
    auto now = std::chrono::steady_clock::now();
    auto elapsedMinutes = std::chrono::duration_cast<std::chrono::minutes>(now - s_lastRun).count();

    if (elapsedMinutes < intervalMin) {
        return;
    }
    s_lastRun = now;

    std::thread([this, snap]() {
        // Thread desanexada: uma excecao aqui escaparia para std::terminate e mataria o processo
        // silenciosamente (fail-fast 0xC0000409 sem WER). Capturar e logar mantem o motor vivo.
        try {
        OPIT_LOG_INFO("AiAssistantManager: Executando otimizacao estrategica periodica em background...");
        std::string report, err;
        bool ok = GenerateSystemOptimizationReport(snap, report, err);
        if (ok) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastRecommendation = report;
            }
            DatabaseManager::Instance().SetSetting("ai_last_recommendation", report);
            DatabaseManager::Instance().LogEvent("AI_STRATEGY", "Otimizacao estrategica periodica por IA concluida com sucesso.");
            OPIT_LOG_INFO("AiAssistantManager: Otimizacao estrategica periodica concluida com sucesso.");
            // Aviso no tray (balao) da nova recomendacao; cooldown de AiAdvice evita repeticao.
            std::string head = report.substr(0, report.find('\n'));
            if (head.size() > 180) head = head.substr(0, 180);
            NotificationManager::Instance().Notify(NotificationManager::Category::AiAdvice,
                                                   "Nova recomendacao da IA",
                                                   head.empty() ? "A IA concluiu uma nova analise estrategica do sistema." : head);
        } else {
            OPIT_LOG_WARN("AiAssistantManager: Falha na otimizacao periodica: " + err);
        }
        } catch (const std::exception& e) {
            OPIT_LOG_ERROR(std::string("AiAssistantManager: excecao na thread de otimizacao periodica (capturada): ") + e.what());
        } catch (...) {
            OPIT_LOG_ERROR("AiAssistantManager: excecao desconhecida na thread de otimizacao periodica (capturada).");
        }
    }).detach();
}

void AiAssistantManager::SaveSettings() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& db = DatabaseManager::Instance();
    if (m_apiKey.empty()) {
        db.SetSetting("ai_api_key", "");
    } else {
        std::string enc = ProtectApiKeyDpapi(m_apiKey);
        if (enc.empty()) {
            // Sem fallback para texto puro: melhor a chave sumir do que vazar em claro no banco
            db.SetSetting("ai_api_key", "");
            OPIT_LOG_ERROR("AiAssistantManager: Falha DPAPI ao proteger a chave de API; chave NAO foi persistida.");
        } else {
            db.SetSetting("ai_api_key", "DPAPI1:" + enc);
        }
    }
    db.SetSetting("ai_provider", std::to_string(static_cast<int>(m_provider)));
    db.SetSetting("ai_model", m_selectedModel);
    db.SetSetting("ai_custom_endpoint", m_customEndpoint);
    db.SetSetting("ai_periodic_enabled", m_periodicEnabled ? "1" : "0");
    db.SetSetting("ai_periodic_interval_min", std::to_string(m_periodicIntervalMinutes));
    db.SetSetting("ai_last_recommendation", m_lastRecommendation);
    OPIT_LOG_INFO("AiAssistantManager: Configuracoes de IA salvas no banco de dados.");
}

void AiAssistantManager::LoadSettings() {
    auto& db = DatabaseManager::Instance();
    std::string storedKey = db.GetSetting("ai_api_key", "");
    if (storedKey.rfind("DPAPI1:", 0) == 0) {
        m_apiKey = UnprotectApiKeyDpapi(storedKey.substr(7));
        if (m_apiKey.empty()) {
            OPIT_LOG_WARN("AiAssistantManager: chave de API protegida nao pôde ser descriptografada neste usuario/maquina. Reinsira a chave.");
        }
    } else if (!storedKey.empty()) {
        // Migração de legado: chave em texto puro -> protege agora e regrava no banco
        m_apiKey = storedKey;
        std::string enc = ProtectApiKeyDpapi(storedKey);
        if (!enc.empty()) {
            db.SetSetting("ai_api_key", "DPAPI1:" + enc);
            OPIT_LOG_INFO("AiAssistantManager: chave de API migrada para armazenamento protegido por DPAPI.");
        } else {
            OPIT_LOG_ERROR("AiAssistantManager: falha DPAPI na migracao da chave de API; texto puro removido do banco.");
            db.SetSetting("ai_api_key", "");
        }
    } else {
        m_apiKey.clear();
    }
    std::string provStr = db.GetSetting("ai_provider", "0");
    int pVal = 0;
    try {
        pVal = std::stoi(provStr);
    } catch (...) {}
    m_provider = static_cast<AiProvider>(std::clamp(pVal, 0, 5));
    m_customEndpoint = db.GetSetting("ai_custom_endpoint", "https://openrouter.ai/api/v1");
    m_selectedModel = db.GetSetting("ai_model", "");
    if (m_selectedModel.empty() || m_selectedModel.find("argon") != std::string::npos) {
        switch (m_provider) {
            case AiProvider::Gemini: m_selectedModel = "gemini-2.0-flash"; break;
            case AiProvider::OpenAi: m_selectedModel = "gpt-4o-mini"; break;
            case AiProvider::Anthropic: m_selectedModel = "claude-3-5-sonnet-20241022"; break;
            case AiProvider::DeepSeek: m_selectedModel = "deepseek-chat"; break;
            case AiProvider::Groq: m_selectedModel = "llama-3.3-70b-versatile"; break;
            case AiProvider::Custom: m_selectedModel = "gpt-4o-mini"; break;
        }
    }
    m_periodicEnabled = (db.GetSetting("ai_periodic_enabled", "0") == "1");
    std::string intStr = db.GetSetting("ai_periodic_interval_min", "30");
    try {
        m_periodicIntervalMinutes = std::clamp(std::stoi(intStr), 5, 1440);
    } catch (...) {
        m_periodicIntervalMinutes = 30;
    }
    m_lastRecommendation = db.GetSetting("ai_last_recommendation", "");
}

bool AiAssistantManager::FetchAvailableModels(std::vector<AiModelInfo>& outModels, std::string& outError) {
    outModels.clear();
    std::string key;
    AiProvider prov;
    std::string customEp;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        key = m_apiKey;
        prov = m_provider;
        customEp = m_customEndpoint;
    }

    if (key.empty() && prov != AiProvider::Custom) {
        outError = "Por favor, insira a sua Chave de API antes de buscar os modelos.";
        return false;
    }

    if (prov == AiProvider::Gemini) {
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models";
        auto res = ExecuteHttp("GET", url, "", { "x-goog-api-key: " + key });
        if (!res.ok) {
            outError = "Falha ao consultar modelos do Google Gemini (HTTP " + std::to_string(res.status) + "): " + res.body;
            return false;
        }

        try {
            auto j = nlohmann::json::parse(res.body);
            if (j.contains("models") && j["models"].is_array()) {
                std::vector<AiModelInfo> recommended;
                std::vector<AiModelInfo> others;

                for (const auto& m : j["models"]) {
                    // 1. Filtrar apenas modelos que suportam generateContent
                    bool supportsGenerate = false;
                    if (m.contains("supportedGenerationMethods") && m["supportedGenerationMethods"].is_array()) {
                        for (const auto& method : m["supportedGenerationMethods"]) {
                            if (method.is_string() && method.get<std::string>() == "generateContent") {
                                supportsGenerate = true;
                                break;
                            }
                        }
                    }
                    if (!supportsGenerate) continue;

                    std::string name = m.value("name", "");
                    std::string disp = m.value("displayName", name);
                    std::string cleanId = name;
                    if (cleanId.rfind("models/", 0) == 0) {
                        cleanId = cleanId.substr(7);
                    }

                    std::string lowerId = StringUtils::ToLower(cleanId);
                    // Ignorar modelos internos, de embedding ou experimentais sem cota pública
                    if (lowerId.find("argon") != std::string::npos ||
                        lowerId.find("internal") != std::string::npos ||
                        lowerId.find("embedding") != std::string::npos ||
                        lowerId.find("aqa") != std::string::npos) {
                        continue;
                    }

                    // Priorizar os modelos estáveis e recomendados no topo
                    if (lowerId == "gemini-2.0-flash" || lowerId == "gemini-1.5-flash" ||
                        lowerId == "gemini-1.5-pro" || lowerId == "gemini-2.5-flash") {
                        recommended.push_back({cleanId, disp + " (" + cleanId + ")"});
                    } else if (cleanId.find("gemini") != std::string::npos) {
                        others.push_back({cleanId, disp + " (" + cleanId + ")"});
                    }
                }

                outModels.insert(outModels.end(), recommended.begin(), recommended.end());
                outModels.insert(outModels.end(), others.begin(), others.end());
            }

            if (outModels.empty()) {
                outModels.push_back({"gemini-2.0-flash", "Gemini 2.0 Flash (Recomendado)"});
                outModels.push_back({"gemini-1.5-flash", "Gemini 1.5 Flash (Estavel)"});
                outModels.push_back({"gemini-1.5-pro", "Gemini 1.5 Pro"});
            }
        } catch (const std::exception& e) {
            outError = "Erro ao decodificar JSON do Gemini: " + std::string(e.what());
            return false;
        }
    } else if (prov == AiProvider::OpenAi || prov == AiProvider::DeepSeek || prov == AiProvider::Groq || prov == AiProvider::Custom) {
        std::string baseUrl;
        if (prov == AiProvider::OpenAi) baseUrl = "https://api.openai.com/v1";
        else if (prov == AiProvider::DeepSeek) baseUrl = "https://api.deepseek.com";
        else if (prov == AiProvider::Groq) baseUrl = "https://api.groq.com/openai/v1";
        else baseUrl = customEp;

        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

        std::string url = baseUrl + "/models";
        std::vector<std::string> headers;
        if (!key.empty()) {
            headers.push_back("Authorization: Bearer " + key);
        }

        auto res = ExecuteHttp("GET", url, "", headers);
        if (!res.ok) {
            outError = "Falha ao consultar modelos (" + ProviderToString(prov) + ") [HTTP " + std::to_string(res.status) + "]: " + res.body;
            return false;
        }

        try {
            auto j = nlohmann::json::parse(res.body);
            auto dataArr = j.contains("data") ? j["data"] : (j.contains("models") ? j["models"] : nlohmann::json::array());
            for (const auto& m : dataArr) {
                std::string id = m.value("id", m.value("name", ""));
                if (!id.empty()) {
                    outModels.push_back({id, id});
                }
            }
        } catch (const std::exception& e) {
            outError = "Erro ao decodificar lista de modelos: " + std::string(e.what());
            return false;
        }
    } else if (prov == AiProvider::Anthropic) {
        std::string url = "https://api.anthropic.com/v1/models";
        std::vector<std::string> headers = {
            "x-api-key: " + key,
            "anthropic-version: 2023-06-01"
        };
        auto res = ExecuteHttp("GET", url, "", headers);
        if (res.ok) {
            try {
                auto j = nlohmann::json::parse(res.body);
                if (j.contains("data") && j["data"].is_array()) {
                    for (const auto& m : j["data"]) {
                        std::string id = m.value("id", "");
                        std::string name = m.value("display_name", id);
                        if (!id.empty()) {
                            outModels.push_back({id, name + " (" + id + ")"});
                        }
                    }
                }
            } catch (...) {}
        }
        // Se a API da Anthropic nao suportar /models na chave ou estiver vazia, preenche os modelos Claude oficiais
        if (outModels.empty()) {
            outModels = {
                {"claude-3-5-sonnet-20241022", "Claude 3.5 Sonnet (Recomendado)"},
                {"claude-3-5-haiku-20241022", "Claude 3.5 Haiku (Rapido)"},
                {"claude-3-opus-20240229", "Claude 3 Opus (Avancado)"},
                {"claude-3-haiku-20240307", "Claude 3 Haiku"}
            };
        }
    }

    if (outModels.empty()) {
        outError = "Nenhum modelo compativel retornado pelo provedor para esta chave.";
        return false;
    }

    // Ordenacao por nome para facilitar escolha do usuario
    std::sort(outModels.begin(), outModels.end(), [](const AiModelInfo& a, const AiModelInfo& b) {
        return a.displayName < b.displayName;
    });

    return true;
}

bool AiAssistantManager::Query(const std::string& prompt, std::string& outResponse, std::string& outError) {
    std::string key;
    AiProvider prov;
    std::string model;
    std::string customEp;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        key = m_apiKey;
        prov = m_provider;
        model = m_selectedModel;
        customEp = m_customEndpoint;
    }

    if (key.empty() && prov != AiProvider::Custom) {
        outError = "Chave de API nao configurada. Por favor, insira sua chave nas configuracoes do assistente.";
        return false;
    }
    if (model.empty()) {
        outError = "Nenhum modelo de IA selecionado. Clique em 'Buscar Modelos' e escolha um.";
        return false;
    }

    std::string effectivePrompt = prompt;
    if (prompt.find("Opit-Core — Hardware Intelligence") == std::string::npos && prompt.find("opit_action") == std::string::npos) {
        std::ostringstream sys;
        sys << "Voce e o Copiloto Especialista em Hardware do Opit-Core. Voce conhece todas as ferramentas e configuracoes do programa:\n"
            << "- Perfis: 'Eco', 'Balanced', 'HighPerformance'\n"
            << "- CPU: Min e Max (AC/DC de 5 a 100%), Turbo Boost (true/false)\n"
            << "- Core Parking: porcentagem de nucleos ativos (ex: 25 a 100)\n"
            << "- GPU: Preferencia (0=Auto, 1=Integrada/Eco, 2=Dedicada/HighPerformance), Baixa Latencia DXGI (true/false)\n"
            << "- Termica: Limite de alerta (65 a 95 C)\n"
            << "- Servicos: regras individuais ('auto', 'pause_on_game', 'pause_on_eco', 'pause_on_high_cpu', 'always_paused', 'never_pause') para WSearch, SysMain, Spooler, DiagTrack e atualizadores de terceiros.\n";

        auto liveDiag = AdaptiveEngine::Instance().GetLatestDiagnosis();
        if (liveDiag.primaryType != BottleneckType::None) {
            sys << "- DIAGNOSTICO ATIVO DE GARGALO NO HARDWARE: " << BottleneckTypeToString(liveDiag.primaryType)
                << " (" << DiagnosisConfidenceToString(liveDiag.confidence) << ") — "
                << liveDiag.justification << "\n";
            if (!liveDiag.supportingEvidence.empty()) {
                sys << "  Evidencia do hardware: " << liveDiag.supportingEvidence[0] << "\n";
            }
        }

        sys << "Responda de forma direta e estruturada em Portugues do Brasil.\n"
            << "SE o usuario pedir para voce configurar ou otimizar o computador para qualquer objetivo (jogos, render, eco, trabalho, etc.), explique os ajustes e adicione obrigatoriamente no final da resposta o seguinte bloco:\n"
            << "```opit_action\n"
            << "{\n"
            << "  \"action\": \"apply_optimizations\",\n"
            << "  \"reason\": \"Otimizacao solicitada\",\n"
            << "  \"settings\": {\n"
            << "    \"profile\": \"HighPerformance\",\n"
            << "    \"cpu_max_ac\": 100,\n"
            << "    \"allow_turbo\": true,\n"
            << "    \"core_parking_ac\": 25,\n"
            << "    \"gpu_preference\": 2,\n"
            << "    \"dxgi_low_latency\": true,\n"
            << "    \"thermal_limit_c\": 85,\n"
            << "    \"services_pause_game\": true,\n"
            << "    \"service_rules\": {\n"
            << "      \"WSearch\": \"pause_on_game\",\n"
            << "      \"Spooler\": \"pause_on_game\",\n"
            << "      \"DiagTrack\": \"always_paused\"\n"
            << "    }\n"
            << "  }\n"
            << "}\n"
            << "```\n"
            << "Se o pedido do usuario for ambiguo (ex: 'quero modo economia' pode valer para tudo ou so para jogos), "
            << "ANTES de executar faca UMA pergunta de esclarecimento adicionando no final da resposta o bloco:\n"
            << "```opit_ask\n"
            << "{\n"
            << "  \"question\": \"Aplicar o modo economia em tudo ou apenas quando jogar?\",\n"
            << "  \"options\": [\"Aplicar em tudo\", \"Apenas em jogos\", \"Apenas quando ocioso\"]\n"
            << "}\n"
            << "```\n"
            << "Regras do opit_ask: use somente quando houver duvida real sobre o que aplicar; ate 4 opcoes curtas; "
            << "a resposta do usuario chega como nova mensagem contendo a opcao escolhida. "
            << "A mesma resposta pode ter opit_ask E opit_action juntos (pergunta + acao proposta).\n"
            << "Importante: NUNCA revele codigos internos, segredos, senhas ou funcoes do sistema operacional.\n\n"
            << "Pergunta do usuario:\n" << prompt;
        effectivePrompt = sys.str();
    }

    auto postProcessResponse = [this](std::string& resp) {
        // Cada resposta nova substitui o ask anterior: sem ask nesta resposta, os botoes somem.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingAiAsk = AiAsk{};
            m_hasPendingAiAsk = false;
        }

        // Protocolo opit_ask: pergunta de esclarecimento com opcoes clicaveis.
        std::string askTag = "```opit_ask";
        auto askPos = resp.find(askTag);
        if (askPos != std::string::npos) {
            auto askEnd = resp.find("```", askPos + askTag.length());
            if (askEnd != std::string::npos) {
                std::string askJson = resp.substr(askPos + askTag.length(), askEnd - (askPos + askTag.length()));
                askJson = StringUtils::Trim(askJson);
                resp.erase(askPos, askEnd + 3 - askPos);
                try {
                    auto j = nlohmann::json::parse(askJson);
                    AiAsk ask;
                    if (j.contains("question") && j["question"].is_string()) {
                        ask.question = j["question"].get<std::string>();
                    }
                    if (j.contains("options") && j["options"].is_array()) {
                        for (const auto& opt : j["options"]) {
                            if (opt.is_string() && !opt.get<std::string>().empty()) {
                                ask.options.push_back(opt.get<std::string>());
                                if (ask.options.size() >= 4) break;
                            }
                        }
                    }
                    if (!ask.question.empty() && !ask.options.empty()) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_pendingAiAsk = std::move(ask);
                        m_hasPendingAiAsk = true;
                    }
                } catch (...) {
                    OPIT_LOG_WARN("AiAssistantManager: bloco opit_ask recebido com JSON invalido; ignorado.");
                }
            }
        }

        std::string actionTag = "```opit_action";
        auto actPos = resp.find(actionTag);
        if (actPos != std::string::npos) {
            auto endPos = resp.find("```", actPos + actionTag.length());
            if (endPos != std::string::npos) {
                std::string actionJson = resp.substr(actPos + actionTag.length(), endPos - (actPos + actionTag.length()));
                actionJson = StringUtils::Trim(actionJson);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_pendingAiActionJson = actionJson;
                }
                const auto& adv = ModuleAdvancedSettings::Instance().services;
                if (adv.aiAutonomyLevel == 2) {
                    std::string summary, execErr;
                    if (ExecuteAiAction(actionJson, summary, execErr)) {
                        resp += "\n\n⚡ **[Acao Automatica Aplicada com Sucesso pelo Opit-Core]**:\n" + summary;
                    } else {
                        resp += "\n\n⚠️ **[Falha ao aplicar acao automatica]**: " + execErr;
                    }
                }
            }
        }
    };

    if (prov == AiProvider::Gemini) {
        std::string cleanModel = model;
        if (cleanModel.rfind("models/", 0) == 0) cleanModel = cleanModel.substr(7);

        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + cleanModel + ":generateContent";
        const std::vector<std::string> geminiHeaders = { "x-goog-api-key: " + key };
        nlohmann::json req;
        req["contents"] = nlohmann::json::array();
        nlohmann::json contentObj;
        contentObj["parts"] = nlohmann::json::array({{{"text", effectivePrompt}}});
        req["contents"].push_back(contentObj);

        auto res = ExecuteHttp("POST", url, req.dump(), geminiHeaders);

        // Auto-fallback inteligente se o modelo não existir (404), for inválido (400) ou atingir cota 0 (429)
        if (!res.ok && (res.status == 404 || res.status == 400 || (res.status == 429 && res.body.find("limit: 0") != std::string::npos))) {
            if (cleanModel != "gemini-2.0-flash" && cleanModel != "gemini-1.5-flash") {
                OPIT_LOG_WARN("AiAssistantManager: Modelo '" + cleanModel + "' retornou HTTP " + std::to_string(res.status) + ". Tentando fallback para 'gemini-2.0-flash'...");
                cleanModel = "gemini-2.0-flash";
                url = "https://generativelanguage.googleapis.com/v1beta/models/" + cleanModel + ":generateContent";
                res = ExecuteHttp("POST", url, req.dump(), geminiHeaders);
                if (res.ok) {
                    SetSelectedModel("gemini-2.0-flash");
                } else if (res.status == 404 || res.status == 429) {
                    cleanModel = "gemini-1.5-flash";
                    url = "https://generativelanguage.googleapis.com/v1beta/models/" + cleanModel + ":generateContent";
                    res = ExecuteHttp("POST", url, req.dump(), geminiHeaders);
                    if (res.ok) {
                        SetSelectedModel("gemini-1.5-flash");
                    }
                }
            }
        }

        if (!res.ok) {
            outError = "Falha no Google Gemini (HTTP " + std::to_string(res.status) + "): " + res.body;
            return false;
        }

        try {
            auto j = nlohmann::json::parse(res.body);
            if (j.contains("candidates") && !j["candidates"].empty()) {
                auto& cand = j["candidates"][0];
                if (cand.contains("content") && cand["content"].contains("parts") && !cand["content"]["parts"].empty()) {
                    outResponse = cand["content"]["parts"][0]["text"].get<std::string>();
                    postProcessResponse(outResponse);
                    return true;
                }
            }
            if (j.contains("error")) {
                outError = "Erro Gemini: " + j["error"]["message"].get<std::string>();
                return false;
            }
            outError = "Resposta em formato inesperado: " + res.body;
            return false;
        } catch (const std::exception& e) {
            outError = "Falha ao decodificar JSON: " + std::string(e.what());
            return false;
        }
    } else if (prov == AiProvider::Anthropic) {
        std::string url = "https://api.anthropic.com/v1/messages";
        nlohmann::json req;
        req["model"] = model;
        req["max_tokens"] = 1500;
        req["messages"] = nlohmann::json::array();
        req["messages"].push_back({
            {"role", "user"},
            {"content", effectivePrompt}
        });

        std::vector<std::string> headers = {
            "x-api-key: " + key,
            "anthropic-version: 2023-06-01"
        };

        auto res = ExecuteHttp("POST", url, req.dump(), headers);
        if (!res.ok) {
            outError = "Falha na API da Anthropic Claude (HTTP " + std::to_string(res.status) + "): " + res.body;
            return false;
        }

        try {
            auto j = nlohmann::json::parse(res.body);
            if (j.contains("content") && j["content"].is_array() && !j["content"].empty()) {
                outResponse = j["content"][0].value("text", "");
                postProcessResponse(outResponse);
                return true;
            }
            if (j.contains("error")) {
                outError = "Erro Claude: " + j["error"]["message"].get<std::string>();
                return false;
            }
            outError = "Resposta inesperada da Anthropic: " + res.body;
            return false;
        } catch (const std::exception& e) {
            outError = "Falha ao processar JSON: " + std::string(e.what());
            return false;
        }
    } else {
        // OpenAI / DeepSeek / Groq / Custom
        std::string baseUrl;
        if (prov == AiProvider::OpenAi) baseUrl = "https://api.openai.com/v1";
        else if (prov == AiProvider::DeepSeek) baseUrl = "https://api.deepseek.com";
        else if (prov == AiProvider::Groq) baseUrl = "https://api.groq.com/openai/v1";
        else baseUrl = customEp;

        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
        std::string url = baseUrl + "/chat/completions";

        nlohmann::json req;
        req["model"] = model;
        req["messages"] = nlohmann::json::array();
        req["messages"].push_back({
            {"role", "system"},
            {"content", "Voce e o assistente tecnico de otimizacao e hardware integrado ao Opit-Core. Responda em portugues de forma clara, tecnica e direta."}
        });
        req["messages"].push_back({
            {"role", "user"},
            {"content", effectivePrompt}
        });

        std::vector<std::string> headers;
        if (!key.empty()) {
            headers.push_back("Authorization: Bearer " + key);
        }

        auto res = ExecuteHttp("POST", url, req.dump(), headers);
        if (!res.ok) {
            outError = "Falha no provedor " + ProviderToString(prov) + " (HTTP " + std::to_string(res.status) + "): " + res.body;
            return false;
        }

        try {
            auto j = nlohmann::json::parse(res.body);
            if (j.contains("choices") && !j["choices"].empty()) {
                auto& choice = j["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content")) {
                    outResponse = choice["message"]["content"].get<std::string>();
                    postProcessResponse(outResponse);
                    return true;
                }
            }
            if (j.contains("error")) {
                outError = "Erro: " + j["error"]["message"].get<std::string>();
                return false;
            }
            outError = "Resposta em formato inesperado: " + res.body;
            return false;
        } catch (const std::exception& e) {
            outError = "Falha ao processar resposta JSON: " + std::string(e.what());
            return false;
        }
    }
}

bool AiAssistantManager::GenerateSystemOptimizationReport(const SystemMetricsSnapshot& snap, std::string& outReport, std::string& outError) {
    std::ostringstream ss;
    ss << "# Opit-Core — Hardware Intelligence & Safe Orchestration\n\n";
    ss << "Voce e o mecanismo de inteligencia do Opit-Core especializado em analise de hardware, eficiencia energetica, desempenho, estabilidade e orquestracao segura.\n";
    ss << "Sua missao e analisar as caracteristicas reais do computador, respeitando estritamente a HIERARQUIA DE CONFIANCA:\n";
    ss << "1. Integridade do sistema e seguranca do hardware;\n";
    ss << "2. Estabilidade do sistema operacional e prevencao de falhas;\n";
    ss << "3. Integridade das politicas de seguranca do Opit-Core;\n";
    ss << "4. Respeito as preferencias explícitas do usuario;\n";
    ss << "5. Eficiencia energetica e controle termico;\n";
    ss << "6. Desempenho e responsividade adequados.\n\n";

    ss << "=== TELEMETRIA E METRICAS DO HARDWARE EM TEMPO REAL ===\n";
    ss << "- CPU: " << snap.cpuUsagePercent << "% de uso atual\n";
    if (snap.hasTempSensor) {
        ss << "- Temperatura da CPU: " << snap.cpuTemperatureC << " C (Fonte: " << snap.tempSensorSource << ")\n";
    } else {
        ss << "- Temperatura da CPU: Sensor ACPI nao reportado diretamente\n";
    }
    if (snap.hasFanRpm) {
        ss << "- Cooler / Ventoinha: " << snap.fanRpm << " RPM\n";
    }
    if (snap.ramTotalMB > 0) {
        ss << "- Memoria RAM: " << snap.ramUsedMB << " MB em uso de " << snap.ramTotalMB << " MB (" << snap.ramUsagePercent << "%)\n";
    }

    auto& gpuMgr = GpuControlManager::Instance();
    if (gpuMgr.GetGpuCount() > 0) {
        auto caps = gpuMgr.GetCapabilities(0);
        auto stat = gpuMgr.GetStatus(0);
        ss << "- GPU: " << caps.adapterName << " (" << GpuVendorToString(caps.vendor) << ")\n";
        ss << "  * Perfil GPU: " << GpuPowerProfileToString(stat.currentProfile) << "\n";
        ss << "  * Power Limit: " << stat.appliedPowerLimitPercent << "%\n";
        ss << "  * Core Clock Offset: " << stat.appliedCoreClockOffsetMHz << " MHz\n";
        ss << "  * Memory Clock Offset: " << stat.appliedMemoryClockOffsetMHz << " MHz\n";
    }

    ss << "- Plano de Energia Ativo: " << snap.activePowerSchemeName << " (" << snap.activeProfileName << ")\n";
    ss << "- Estado de Jogos: " << (snap.isGameRunning ? ("Jogo Detectado em Execucao: " + snap.runningGameName) : "Desktop / Geral Ocioso") << "\n";
    ss << "- Alerta Termico (Throttling): " << (snap.isThermalThrottling ? "ALERTA (Throttling ativo)" : "Normal") << "\n";

    auto diag = AdaptiveEngine::Instance().GetLatestDiagnosis();
    if (diag.primaryType != BottleneckType::None) {
        ss << "- Diagnostico Ativo de Gargalo: " << BottleneckTypeToString(diag.primaryType)
           << " (" << DiagnosisConfidenceToString(diag.confidence) << ")\n";
        ss << "  * Justificativa: " << diag.justification << "\n";
        for (const auto& ev : diag.supportingEvidence) {
            ss << "  * Evidencia: " << ev << "\n";
        }
    } else {
        ss << "- Diagnostico Ativo de Gargalo: Nenhum (Hardware operando sem restricoes ou gargalos criticos detectados)\n";
    }
    ss << "\n";

    ss << "Por favor, responda com texto estruturado, limpo e direto nas seguintes secoes numeradas:\n";
    ss << "## 1. DIAGNOSTICO DO HARDWARE E CONDICOES ATUAIS\n";
    ss << "Avalie se ha gargalos de processamento, superaquecimento ou pressao sobre a RAM.\n\n";
    ss << "## 2. RECOMENDACOES DE CPU E CORE PARKING\n";
    ss << "Sugira percentuais de Min/Max de CPU e se o estacionamento de nucleos ociosos deve ser intensificado para economizar energia ou liberado para tarefas pesadas.\n\n";
    ss << "## 3. OTIMIZACAO DE GPU, ENERGIA E SERVICOS DO WINDOWS\n";
    ss << "Sugira como dosar os servicos em segundo plano e a GPU para manter o sistema estavel e eficiente.\n\n";
    ss << "## 4. DIRETRIZES DE SEGURANCA E ESTABILIDADE\n";
    ss << "Reforce que nenhuma alteracao deve violar os limites termicos ou eletricos do hardware.\n";

    return Query(ss.str(), outReport, outError);
}

bool AiAssistantManager::HasPendingAiAction() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pendingAiActionJson.empty();
}

std::string AiAssistantManager::GetPendingAiActionJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pendingAiActionJson;
}

void AiAssistantManager::ClearPendingAiAction() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingAiActionJson.clear();
}

bool AiAssistantManager::HasPendingAiAsk() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasPendingAiAsk;
}

AiAsk AiAssistantManager::GetPendingAiAsk() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pendingAiAsk;
}

void AiAssistantManager::ClearPendingAiAsk() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingAiAsk = AiAsk{};
    m_hasPendingAiAsk = false;
}

bool AiAssistantManager::CanRollbackAiAction() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastSnapshot.hasSnapshot;
}

std::string AiAssistantManager::GetLastActionSummary() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastActionSummary;
}

bool AiAssistantManager::ExecuteAiAction(const std::string& actionJson, std::string& outSummary, std::string& outError) {
    try {
        auto j = nlohmann::json::parse(actionJson);
        if (!j.contains("settings")) {
            outError = "Bloco de acao invalido: campo 'settings' ausente.";
            return false;
        }

        auto& adv = ModuleAdvancedSettings::Instance();
        auto& svcMgr = ServiceManager::Instance();

        // 1. Snapshot defensivo antes de alterar
        AiActionSnapshot snap;
        snap.hasSnapshot = true;
        snap.previousProfile = Config::ProfileToString(Config::Instance().GetProfile());
        snap.prevCpuMinAC = adv.cpuMinMax.minPercentAC;
        snap.prevCpuMaxAC = adv.cpuMinMax.maxPercentAC;
        snap.prevCpuMinDC = adv.cpuMinMax.minPercentDC;
        snap.prevCpuMaxDC = adv.cpuMinMax.maxPercentDC;
        snap.prevAllowTurbo = adv.cpuMinMax.allowTurboBoost;
        snap.prevCoreParkingAC = adv.coreParking.minCoresActiveAC;
        snap.prevCoreParkingDC = adv.coreParking.minCoresActiveDC;
        snap.prevGpuPref = adv.gpu.graphicsPreference;
        snap.prevDxgiLowLatency = adv.gpu.dxgiLowLatency;
        snap.prevThermalLimit = adv.thermal.alertLimitC;
        snap.prevServiceRules = svcMgr.GetAllCustomRules();
        snap.actionDescription = j.value("reason", "Ajuste de otimizacao por IA");

        std::vector<std::string> appliedChanges;
        const auto& s = j["settings"];

        // Perfil global (se solicitado)
        if (s.contains("profile") && s["profile"].is_string()) {
            std::string profStr = s["profile"].get<std::string>();
            if (profStr == "Eco") {
                CoreRuntime::Instance().ApplyProfile(Config::Profile::Eco);
                appliedChanges.push_back("Perfil Geral: Economia de Energia");
            } else if (profStr == "HighPerformance" || profStr == "Gamer") {
                CoreRuntime::Instance().ApplyProfile(Config::Profile::HighPerformance);
                appliedChanges.push_back("Perfil Geral: Alto Desempenho");
            } else if (profStr == "Balanced") {
                CoreRuntime::Instance().ApplyProfile(Config::Profile::Balanced);
                appliedChanges.push_back("Perfil Geral: Equilibrado");
            }
        }

        // CPU Min / Max
        if (s.contains("cpu_max_ac") && s["cpu_max_ac"].is_number()) {
            uint32_t val = std::clamp(s["cpu_max_ac"].get<uint32_t>(), 50u, 100u);
            adv.cpuMinMax.maxPercentAC = val;
            appliedChanges.push_back("CPU Max AC: " + std::to_string(val) + "%");
        }
        if (s.contains("cpu_max_dc") && s["cpu_max_dc"].is_number()) {
            uint32_t val = std::clamp(s["cpu_max_dc"].get<uint32_t>(), 30u, 100u);
            adv.cpuMinMax.maxPercentDC = val;
            appliedChanges.push_back("CPU Max DC (Bateria): " + std::to_string(val) + "%");
        }
        if (s.contains("allow_turbo") && s["allow_turbo"].is_boolean()) {
            adv.cpuMinMax.allowTurboBoost = s["allow_turbo"].get<bool>();
            appliedChanges.push_back(std::string("Turbo Boost: ") + (adv.cpuMinMax.allowTurboBoost ? "Ativado" : "Desativado"));
        }

        // Core Parking
        if (s.contains("core_parking_ac") && s["core_parking_ac"].is_number()) {
            uint32_t val = std::clamp(s["core_parking_ac"].get<uint32_t>(), 0u, 100u);
            adv.coreParking.minCoresActiveAC = val;
            appliedChanges.push_back("Nucleos Ativos (Core Parking): " + std::to_string(val) + "%");
        }

        // GPU
        if (s.contains("gpu_preference") && s["gpu_preference"].is_number()) {
            adv.gpu.graphicsPreference = std::clamp(s["gpu_preference"].get<int>(), 0, 2);
            appliedChanges.push_back("Preferencia Grafica GPU atualizada");
        }
        if (s.contains("dxgi_low_latency") && s["dxgi_low_latency"].is_boolean()) {
            adv.gpu.dxgiLowLatency = s["dxgi_low_latency"].get<bool>();
            appliedChanges.push_back("Baixa Latencia DXGI: Ativada");
        }

        // Térmica segura
        if (s.contains("thermal_limit_c") && s["thermal_limit_c"].is_number()) {
            double tLimit = std::clamp(s["thermal_limit_c"].get<double>(), 65.0, 95.0);
            adv.thermal.alertLimitC = tLimit;
            appliedChanges.push_back("Limite Termico de Seguranca: " + std::to_string((int)tLimit) + " C");
        }

        // Gatilhos de serviços
        if (s.contains("services_pause_game") && s["services_pause_game"].is_boolean()) {
            adv.services.pauseOnGame = s["services_pause_game"].get<bool>();
            appliedChanges.push_back(std::string("Pausar servicos em jogos: ") + (adv.services.pauseOnGame ? "Sim" : "Nao"));
        }
        if (s.contains("services_pause_highcpu") && s["services_pause_highcpu"].is_boolean()) {
            adv.services.pauseOnHighCpu = s["services_pause_highcpu"].get<bool>();
            appliedChanges.push_back(std::string("Pausar servicos sob alta CPU: ") + (adv.services.pauseOnHighCpu ? "Sim" : "Nao"));
        }

        // Regras individuais de serviços (com blindagem rigorosa contra críticos)
        if (s.contains("service_rules") && s["service_rules"].is_object()) {
            for (auto it = s["service_rules"].begin(); it != s["service_rules"].end(); ++it) {
                std::string svc = it.key();
                std::string rule = it.value().get<std::string>();
                if (!svcMgr.IsCriticalService(svc) && adv.services.IsServiceAllowed(svc)) {
                    svcMgr.SetServiceRule(svc, rule);
                    appliedChanges.push_back("Servico " + svc + " configurado para [" + rule + "]");
                }
            }
        }

        adv.Save();
        CoreRuntime::Instance().ApplyUnifiedSettings();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastSnapshot = snap;
            std::ostringstream ssSummary;
            ssSummary << "Otimizacoes aplicadas com sucesso pela IA (" << snap.actionDescription << "):\n";
            for (const auto& c : appliedChanges) {
                ssSummary << " • " << c << "\n";
            }
            m_lastActionSummary = ssSummary.str();
            m_pendingAiActionJson.clear();
            outSummary = m_lastActionSummary;
        }

        DatabaseManager::Instance().LogEvent("AI_ORCHESTRATION", "Acoes de otimizacao aplicadas pela IA com sucesso.");
        OPIT_LOG_INFO("AiAssistantManager: Acoes de configuracao aplicadas com sucesso pelo despachante.");
        return true;
    } catch (const std::exception& ex) {
        outError = std::string("Erro ao processar acao da IA: ") + ex.what();
        OPIT_LOG_WARN("AiAssistantManager: " + outError);
        return false;
    }
}

bool AiAssistantManager::RollbackLastAiAction(std::string& outSummary, std::string& outError) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_lastSnapshot.hasSnapshot) {
        outError = "Nao ha acao anterior registrada para desfazer.";
        return false;
    }

    auto& adv = ModuleAdvancedSettings::Instance();
    auto& svcMgr = ServiceManager::Instance();

    adv.cpuMinMax.minPercentAC = m_lastSnapshot.prevCpuMinAC;
    adv.cpuMinMax.maxPercentAC = m_lastSnapshot.prevCpuMaxAC;
    adv.cpuMinMax.minPercentDC = m_lastSnapshot.prevCpuMinDC;
    adv.cpuMinMax.maxPercentDC = m_lastSnapshot.prevCpuMaxDC;
    adv.cpuMinMax.allowTurboBoost = m_lastSnapshot.prevAllowTurbo;
    adv.coreParking.minCoresActiveAC = m_lastSnapshot.prevCoreParkingAC;
    adv.coreParking.minCoresActiveDC = m_lastSnapshot.prevCoreParkingDC;
    adv.gpu.graphicsPreference = m_lastSnapshot.prevGpuPref;
    adv.gpu.dxgiLowLatency = m_lastSnapshot.prevDxgiLowLatency;
    adv.thermal.alertLimitC = m_lastSnapshot.prevThermalLimit;

    for (const auto& kv : m_lastSnapshot.prevServiceRules) {
        svcMgr.SetServiceRule(kv.first, kv.second);
    }

    adv.Save();
    CoreRuntime::Instance().ApplyUnifiedSettings();
    svcMgr.RestoreAllServicesNow();

    m_lastSnapshot.hasSnapshot = false;
    outSummary = "Todas as configuracoes foram revertidas com sucesso para o estado anterior.";
    DatabaseManager::Instance().LogEvent("AI_ORCHESTRATION", "Reversao de acoes da IA concluida com sucesso.");
    OPIT_LOG_INFO("AiAssistantManager: Rollback de acao da IA concluido.");
    return true;
}

bool AiAssistantManager::ExecuteAiAction(std::string& outFeedback) {
    std::string json = GetPendingAiActionJson();
    std::string err;
    bool ok = ExecuteAiAction(json, outFeedback, err);
    if (!ok && !err.empty()) {
        outFeedback = err;
    }
    return ok;
}

bool AiAssistantManager::RollbackLastAiAction(std::string& outFeedback) {
    std::string err;
    bool ok = RollbackLastAiAction(outFeedback, err);
    if (!ok && !err.empty()) {
        outFeedback = err;
    }
    return ok;
}

} // namespace opitcore
