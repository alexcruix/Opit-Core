// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "HwmonFanProvider.h"

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <map>
#include <set>

namespace opitcore {
namespace {

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// "fan12_input" com prefixo "fan" e sufixo "_input" -> 12. Só algarismos entre os dois:
// é este inteiro (e nunca o nome lido do disco) que forma os caminhos que vamos escrever.
bool IndexedName(const std::string& name, const char* prefix, const char* suffix, int& idxOut) {
    const size_t pl = std::strlen(prefix);
    const size_t sl = std::strlen(suffix);
    if (name.size() <= pl + sl) return false;
    if (name.compare(0, pl, prefix) != 0) return false;
    if (name.compare(name.size() - sl, sl, suffix) != 0) return false;
    const std::string digits = name.substr(pl, name.size() - pl - sl);
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
    }
    idxOut = std::atoi(digits.c_str());
    return idxOut > 0;
}

bool OnlyDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

std::string ToLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Os rótulos do sysfs terminam em '\n' e alguns chips deixam-nos vazios. Uma leitura, sem strtok.
bool ReadTextFile(const std::string& path, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[128] = {0};
    bool got = false;
    if (std::fgets(buf, sizeof(buf), f)) {
        size_t n = std::strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n > 0) {
            out = buf;
            got = true;
        }
    }
    std::fclose(f);
    return got;
}

} // namespace

HwmonFanProvider::HwmonFanProvider(std::string sysfsRoot) : m_root(std::move(sysfsRoot)) {
    while (m_root.size() > 1 && m_root.back() == '/') m_root.pop_back();
}

HwmonFanProvider::~HwmonFanProvider() {
    Shutdown();
}

bool HwmonFanProvider::Initialize() {
    // O driver pode não existir (kernel sem CONFIG_HWMON, ou container sem /sys). Isso não é
    // erro do provider: ele fica inicializado e publica zero aparelhos, como os providers NVAPI
    // e ADL fazem quando a placa não está lá.
    DIR* probe = opendir(m_root.c_str());
    if (probe) closedir(probe);
    m_targets.clear();
    m_initialized = true;
    return true;
}

void HwmonFanProvider::Shutdown() {
    m_targets.clear();
    m_initialized = false;
}

bool HwmonFanProvider::ReadInt(const std::string& path, long long& out) const {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[64] = {0};
    const size_t got = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (got == 0) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(buf, &end, 10);
    if (end == buf) return false;
    out = v;
    return true;
}

bool HwmonFanProvider::WriteInt(const std::string& path, long long value, FanStatusCode& codeOut,
                               std::string& msgOut) const {
    codeOut = FanStatusCode::Ok;
    msgOut.clear();
    const std::string text = std::to_string(value);
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            codeOut = FanStatusCode::Unavailable;
            msgOut = "O ficheiro " + path + " não existe (o driver mudou entretanto).";
        } else if (errno == EACCES || errno == EPERM) {
            codeOut = FanStatusCode::AccessDenied;
            msgOut = "Escrita recusada em " + path + ": precisa de privilégio root e o modo do "
                     "ficheiro não o permite.";
        } else if (errno == EINVAL || errno == ERANGE || errno == EDOM) {
            codeOut = FanStatusCode::CommandRejected;
            msgOut = "O driver rejeitou o valor em " + path + ".";
        } else {
            codeOut = FanStatusCode::ReadError;
            msgOut = "Falha de escrita em " + path + ": " + std::strerror(errno);
        }
        return false;
    }
    size_t off = 0;
    while (off < text.size()) {
        const ssize_t w = ::write(fd, text.data() + off, text.size() - off);
        if (w <= 0) {
            const int saved = errno;
            ::close(fd);
            codeOut = (saved == EACCES || saved == EPERM) ? FanStatusCode::AccessDenied
                      : (saved == EINVAL || saved == ERANGE) ? FanStatusCode::CommandRejected
                      : FanStatusCode::ReadError;
            msgOut = "Escrita interrompida em " + path + ": " + std::strerror(saved);
            return false;
        }
        off += static_cast<size_t>(w);
    }
    ::close(fd);
    return true;
}

std::vector<HwmonFanProvider::FanTarget> HwmonFanProvider::Scan() const {
    std::vector<FanTarget> out;
    DIR* root = opendir(m_root.c_str());
    if (!root) return out;

    std::vector<std::string> hwmons;
    while (const dirent* e = readdir(root)) {
        const std::string n = e->d_name;
        if (n.compare(0, 5, "hwmon") == 0 && OnlyDigits(n.substr(5))) hwmons.push_back(n);
    }
    closedir(root);
    std::sort(hwmons.begin(), hwmons.end());

    for (const std::string& hw : hwmons) {
        const std::string dir = m_root + "/" + hw;
        std::string chip;
        {
            std::string raw;
            if (ReadTextFile(dir + "/name", raw)) chip = ToLower(raw);
        }
        if (chip.empty()) chip = "desconhecido";

        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::set<int> fanIdx, tempIdx, pwmIdx;
        std::map<int, std::string> tempLabel;
        while (const dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            int idx = 0;
            if (IndexedName(n, "fan", "_input", idx)) fanIdx.insert(idx);
            else if (IndexedName(n, "temp", "_input", idx)) tempIdx.insert(idx);
            else if (IndexedName(n, "pwm", "", idx)) pwmIdx.insert(idx);
            else if (IndexedName(n, "temp", "_label", idx)) {
                std::string lbl;
                if (ReadTextFile(dir + "/" + n, lbl)) tempLabel[idx] = lbl;
            }
        }
        closedir(d);

        // Índice da ventoinha = qualquer índice que publique fanN_input ou pwmN. Os chips que só
        // têm pwm (sem tacómetro) continuam listáveis: leitura de RPM fica marcada como ausente.
        std::set<int> targets = fanIdx;
        targets.insert(pwmIdx.begin(), pwmIdx.end());

        for (int idx : targets) {
            FanTarget t;
            t.id = "hwmon:" + hw + ":fan" + std::to_string(idx);
            t.dir = dir;
            t.chip = chip;
            if (fanIdx.count(idx)) t.fanInput = dir + "/fan" + std::to_string(idx) + "_input";
            if (tempIdx.count(idx)) {
                t.tempInput = dir + "/temp" + std::to_string(idx) + "_input";
                const auto lit = tempLabel.find(idx);
                if (lit != tempLabel.end()) t.tempLabel = lit->second;
            } else if (!tempIdx.empty()) {
                // Sem temperatura no mesmo índice: a mais baixa publicada é a que o driver usa
                // para o controlo da ventoinha, e é a que mostramos como referência.
                const int ref = *tempIdx.begin();
                t.tempInput = dir + "/temp" + std::to_string(ref) + "_input";
                const auto lit = tempLabel.find(ref);
                if (lit != tempLabel.end()) t.tempLabel = lit->second;
            }
            const std::string idxStr = std::to_string(idx);
            std::string pwm = dir + "/pwm" + idxStr;
            struct stat st{};
            if (::stat(pwm.c_str(), &st) != 0) {
                pwm.clear();
                if (idx == 1 && ::stat((dir + "/pwm").c_str(), &st) == 0) pwm = dir + "/pwm";
            }
            if (!pwm.empty()) {
                t.pwmFile = pwm;
                t.pwmEnableFile = pwm + "_enable";
                long long mx = 0;
                if (!ReadInt(pwm + "_max", mx) && !ReadInt(dir + "/pwm_max", mx)) mx = 255;
                long long mn = 0;
                if (!ReadInt(pwm + "_min", mn) && !ReadInt(dir + "/pwm_min", mn)) mn = 0;
                if (mx <= mn) mx = std::max<long long>(mx, 255);
                t.pwmMax = mx;
                t.pwmMin = mn;
            }
            out.push_back(t);
        }
    }
    return out;
}

const HwmonFanProvider::FanTarget* HwmonFanProvider::ResolveTarget(const std::string& deviceId) {
    if (!m_initialized) return nullptr;
    const auto it = m_targets.find(deviceId);
    if (it != m_targets.end()) return &it->second;
    // O sysfs ganha e perde aparelhos com módulos carregados/descarregados: se o id não está no
    // mapa, reescaneamos uma vez antes de dizer que não existe.
    m_targets.clear();
    for (const FanTarget& t : Scan()) m_targets[t.id] = t;
    const auto again = m_targets.find(deviceId);
    return again == m_targets.end() ? nullptr : &again->second;
}

size_t HwmonFanProvider::DevicesWithPwmControl() const {
    size_t n = 0;
    for (const auto& kv : m_targets) {
        if (!kv.second.pwmFile.empty()) ++n;
    }
    return n;
}

FanDeviceType HwmonFanProvider::ClassifyChip(const std::string& chip) const {
    // Nomes publicadors pelos módulos do kernel (hwmon "name"): nouveau para NVIDIA livre,
    // amdgpu/radeon, i915/xe para Intel, e os EC de portáteis. O resto é placa-mãe (nct6775,
    // it87, coretemp, k10temp, ...).
    if (chip.find("nouveau") != std::string::npos || chip == "gputemp") {
        return FanDeviceType::GpuNvidia;
    }
    if (chip.find("amdgpu") != std::string::npos || chip.find("radeon") != std::string::npos) {
        return FanDeviceType::GpuAmd;
    }
    if (chip == "i915" || chip == "xe" || chip.find("iucore") != std::string::npos) {
        return FanDeviceType::GpuIntel;
    }
    if (chip.find("dell_smm") != std::string::npos || chip.find("acer") != std::string::npos ||
        chip.find("asus") != std::string::npos || chip.find("msi_ec") != std::string::npos ||
        chip.find("thinkpad") != std::string::npos || chip == "ec") {
        return FanDeviceType::LaptopEC;
    }
    return FanDeviceType::CpuMotherboard;
}

std::vector<FanDeviceInfo> HwmonFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized) return devices;

    m_targets.clear();
    const std::vector<FanTarget> scanned = Scan();
    for (const FanTarget& t : scanned) m_targets[t.id] = t;

    for (const FanTarget& t : scanned) {
        FanDeviceInfo dev;
        dev.id = t.id;
        const std::string idxPart = t.id.substr(t.id.rfind(":fan") + 1);
        dev.name = "Ventoinha hwmon " + t.chip + " (" + idxPart + ")";
        if (!t.tempLabel.empty()) dev.name += " - " + t.tempLabel;
        dev.vendor = t.chip;
        dev.deviceType = ClassifyChip(t.chip);

        dev.capabilities.canReadRpm = !t.fanInput.empty();
        dev.capabilities.canReadTemperature = !t.tempInput.empty();
        dev.capabilities.canControlSpeed = !t.pwmFile.empty();
        dev.capabilities.canSetCurve = !t.pwmFile.empty();
        dev.capabilities.minSpeedPercent = 0;
        dev.capabilities.maxSpeedPercent = 100;

        if (!t.pwmFile.empty()) {
            long long enable = 0;
            long long duty = 0;
            const bool hasEnable = ReadInt(t.pwmEnableFile, enable);
            const bool hasDuty = ReadInt(t.pwmFile, duty);
            if (hasEnable && enable == 1) {
                dev.activeMode = FanControlMode::ManualFixed;
            } else if (hasEnable && enable == 2) {
                dev.activeMode = FanControlMode::CustomCurve;
            } else {
                dev.activeMode = FanControlMode::BiosFirmwareAuto;
            }
            if (hasDuty && t.pwmMax > t.pwmMin) {
                const long long span = t.pwmMax - t.pwmMin;
                dev.manualTargetSpeedPercent =
                    static_cast<uint32_t>(std::max<long long>(0, std::min<long long>(100,
                        (duty - t.pwmMin) * 100 / span)));
            }
        } else {
            dev.activeMode = FanControlMode::BiosFirmwareAuto;
        }

        const auto tel = ReadTelemetry(dev.id);
        if (tel.IsSuccess()) dev.latestReading = tel.value;
        devices.push_back(dev);
    }
    return devices;
}

FanResult<FanReading> HwmonFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    const FanTarget* t = ResolveTarget(deviceId);
    if (!t) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Aparelho " + deviceId + " não está no hwmon deste sistema.";
        return res;
    }

    FanReading r;
    r.timestampMs = NowMs();
    r.sensorSource = "hwmon " + t->chip + " " + deviceId.substr(deviceId.rfind(':') + 1);
    bool readAnything = false;

    long long milli = 0;
    if (!t->tempInput.empty() && ReadInt(t->tempInput, milli)) {
        r.temperatureC = static_cast<double>(milli) / 1000.0;
        r.hasTemperature = true;
        readAnything = true;
        if (!t->tempLabel.empty()) r.sensorSource += " / " + t->tempLabel;
    }

    long long rpm = 0;
    if (!t->fanInput.empty() && ReadInt(t->fanInput, rpm)) {
        r.rpm = static_cast<uint32_t>(rpm < 0 ? 0 : rpm);
        r.hasRpm = true;
        readAnything = true;
    }

    if (!t->pwmFile.empty()) {
        long long enable = 0;
        long long duty = 0;
        if (ReadInt(t->pwmEnableFile, enable) && enable == 1 && ReadInt(t->pwmFile, duty) &&
            t->pwmMax > t->pwmMin) {
            const long long span = t->pwmMax - t->pwmMin;
            r.reportedSpeedPercent = static_cast<uint32_t>(
                std::max<long long>(0, std::min<long long>(100, (duty - t->pwmMin) * 100 / span)));
            r.hasReportedSpeedPercent = true;
            readAnything = true;
        }
    }

    if (!readAnything) {
        res.code = FanStatusCode::ReadError;
        r.lastErrorMessage = "Nenhum dos ficheiros de " + t->dir + " respondeu à leitura.";
        res.message = r.lastErrorMessage;
        res.value = r;
        return res;
    }
    res.code = FanStatusCode::Ok;
    res.value = r;
    return res;
}

FanResult<bool> HwmonFanProvider::SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    res.value = false;
    const FanTarget* t = ResolveTarget(deviceId);
    if (!t) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Aparelho " + deviceId + " não está no hwmon deste sistema.";
        return res;
    }
    if (t->pwmFile.empty()) {
        res.code = FanStatusCode::Unsupported;
        res.message = "O chip " + t->chip + " não publica pwm para " + deviceId +
                      ": o controlo fica no firmware. Escrever por cima não é suportado.";
        return res;
    }

    uint32_t pct = speedPercent > 100 ? 100 : speedPercent;
    const long long span = t->pwmMax - t->pwmMin;
    const long long duty = t->pwmMin + (span * static_cast<long long>(pct)) / 100;

    FanStatusCode code = FanStatusCode::Ok;
    std::string msg;
    // pwmN_enable tem de ir a 1 (manual) antes do duty: com 0 o firmware ignora o valor escrito.
    if (!t->pwmEnableFile.empty()) {
        if (!WriteInt(t->pwmEnableFile, 1, code, msg)) {
            res.code = code;
            res.message = msg;
            return res;
        }
    }
    if (!WriteInt(t->pwmFile, duty, code, msg)) {
        res.code = code;
        res.message = msg;
        return res;
    }

    long long readBack = 0;
    const bool rbOk = ReadInt(t->pwmFile, readBack);
    if (!rbOk || readBack != duty) {
        res.code = FanStatusCode::Unverified;
        res.message = rbOk
            ? "Escrevi " + std::to_string(duty) + " em " + t->pwmFile + " e li " + std::to_string(readBack) +
              " de volta: o driver ajustou ou travou o duty."
            : "Escrevi " + std::to_string(duty) + " em " + t->pwmFile +
              " e a leitura de confirmação não respondeu.";
        return res;
    }
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Duty " + std::to_string(duty) + " (" + std::to_string(pct) + "% de " +
                  std::to_string(t->pwmMin) + ".." + std::to_string(t->pwmMax) + ") confirmado em " +
                  t->pwmFile + ".";
    return res;
}

FanResult<bool> HwmonFanProvider::RestoreDefaultControl(const std::string& deviceId) {
    FanResult<bool> res;
    res.value = false;
    const FanTarget* t = ResolveTarget(deviceId);
    if (!t) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Aparelho " + deviceId + " não está no hwmon deste sistema.";
        return res;
    }
    if (t->pwmEnableFile.empty()) {
        res.code = FanStatusCode::Unsupported;
        res.message = "O chip " + t->chip + " não expõe pwm*_enable: não há forma de pedir o modo do "
                      "firmware e o duty escrito fica activo. Deixo-o como está.";
        return res;
    }
    FanStatusCode code = FanStatusCode::Ok;
    std::string msg;
    if (!WriteInt(t->pwmEnableFile, 0, code, msg)) {
        res.code = code;
        res.message = msg;
        return res;
    }
    long long enable = 0;
    const bool enOk = ReadInt(t->pwmEnableFile, enable);
    if (!enOk || enable != 0) {
        res.code = FanStatusCode::Unverified;
        res.message = enOk
            ? "Pedi modo firmware em " + t->pwmEnableFile + " e li " + std::to_string(enable) + "."
            : "Pedi modo firmware em " + t->pwmEnableFile + " e a leitura de confirmação não respondeu.";
        return res;
    }
    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Modo firmware/BIOS restaurado em " + t->pwmEnableFile + ".";
    return res;
}

} // namespace opitcore
