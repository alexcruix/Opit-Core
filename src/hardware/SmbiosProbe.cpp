// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "SmbiosProbe.h"

#ifdef _WIN32
#include <windows.h>
#else
// Os tres tipos do parser de tabelas definidos a partir do C++, para que a aritmetica de
// offsets SMBIOS seja literalmente a mesma dos dois lados.
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#endif
#include <cstring>

namespace opitcore {
namespace {

#pragma pack(push, 1)
struct OpitRawSmbiosData {
    BYTE UsedCallingMethod;
    BYTE SmbiosMajorVersion;
    BYTE SmbiosMinorVersion;
    BYTE DmiRevision;
    DWORD Length;
    BYTE TableData[1];
};
#pragma pack(pop)

std::string GetString(const BYTE* tableEnd, const BYTE* header, BYTE index) {
    if (index == 0) return "";
    const BYTE* p = header + header[1];
    for (BYTE i = 1; i < index && p < tableEnd; ++i) {
        p += strlen(reinterpret_cast<const char*>(p)) + 1;
    }
    if (p >= tableEnd) return "";
    return std::string(reinterpret_cast<const char*>(p));
}

const char* ChassisName(BYTE t) {
    static const char* kNames[] = {
        "", "Outro", "Desconhecido", "Desktop", "Desktop compacto", "Pizza Box",
        "Mini Tower", "Tower", "Portatil", "Laptop", "Notebook",
        "Hand Held", "Docking Station", "All in One", "Sub Notebook", "Economia de espaco",
        "Lunch Box", "Servidor principal", "Chassi de expansao", "Sub-chassi", "Expansao de barramento",
        "Periferico", "RAID", "Rack mount", "Caixa selada", "Mini PC",
        "Multi-sistema", "Compacto PCI", "Advanced TCA", "Blade", "Blade enclosure",
        "Tablet", "Convertivel", "Removivel", "Notebook clampado", "All in One portable"
    };
    t &= 0x7F;
    return (t < sizeof(kNames) / sizeof(kNames[0])) ? kNames[t] : "";
}

const char* FormFactorName(BYTE t) {
    switch (t) {
        case 0x08: return "DIMM";
        case 0x09: return "SIMM";
        case 0x0D: return "SODIMM";
        case 0x0E: return "SRIMM";
        case 0x01: return "Outro";
        default:   return "";
    }
}

const char* MemoryTypeName(BYTE t) {
    switch (t) {
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
        case 0x15: return "DDR3";
        case 0x17: return "DDR4";
        case 0x1F: return "DDR5";
        case 0x18: return "LPDDR";
        case 0x19: return "LPDDR2";
        case 0x1A: return "LPDDR3";
        case 0x1B: return "LPDDR4";
        case 0x20: return "LPDDR5";
        case 0x01: return "Outro";
        default:   return "";
    }
}

bool IsBoardPlaceholder(const std::string& s) {
    if (s.empty()) return true;
    std::string lower = s;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    return lower.find("system manufacturer") != std::string::npos ||
           lower.find("system product name") != std::string::npos ||
           lower.find("to be filled by o.e.m.") != std::string::npos ||
           lower.find("default string") != std::string::npos ||
           lower.find("type2 - board vendor name") != std::string::npos ||
           lower.find("none") != std::string::npos;
}

WORD ReadWord(const BYTE* header, BYTE offset, BYTE length) {
    if (length < offset + 2) return 0;
    return static_cast<WORD>(header[offset] | (header[offset + 1] << 8));
}

DWORD ReadDword(const BYTE* header, BYTE offset, BYTE length) {
    if (length < offset + 4) return 0;
    return static_cast<DWORD>(header[offset] | (header[offset + 1] << 8) |
                              (header[offset + 2] << 16) | (header[offset + 3] << 24));
}

} // namespace

// O parse das estruturas empacotadas e comum aos dois sistemas: o Windows tira os bytes do
// GetSystemFirmwareTable('RSMB'), o Linux do blob /sys/firmware/dmi/tables/DMI. O firmware
// fala o mesmo SMBIOS, por isso a aritmetica de offsets nao se duplica.
static void ParsePackedTables(const BYTE* p, const BYTE* end, SmbiosSnapshot& snap) {
    while (p + 4 <= end) {
        const BYTE type = p[0];
        const BYTE len = p[1];
        if (len < 4) break;

        if (type == 0 && len >= 9) { // BIOS Information
            snap.bios.vendor = GetString(end, p, p[4]);
            snap.bios.version = GetString(end, p, p[5]);
            snap.bios.releaseDate = GetString(end, p, p[8]);
            snap.bios.origin = DataOrigin::Factory;
            snap.bios.confidence = DataConfidence::High;
        } else if (type == 1 && len >= 8) { // System Information
            // 04 fabricante, 05 produto, 06 versao; 07 e numero de serie: ignorado de proposito.
            // Apenas se NAO for placeholder de firmware ("System manufacturer" etc.).
            if (!IsBoardPlaceholder(GetString(end, p, p[4]))) {
                snap.board.manufacturer = GetString(end, p, p[4]);
            }
            if (!IsBoardPlaceholder(GetString(end, p, p[5]))) {
                snap.board.product = GetString(end, p, p[5]);
            }
            const std::string sysVersion = GetString(end, p, p[6]);
            if (!IsBoardPlaceholder(sysVersion)) {
                snap.board.version = sysVersion;
            }
            snap.board.origin = DataOrigin::Factory;
            snap.board.confidence = DataConfidence::High;
        } else if (type == 2 && len >= 7) { // Base Board — prevalece sobre placeholders do Type 1
            const std::string mfr = GetString(end, p, p[4]);
            const std::string prod = GetString(end, p, p[5]);
            const std::string ver = (len >= 8) ? GetString(end, p, p[6]) : std::string();
            if (!IsBoardPlaceholder(mfr)) {
                snap.board.manufacturer = mfr;
            }
            if (!IsBoardPlaceholder(prod)) {
                snap.board.product = prod;
            }
            if (!IsBoardPlaceholder(ver)) {
                snap.board.version = ver;
            }
        } else if (type == 3 && len >= 6) { // Chassis
            snap.board.chassisType = ChassisName(p[5]);
        } else if (type == 17 && len >= 0x12) { // Memory Device
            const WORD sizeMB = ReadWord(p, 0x0C, len);
            if (sizeMB != 0 && sizeMB != 0xFFFF) {
                MemoryStickInfo stick;
                stick.capacityMB = sizeMB;
                if (sizeMB == 0x8000 && len >= 0x24) {
                    // SMBIOS 3.2+: tamanho estendido em KiB no offset 0x20
                    const DWORD kb = ReadDword(p, 0x20, len);
                    stick.capacityMB = static_cast<uint32_t>(kb / 1024);
                }
                stick.locator = GetString(end, p, p[0x10]);
                stick.manufacturer = GetString(end, p, p[0x17]);
                stick.partNumber = GetString(end, p, p[0x1A]);
                stick.formFactor = FormFactorName(p[0x0E]);
                const BYTE rawType = p[0x12];
                const char* typeName = MemoryTypeName(rawType);
                // Quirk medido 2026-11-18 (ASUS PRIME X370-PRO, AMI 6265): firmware grava o
                // codigo CIM (26=DDR4) onde a spec SMBIOS espera 0x1A=LPDDR3. LPDDR nunca vem
                // em modulo encaixavel: byte 26 com form factor DIMM/SIMM/SODIMM e DDR4.
                if (rawType == 0x1A && (p[0x0E] == 0x08 || p[0x0E] == 0x09 || p[0x0E] == 0x0D)) {
                    typeName = "DDR4";
                }
                stick.memoryType = typeName ? typeName : "";
                WORD speed = ReadWord(p, 0x15, len);
                if (speed == 0xFFFF) speed = 0;
                stick.speedMHz = speed;
                WORD conf = ReadWord(p, 0x1C, len);
                if (conf == 0xFFFF || conf == 0) conf = speed;
                stick.configuredSpeedMHz = conf;
                snap.sticks.push_back(stick);
            }
        }

        const BYTE* q = p + len;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        if (q + 1 >= end) break;
        p = q + 2;
    }

    snap.ok = true;
}

#ifdef _WIN32
SmbiosSnapshot SmbiosProbe::Probe() {
    SmbiosSnapshot snap;

    UINT size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size == 0) {
        return snap;
    }
    std::vector<BYTE> buf(size);
    UINT got = GetSystemFirmwareTable('RSMB', 0, buf.data(), size);
    if (got < sizeof(OpitRawSmbiosData)) {
        return snap;
    }

    auto* raw = reinterpret_cast<const OpitRawSmbiosData*>(buf.data());
    ParsePackedTables(raw->TableData, raw->TableData + raw->Length, snap);
    return snap;
}
#else
namespace {

bool ReadWholeFile(const std::string& path, std::vector<BYTE>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got == 0) { out.clear(); return false; }
    out.resize(got);
    return true;
}

std::string ReadIdField(const std::string& idRoot, const char* key) {
    std::ifstream f(idRoot + "/" + key);
    std::string s;
    std::getline(f, s);
    return s;
}

bool NadaDetectado(const SmbiosSnapshot& snap) {
    return snap.bios.vendor.empty() && snap.bios.version.empty() &&
           snap.board.manufacturer.empty() && snap.board.product.empty() &&
           snap.sticks.empty();
}

// Prenchimento pelo /sys/class/dmi/id: e o que um utilizador sem root consegue ler.
// A origem passa a Detected (o kernel re-emite o firmware) e os pentes de RAM ficam
// ausentes, porque so a tabela empacotada os traz.
void FillFromDmiId(const std::string& idRoot, SmbiosSnapshot& snap) {
    snap.bios.vendor = ReadIdField(idRoot, "bios_vendor");
    snap.bios.version = ReadIdField(idRoot, "bios_version");
    snap.bios.releaseDate = ReadIdField(idRoot, "bios_date");
    snap.bios.origin = DataOrigin::Detected;
    snap.bios.confidence = DataConfidence::Medium;

    const std::string boardVendor = ReadIdField(idRoot, "board_vendor");
    const std::string boardName = ReadIdField(idRoot, "board_name");
    const std::string boardVersion = ReadIdField(idRoot, "board_version");
    const std::string sysVendor = ReadIdField(idRoot, "sys_vendor");
    const std::string productName = ReadIdField(idRoot, "product_name");

    if (!IsBoardPlaceholder(boardVendor)) {
        snap.board.manufacturer = boardVendor;
    } else if (!IsBoardPlaceholder(sysVendor)) {
        snap.board.manufacturer = sysVendor;
    }
    if (!IsBoardPlaceholder(boardName)) {
        snap.board.product = boardName;
    } else if (!IsBoardPlaceholder(productName)) {
        snap.board.product = productName;
    }
    if (!IsBoardPlaceholder(boardVersion)) {
        snap.board.version = boardVersion;
    }
    const std::string chassisHex = ReadIdField(idRoot, "chassis_type");
    if (!chassisHex.empty()) {
        snap.board.chassisType = ChassisName((BYTE)std::strtoul(chassisHex.c_str(), nullptr, 0));
    }
    snap.board.origin = DataOrigin::Detected;
    snap.board.confidence = DataConfidence::Medium;
    snap.ok = !NadaDetectado(snap);
}

} // namespace

SmbiosSnapshot SmbiosProbe::Probe() {
    SmbiosSnapshot snap;

    // Raiz configuravel, pelo mesmo motivo do hwmon: as tabelas empacotadas so se leem a
    // root, e sem fixture nao ha como exercitar o parser em maquina virtual nem em CI.
    const char* tablesEnv = std::getenv("OPITCORE_DMI_ROOT");
    const std::string tablesRoot = (tablesEnv && *tablesEnv) ? std::string(tablesEnv)
                                                             : std::string("/sys/firmware/dmi/tables");
    std::vector<BYTE> blob;
    if (ReadWholeFile(tablesRoot + "/DMI", blob) && blob.size() >= 4) {
        ParsePackedTables(blob.data(), blob.data() + blob.size(), snap);
        if (!NadaDetectado(snap)) {
            snap.ok = true;
            return snap;
        }
        snap = SmbiosSnapshot();
    }

    const char* idEnv = std::getenv("OPITCORE_DMI_ID_ROOT");
    const std::string idRoot = (idEnv && *idEnv) ? std::string(idEnv) : std::string("/sys/class/dmi/id");
    FillFromDmiId(idRoot, snap);
    return snap;
}
#endif

} // namespace opitcore
