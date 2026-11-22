#include "PowerManager.h"
#include "utils/StringUtils.h"
#include "platform/windows/ElevationHelper.h"
#include <windows.h>
#include <objbase.h>
#include <powrprof.h>
#include <initguid.h>
#include <cstdio>
#include <vector>
#include <string>

#pragma comment(lib, "powrprof.lib")

// GUIDs de subgrupo e configuração de processador
// Subgrupo de configurações de processador: 54533251-82be-4824-96c1-47b60b740d00
DEFINE_GUID(OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, 0x54533251, 0x82be, 0x4824, 0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00);
// Estado de desempenho máximo do processador: bc5038f7-23e0-4960-96da-33abaf5935ec
DEFINE_GUID(OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, 0xbc5038f7, 0x23e0, 0x4960, 0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec);
// Estado de desempenho mínimo do processador (PROCTHROTTLEMIN oficial Microsoft): 893dee8e-2bef-41e0-89c6-b55d0929964c
DEFINE_GUID(OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, 0x893dee8e, 0x2bef, 0x41e0, 0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c);
// Core Parking - Mínimo de núcleos desestacionados (CPMINCORES Classe 0): 0cc5b647-c1df-4637-891a-dec35c318583
DEFINE_GUID(OPIT_GUID_CORE_PARKING_MIN_CORES, 0x0cc5b647, 0xc1df, 0x4637, 0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83);
// Core Parking - Mínimo de núcleos Classe 1 (CPMINCORES1): 0cc5b647-c1df-4637-891a-dec35c318584
DEFINE_GUID(OPIT_GUID_CORE_PARKING_MIN_CORES1, 0x0cc5b647, 0xc1df, 0x4637, 0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x84);
// Core Parking - Máximo de núcleos desestacionados (CPMAXCORES Classe 0): ea062031-0e34-4ff1-9b6d-eb1059334028
DEFINE_GUID(OPIT_GUID_CORE_PARKING_MAX_CORES, 0xea062031, 0x0e34, 0x4ff1, 0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x28);
// Core Parking - Máximo de núcleos Classe 1 (CPMAXCORES1): ea062031-0e34-4ff1-9b6d-eb1059334029
DEFINE_GUID(OPIT_GUID_CORE_PARKING_MAX_CORES1, 0xea062031, 0x0e34, 0x4ff1, 0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x29);

// Subgrupo de configurações de disco: 0012ee47-9041-4b5d-9b77-535fba8b1442
DEFINE_GUID(OPIT_GUID_DISK_SUBGROUP, 0x0012ee47, 0x9041, 0x4b5d, 0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42);
// Desligar disco rígido após inatividade (segundos; 0 = Nunca): 6735422d-22ee-4e4c-9721-29c6f1330cad
DEFINE_GUID(OPIT_GUID_DISK_POWERDOWN_TIMEOUT, 0x6735422d, 0x22ee, 0x4e4c, 0x97, 0x21, 0x29, 0xc6, 0xf1, 0x33, 0x0c, 0xad);

// Subgrupo USB e Suspensão Seletiva USB (proteção de teclado/mouse/controladoras)
DEFINE_GUID(OPIT_GUID_USB_SUBGROUP, 0x2a737441, 0x1930, 0x4402, 0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3);
DEFINE_GUID(OPIT_GUID_USB_SELECTIVE_SUSPEND, 0x48e6b7a6, 0x50f5, 0x4782, 0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26);

// Subgrupo PCI Express e ASPM (proteção do link de chipset X370/AM4 contra queda de barramento)
DEFINE_GUID(OPIT_GUID_PCIEXPRESS_SUBGROUP, 0x501a4d13, 0x42af, 0x4429, 0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20);
DEFINE_GUID(OPIT_GUID_PCIEXPRESS_ASPM, 0xee12f906, 0xd277, 0x404b, 0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5);

namespace opitcore {

// Configura o comportamento de periféricos USB e barramentos PCIe (ASPM) no esquema.
// Se pauseIdleDevices for true, ativa a suspensão de periféricos ociosos para economia máxima.
// Se false, garante que portas USB e link PCIe nunca caiam, protegendo teclado, mouse e estabilidade do barramento.
static void ConfigurePeripheralsOnScheme(const GUID* pScheme, bool pauseIdleDevices) {
    if (!pScheme) return;
    if (pauseIdleDevices) {
        PowerWriteACValueIndex(nullptr, pScheme, &OPIT_GUID_USB_SUBGROUP, &OPIT_GUID_USB_SELECTIVE_SUSPEND, 1);
        PowerWriteDCValueIndex(nullptr, pScheme, &OPIT_GUID_USB_SUBGROUP, &OPIT_GUID_USB_SELECTIVE_SUSPEND, 1);
        PowerWriteACValueIndex(nullptr, pScheme, &OPIT_GUID_PCIEXPRESS_SUBGROUP, &OPIT_GUID_PCIEXPRESS_ASPM, 2);
        PowerWriteDCValueIndex(nullptr, pScheme, &OPIT_GUID_PCIEXPRESS_SUBGROUP, &OPIT_GUID_PCIEXPRESS_ASPM, 2);
    } else {
        PowerWriteACValueIndex(nullptr, pScheme, &OPIT_GUID_USB_SUBGROUP, &OPIT_GUID_USB_SELECTIVE_SUSPEND, 0);
        PowerWriteDCValueIndex(nullptr, pScheme, &OPIT_GUID_USB_SUBGROUP, &OPIT_GUID_USB_SELECTIVE_SUSPEND, 0);
        PowerWriteACValueIndex(nullptr, pScheme, &OPIT_GUID_PCIEXPRESS_SUBGROUP, &OPIT_GUID_PCIEXPRESS_ASPM, 0);
        PowerWriteDCValueIndex(nullptr, pScheme, &OPIT_GUID_PCIEXPRESS_SUBGROUP, &OPIT_GUID_PCIEXPRESS_ASPM, 0);
    }
}

static void ProtectEssentialPeripheralsOnScheme(const GUID* pScheme, bool pauseIdleDevices = false) {
    ConfigurePeripheralsOnScheme(pScheme, pauseIdleDevices);
}

static std::string GuidToString(const GUID& g) {
    char guidStr[64];
    snprintf(guidStr, sizeof(guidStr), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return std::string(guidStr);
}

static bool StringToGuid(const std::string& str, GUID& outGuid) {
    std::wstring ws = StringUtils::ToWide(str);
    return SUCCEEDED(CLSIDFromString(ws.c_str(), &outGuid));
}

PowerManager::PowerManager() = default;

PowerManager::~PowerManager() {
    RestoreOriginalSettings();
}

bool PowerManager::Initialize() {
    if (m_initialized) return true;

    ElevationHelper::UnlockCoreParkingInRegistry();

    PowerScheme active = GetActiveScheme();
    m_originalSchemeGuid = active.guid;
    m_originalThrottleLimit = GetProcessorThrottleLimit();
    m_initialized = true;
    return true;
}

namespace {
// PowerReadFriendlyName devolve o tamanho em bytes COM o terminador UTF-16. Converter o buffer
// inteiro faz o nome carregar um U+0000 no fim, e ai todo texto escrito DEPOIS do nome se perde:
// as APIs de janela e de log param no primeiro NUL. O rotulo "Plano [Economia]: ... (Ativo agora)"
// e a evidencia "plano=Equilibrado (estagio 1)" eram cortados por causa disso.
std::string FriendlyNameFromBuffer(const UCHAR* buf, DWORD nsize) {
    if (!buf || nsize < sizeof(wchar_t)) return std::string();
    std::wstring raw(reinterpret_cast<LPCWCH>(buf), nsize / sizeof(wchar_t));
    const size_t nul = raw.find(L'\0');
    if (nul != std::wstring::npos) raw.erase(nul);
    return StringUtils::ToUtf8(raw);
}
} // namespace

PowerScheme PowerManager::GetActiveScheme() {
    PowerScheme ps;
    GUID* pGuid = nullptr;
    if (PowerGetActiveScheme(nullptr, &pGuid) == ERROR_SUCCESS && pGuid) {
        ps.guid = GuidToString(*pGuid);
        ps.isActive = true;

        DWORD nsize = 0;
        PowerReadFriendlyName(nullptr, pGuid, nullptr, nullptr, nullptr, &nsize);
        if (nsize > 0) {
            std::vector<UCHAR> buf(nsize);
            if (PowerReadFriendlyName(nullptr, pGuid, nullptr, nullptr, buf.data(), &nsize) == ERROR_SUCCESS) {
                ps.name = FriendlyNameFromBuffer(buf.data(), nsize);
            }
        }
        if (ps.name.empty()) {
            ps.name = "Plano Ativo";
        }
        LocalFree(pGuid);
    }
    return ps;
}

std::vector<PowerScheme> PowerManager::ListSchemes() {
    std::vector<PowerScheme> out;
    PowerScheme current = GetActiveScheme();

    for (DWORD idx = 0;; idx++) {
        GUID g{};
        DWORD gsize = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, idx, reinterpret_cast<UCHAR*>(&g), &gsize) != ERROR_SUCCESS) {
            break;
        }

        DWORD nsize = 0;
        PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &nsize);
        std::vector<UCHAR> buf(nsize);
        if (nsize > 0 && PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, buf.data(), &nsize) == ERROR_SUCCESS) {
            PowerScheme ps;
            ps.guid = GuidToString(g);
            ps.name = FriendlyNameFromBuffer(buf.data(), nsize);
            ps.isActive = (ps.guid == current.guid);
            out.push_back(ps);
        }
    }
    return out;
}

bool PowerManager::SetActiveScheme(const std::string& guidStr) {
    GUID g{};
    if (!StringToGuid(guidStr, g)) return false;

    // Se o plano for econômico e o usuário optou por pausar dispositivos ociosos, ativa suspensão.
    // Caso contrário, protege os periféricos e barramentos essenciais contra desligamento.
    bool isEco = false;
    std::string lowerGuid = StringUtils::ToLower(guidStr);
    if (lowerGuid.find("a1841308-3541-4fab-bc81-f71556f20b4a") != std::string::npos) {
        isEco = true;
    }
    ProtectEssentialPeripheralsOnScheme(&g, (isEco && m_pauseIdleDevicesEco));

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) == ERROR_SUCCESS && pActive) {
        bool alreadyActive = (IsEqualGUID(*pActive, g) != 0);
        LocalFree(pActive);
        if (alreadyActive) {
            return true;
        }
    }
    return (PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS);
}

uint32_t PowerManager::GetProcessorThrottleLimit() {
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return 100;
    }

    DWORD acValue = 100;
    PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &acValue);
    LocalFree(pActive);
    return acValue;
}

bool PowerManager::SetProcessorThrottleLimit(uint32_t percent) {
    if (percent > 100) percent = 100;
    if (percent < 20) percent = 20; // Limite mínimo de segurança

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    DWORD curMaxAc = 100, curMaxDc = 100;
    PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &curMaxAc);
    PowerReadDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &curMaxDc);
    if (curMaxAc == percent && curMaxDc == percent) {
        LocalFree(pActive);
        return true;
    }

    ProtectEssentialPeripheralsOnScheme(pActive);
    DWORD resAc = PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, percent);
    DWORD resDc = PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, percent);
    PowerSetActiveScheme(nullptr, pActive);

    LocalFree(pActive);
    return (resAc == ERROR_SUCCESS && resDc == ERROR_SUCCESS);
}

bool PowerManager::SetProcessorThrottleRange(uint32_t minPercent, uint32_t maxPercent) {
    if (minPercent > 100 || maxPercent > 100) return false;
    if (minPercent > maxPercent) {
        // Conflito: minimo superior ao maximo deve ser rejeitado defensivamente
        return false;
    }

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    DWORD curMinAc = 5, curMaxAc = 100, curMinDc = 5, curMaxDc = 100;
    PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, &curMinAc);
    PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &curMaxAc);
    PowerReadDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, &curMinDc);
    PowerReadDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &curMaxDc);

    if (curMinAc == minPercent && curMaxAc == maxPercent && curMinDc == minPercent && curMaxDc == maxPercent) {
        LocalFree(pActive);
        return true;
    }

    ProtectEssentialPeripheralsOnScheme(pActive);
    PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, minPercent);
    PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, minPercent);

    PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, maxPercent);
    PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, maxPercent);

    DWORD res = PowerSetActiveScheme(nullptr, pActive);
    LocalFree(pActive);
    return (res == ERROR_SUCCESS);
}

bool PowerManager::SetCoreParking(uint32_t minCoresPercent) {
    if (minCoresPercent > 100) minCoresPercent = 100;
    if (minCoresPercent < 5) minCoresPercent = 5;

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    ProtectEssentialPeripheralsOnScheme(pActive);
    DWORD lastError = ERROR_SUCCESS;
    auto writeBoth = [&](const GUID* setting, uint32_t value) {
        DWORD rAc = PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value);
        DWORD rDc = PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value);
        if (rAc != ERROR_SUCCESS) lastError = rAc;
        if (rDc != ERROR_SUCCESS) lastError = rDc;
    };

    // Grava CPMINCORES para Classe 0 (P-cores) e Classe 1 (E-cores) em AC e DC
    writeBoth(&OPIT_GUID_CORE_PARKING_MIN_CORES, minCoresPercent);
    writeBoth(&OPIT_GUID_CORE_PARKING_MIN_CORES1, minCoresPercent);

    // Grava CPMAXCORES (100% de teto para permitir que nucleos acordem sob demanda)
    writeBoth(&OPIT_GUID_CORE_PARKING_MAX_CORES, 100);
    writeBoth(&OPIT_GUID_CORE_PARKING_MAX_CORES1, 100);

    DWORD res = PowerSetActiveScheme(nullptr, pActive);
    if (res != ERROR_SUCCESS) lastError = res;
    LocalFree(pActive);
    return (lastError == ERROR_SUCCESS);
}

std::string PowerManager::FindEcoScheme() {
    auto schemes = ListSchemes();
    // 1. Verificar se existe o GUID padrão da Microsoft para Economia de Energia: GUID_MAX_POWER_SAVINGS
    // {a1841308-3541-4fab-bc81-f71556f20b4a}
    for (const auto& s : schemes) {
        std::string lowerGuid = StringUtils::ToLower(s.guid);
        if (lowerGuid.find("a1841308-3541-4fab-bc81-f71556f20b4a") != std::string::npos) {
            return s.guid;
        }
    }

    // 2. Procurar por identificadores lexicais em múltiplos idiomas
    for (const auto& s : schemes) {
        std::string lower = StringUtils::ToLower(s.name);
        if (lower.find("economia") != std::string::npos ||
            lower.find("poupança") != std::string::npos ||
            lower.find("saver") != std::string::npos ||
            lower.find("power saver") != std::string::npos ||
            lower.find("eco") != std::string::npos) {
            return s.guid;
        }
    }

    // 3. Fallback: se não encontrar específico, buscar qualquer plano que não seja Alto Desempenho
    for (const auto& s : schemes) {
        std::string lower = StringUtils::ToLower(s.name);
        if (lower.find("equilibrado") != std::string::npos || lower.find("balanced") != std::string::npos) {
            return s.guid;
        }
    }

    return schemes.empty() ? "" : schemes[0].guid;
}

std::string PowerManager::FindBalancedScheme() {
    auto schemes = ListSchemes();
    for (const auto& s : schemes) {
        std::string lowerGuid = StringUtils::ToLower(s.guid);
        if (lowerGuid.find("381b4222-f694-41f0-9685-ff5bb260df2e") != std::string::npos) {
            return s.guid;
        }
    }
    for (const auto& s : schemes) {
        std::string lower = StringUtils::ToLower(s.name);
        if (lower.find("balanceado") != std::string::npos ||
            lower.find("equilibrado") != std::string::npos ||
            lower.find("balanced") != std::string::npos) {
            return s.guid;
        }
    }
    return schemes.empty() ? "" : schemes[0].guid;
}

std::string PowerManager::FindHighPerformanceScheme() {
    auto schemes = ListSchemes();
    for (const auto& s : schemes) {
        std::string lowerGuid = StringUtils::ToLower(s.guid);
        if (lowerGuid.find("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") != std::string::npos ||
            lowerGuid.find("e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos) {
            return s.guid;
        }
    }
    for (const auto& s : schemes) {
        std::string lower = StringUtils::ToLower(s.name);
        if (lower.find("alto") != std::string::npos ||
            lower.find("high") != std::string::npos ||
            lower.find("desempenho") != std::string::npos ||
            lower.find("ultimate") != std::string::npos) {
            return s.guid;
        }
    }
    return "";
}

bool PowerManager::ForceEcoPlan(bool enable, std::string& outAppliedName, std::string& outStatusMsg) {
    if (enable) {
        PowerScheme current = GetActiveScheme();
        std::string ecoGuid = FindEcoScheme();
        if (ecoGuid.empty()) {
            outStatusMsg = "Indisponivel: Nenhum plano economico compativel localizado no Windows.";
            return false;
        }

        if (!m_isEcoPlanForced) {
            m_preForcedEcoSchemeGuid = current.guid;
        }

        if (!SetActiveScheme(ecoGuid)) {
            outStatusMsg = "Falha ao aplicar plano economico no Windows.";
            return false;
        }

        PowerScheme verified = GetActiveScheme();
        m_isEcoPlanForced = true;
        outAppliedName = verified.name;
        outStatusMsg = "Aplicado com sucesso: " + verified.name;
        return true;
    } else {
        if (m_isEcoPlanForced && !m_preForcedEcoSchemeGuid.empty()) {
            SetActiveScheme(m_preForcedEcoSchemeGuid);
            PowerScheme restored = GetActiveScheme();
            outAppliedName = restored.name;
            outStatusMsg = "Restaurado plano anterior: " + restored.name;
        } else {
            outStatusMsg = "Imposicao desativada.";
        }
        m_isEcoPlanForced = false;
        return true;
    }
}

bool PowerManager::ReadSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t& minAc, uint32_t& maxAc, uint32_t& minDc, uint32_t& maxDc) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;

    DWORD valMinAc = 5, valMaxAc = 100, valMinDc = 5, valMaxDc = 100;
    DWORD r1 = PowerReadACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, &valMinAc);
    DWORD r2 = PowerReadACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &valMaxAc);
    DWORD r3 = PowerReadDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, &valMinDc);
    DWORD r4 = PowerReadDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, &valMaxDc);

    minAc = valMinAc;
    maxAc = valMaxAc;
    minDc = valMinDc;
    maxDc = valMaxDc;

    // Um valor lido por fallback nao e uma leitura: sem isto, o diario gravaria padroes como se fossem o original do usuario
    return r1 == ERROR_SUCCESS && r2 == ERROR_SUCCESS && r3 == ERROR_SUCCESS && r4 == ERROR_SUCCESS;
}

bool PowerManager::WriteSchemeProcessorThrottle(const std::string& schemeGuid, uint32_t minAc, uint32_t maxAc, uint32_t minDc, uint32_t maxDc) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;

    if (minAc > 100 || maxAc > 100 || minDc > 100 || maxDc > 100) return false;
    if (minAc > maxAc || minDc > maxDc) {
        // Conflito: minimo superior ao maximo rejeitado sem correcao silenciosa
        return false;
    }

    PowerWriteACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, minAc);
    PowerWriteACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, maxAc);
    PowerWriteDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MINIMUM, minDc);
    PowerWriteDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_PROCESSOR_THROTTLE_MAXIMUM, maxDc);

    PowerScheme active = GetActiveScheme();
    if (active.guid == schemeGuid) {
        PowerSetActiveScheme(nullptr, &g);
    }
    return true;
}

bool PowerManager::ReadCoreParking(const std::string& schemeGuid, uint32_t& acVal, uint32_t& dcVal) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;

    DWORD valAc = 50, valDc = 75;
    DWORD rAc = PowerReadACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_CORE_PARKING_MIN_CORES, &valAc);
    DWORD rDc = PowerReadDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, &OPIT_GUID_CORE_PARKING_MIN_CORES, &valDc);
    acVal = valAc;
    dcVal = valDc;
    return rAc == ERROR_SUCCESS && rDc == ERROR_SUCCESS;
}

bool PowerManager::SetCoreParkingLines(const std::string& schemeGuid, uint32_t acPercent, uint32_t dcPercent) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;

    auto clamp = [](uint32_t v) { return v < 5 ? 5 : (v > 100 ? 100 : v); };
    const uint32_t acVal = clamp(acPercent);
    const uint32_t dcVal = clamp(dcPercent);

    DWORD lastError = ERROR_SUCCESS;
    const GUID* parkingSettings[] = {&OPIT_GUID_CORE_PARKING_MIN_CORES, &OPIT_GUID_CORE_PARKING_MIN_CORES1,
                                     &OPIT_GUID_CORE_PARKING_MAX_CORES, &OPIT_GUID_CORE_PARKING_MAX_CORES1};
    auto writeLine = [&](bool isAc, uint32_t value) {
        for (const GUID* setting : parkingSettings) {
            DWORD r = isAc ? PowerWriteACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value)
                           : PowerWriteDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value);
            if (r != ERROR_SUCCESS) lastError = r;
        }
    };
    writeLine(true, acVal);
    writeLine(false, dcVal);

    PowerScheme active = GetActiveScheme();
    if (active.guid == schemeGuid) {
        DWORD res = PowerSetActiveScheme(nullptr, &g);
        if (res != ERROR_SUCCESS) lastError = res;
    }
    return lastError == ERROR_SUCCESS;
}

bool PowerManager::SetCoreParkingForScheme(const std::string& schemeGuid, uint32_t minCoresPercent) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;

    if (minCoresPercent > 100) minCoresPercent = 100;
    if (minCoresPercent < 5) minCoresPercent = 5;

    DWORD lastError = ERROR_SUCCESS;
    auto writeBoth = [&](const GUID* setting, uint32_t value) {
        DWORD rAc = PowerWriteACValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value);
        DWORD rDc = PowerWriteDCValueIndex(nullptr, &g, &OPIT_GUID_PROCESSOR_SETTINGS_SUBGROUP, setting, value);
        if (rAc != ERROR_SUCCESS) lastError = rAc;
        if (rDc != ERROR_SUCCESS) lastError = rDc;
    };

    writeBoth(&OPIT_GUID_CORE_PARKING_MIN_CORES, minCoresPercent);
    writeBoth(&OPIT_GUID_CORE_PARKING_MIN_CORES1, minCoresPercent);
    writeBoth(&OPIT_GUID_CORE_PARKING_MAX_CORES, 100);
    writeBoth(&OPIT_GUID_CORE_PARKING_MAX_CORES1, 100);

    PowerScheme active = GetActiveScheme();
    if (active.guid == schemeGuid) {
        DWORD res = PowerSetActiveScheme(nullptr, &g);
        if (res != ERROR_SUCCESS) lastError = res;
    }
    return lastError == ERROR_SUCCESS;
}

bool PowerManager::ForceMaxCoreParking(bool enable, uint32_t& outAppliedVal) {
    PowerScheme active = GetActiveScheme();
    if (enable) {
        uint32_t curAc = 50, curDc = 75;
        ReadCoreParking(active.guid, curAc, curDc);
        if (!m_isMaxCoreParkingForced) {
            m_preForcedCoreParkingAc = curAc;
        }

        // 5% de núcleos mínimos unparked = permite ao agendador do Windows pausar o máximo de núcleos lógicos
        uint32_t maxParkingTarget = 5;
        SetCoreParking(maxParkingTarget);
        m_isMaxCoreParkingForced = true;
        outAppliedVal = maxParkingTarget;
        return true;
    } else {
        if (m_isMaxCoreParkingForced) {
            SetCoreParking(m_preForcedCoreParkingAc > 0 ? m_preForcedCoreParkingAc : 50);
            outAppliedVal = m_preForcedCoreParkingAc;
        }
        m_isMaxCoreParkingForced = false;
        return true;
    }
}

uint32_t PowerManager::GetDiskIdleTimeout(bool dc) {
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return 0;
    }

    DWORD timeoutSec = 0;
    if (dc) {
        PowerReadDCValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, &timeoutSec);
    } else {
        PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, &timeoutSec);
    }
    LocalFree(pActive);
    return timeoutSec;
}

bool PowerManager::SetDiskIdleTimeout(uint32_t seconds, bool dc) {
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    DWORD res = ERROR_SUCCESS;
    if (dc) {
        res = PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, seconds);
    } else {
        res = PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, seconds);
    }
    PowerSetActiveScheme(nullptr, pActive);
    LocalFree(pActive);
    return (res == ERROR_SUCCESS);
}

bool PowerManager::PreventDiskPowerDown(bool prevent) {
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    if (prevent) {
        if (!m_isDiskPowerDownPrevented) {
            DWORD acVal = 0, dcVal = 0;
            PowerReadACValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, &acVal);
            PowerReadDCValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, &dcVal);
            m_prePreventDiskTimeoutAc = acVal;
            m_prePreventDiskTimeoutDc = dcVal;
        }

        // 0 segundos = Nunca desligar o disco rígido (evita congelamentos por spin-down)
        PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, 0);
        PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, 0);
        PowerSetActiveScheme(nullptr, pActive);
        m_isDiskPowerDownPrevented = true;
    } else {
        if (m_isDiskPowerDownPrevented) {
            PowerWriteACValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, m_prePreventDiskTimeoutAc);
            PowerWriteDCValueIndex(nullptr, pActive, &OPIT_GUID_DISK_SUBGROUP, &OPIT_GUID_DISK_POWERDOWN_TIMEOUT, m_prePreventDiskTimeoutDc);
            PowerSetActiveScheme(nullptr, pActive);
        }
        m_isDiskPowerDownPrevented = false;
    }

    LocalFree(pActive);
    return true;
}

bool PowerManager::ConfigureIdleDeviceSuspension(const std::string& schemeGuid, bool pauseIdleDevices) {
    GUID g{};
    if (!StringToGuid(schemeGuid, g)) return false;
    m_pauseIdleDevicesEco = pauseIdleDevices;
    ConfigurePeripheralsOnScheme(&g, pauseIdleDevices);
    return true;
}

// 128 = sem bateria, 255 = estado de energia indisponivel (VM e servico de energia parado).
bool SystemHasBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return false;
    return sps.BatteryFlag != 128 && sps.BatteryFlag != 255;
}

BatteryStatusInfo PowerManager::GetBatteryStatus() {
    BatteryStatusInfo info;
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        info.isAcOnline = (sps.ACLineStatus == 1);
        info.hasBattery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255); // 128 = Sem bateria
        info.batteryPercent = (sps.BatteryLifePercent <= 100) ? sps.BatteryLifePercent : -1;
        info.isCharging = (sps.BatteryFlag & 8) != 0;
    }
    return info;
}

void PowerManager::RestoreOriginalSettings() {
    if (!m_initialized) return;

    if (m_isDiskPowerDownPrevented) {
        PreventDiskPowerDown(false);
    }
    if (!m_originalSchemeGuid.empty()) {
        SetActiveScheme(m_originalSchemeGuid);
    }
    if (m_originalThrottleLimit > 0) {
        SetProcessorThrottleLimit(m_originalThrottleLimit);
    }
}

} // namespace opitcore
