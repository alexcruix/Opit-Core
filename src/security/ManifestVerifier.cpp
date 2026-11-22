#include "ManifestVerifier.h"
#include "core/Logger.h"
#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

namespace opitcore {
namespace security {

namespace {

// Chave pública RSA-2048 (SPKI ASN.1 Base64) oficial de produção da ACSTC Lab
constexpr char k_official_spki_b64[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvDdQueeRVcvikbWkysOX"
    "MEb9Vhqc15kXs0rOlxDijnUVV/ex3Jf/7tiTJ7N9HIIU8G+YaCIqtY/s0KvlY5zc"
    "Rf/UEqgIo4eNRZWHINK8qpNfuigX0uzdni0NFEw+VtYDVds8YxX24tfWgoX6gL6Z"
    "cJWnrNu88Z9Uy057OLmPoxUKnGKPt3KxYyNOd9OgKPbme6WWj0osc97k20FEwQ+d"
    "G2zUcp+Q+buKaB8kmjXsT5A35MW5B67moYYY0s4I7IbCsS1mXUB06nu3RUvsn3Ac"
    "VrpDrqRJtHWnwXzbIICBV+VQsq4g33PN7yXAOtps58LLrctEMjWflQcJXJzJ6PJp"
    "yQIDAQAB";

bool Base64Decode(const std::string& in, std::vector<BYTE>& out) {
    if (in.empty()) return false;
    DWORD need = 0;
    if (!CryptStringToBinaryA(in.c_str(), static_cast<DWORD>(in.size()), CRYPT_STRING_BASE64, nullptr,
                              &need, nullptr, nullptr)) {
        return false;
    }
    out.resize(need);
    return CryptStringToBinaryA(in.c_str(), static_cast<DWORD>(in.size()), CRYPT_STRING_BASE64,
                                out.data(), &need, nullptr, nullptr) == TRUE;
}

bool VerifyRsaSha256(const std::string& payload, const std::string& signatureB64, const std::string& spkiB64) {
    std::vector<BYTE> spki;
    if (!Base64Decode(spkiB64, spki) || spki.empty()) {
        return false;
    }

    std::vector<BYTE> sigBe;
    if (!Base64Decode(signatureB64, sigBe) || sigBe.empty()) {
        return false;
    }

    PCERT_PUBLIC_KEY_INFO pki = nullptr;
    DWORD pki_len = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, spki.data(),
                             static_cast<DWORD>(spki.size()), CRYPT_DECODE_ALLOC_FLAG, nullptr,
                             &pki, &pki_len) || !pki) {
        return false;
    }

    HCRYPTPROV prov = 0;
    HCRYPTKEY key = 0;
    HCRYPTHASH hash = 0;
    bool ok = false;

    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        LocalFree(pki);
        return false;
    }
    if (!CryptImportPublicKeyInfo(prov, X509_ASN_ENCODING, pki, &key)) {
        CryptReleaseContext(prov, 0);
        LocalFree(pki);
        return false;
    }
    LocalFree(pki);

    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptDestroyKey(key);
        CryptReleaseContext(prov, 0);
        return false;
    }
    if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(payload.data()),
                       static_cast<DWORD>(payload.size()), 0)) {
        CryptDestroyHash(hash);
        CryptDestroyKey(key);
        CryptReleaseContext(prov, 0);
        return false;
    }

    // Windows CryptoAPI espera Little-Endian
    std::vector<BYTE> sigLe(sigBe.rbegin(), sigBe.rend());
    ok = (CryptVerifySignatureA(hash, sigLe.data(), static_cast<DWORD>(sigLe.size()), key, nullptr, 0) == TRUE);

    CryptDestroyHash(hash);
    CryptDestroyKey(key);
    CryptReleaseContext(prov, 0);
    return ok;
}

} // namespace

ManifestVerifier::ManifestVerifier() = default;

void ManifestVerifier::SetCustomPublicKeySpki(const std::string& spkiB64) {
    m_customSpkiB64 = spkiB64;
}

IntegrityStatus ManifestVerifier::VerifyAndLoad(const std::wstring& manifestPath,
                                               const std::wstring& signaturePath,
                                               std::vector<FileIntegrityRecord>& outRecords,
                                               std::string& outError) {
    outRecords.clear();
    outError.clear();

    std::ifstream manifestFile(manifestPath, std::ios::binary);
    if (!manifestFile.is_open()) {
        outError = "Arquivo de manifesto nao encontrado";
        return IntegrityStatus::ManifestMissing;
    }

    std::stringstream manifestSs;
    manifestSs << manifestFile.rdbuf();
    std::string jsonPayload = manifestSs.str();
    manifestFile.close();

    std::ifstream sigFile(signaturePath, std::ios::binary);
    if (!sigFile.is_open()) {
        outError = "Arquivo de assinatura do manifesto nao encontrado";
        return IntegrityStatus::UntrustedSignature;
    }

    std::stringstream sigSs;
    sigSs << sigFile.rdbuf();
    std::string sigContent = sigSs.str();
    sigFile.close();

    // Remove whitespace/newlines da assinatura base64 se houver
    while (!sigContent.empty() && (sigContent.back() == '\r' || sigContent.back() == '\n' || sigContent.back() == ' ')) {
        sigContent.pop_back();
    }

    return VerifyPayload(jsonPayload, sigContent, outRecords, outError);
}

IntegrityStatus ManifestVerifier::VerifyPayload(const std::string& jsonPayload,
                                                const std::string& signatureB64,
                                                std::vector<FileIntegrityRecord>& outRecords,
                                                std::string& outError) {
    outRecords.clear();
    outError.clear();

    if (jsonPayload.empty()) {
        outError = "Manifesto vazio";
        return IntegrityStatus::ManifestInvalid;
    }

    const std::string& spkiToUse = m_customSpkiB64.empty() ? k_official_spki_b64 : m_customSpkiB64;

    if (!VerifyRsaSha256(jsonPayload, signatureB64, spkiToUse)) {
        outError = "Assinatura digital do manifesto invalida ou nao confiavel";
        return IntegrityStatus::UntrustedSignature;
    }

    try {
        auto j = nlohmann::json::parse(jsonPayload);
        if (!j.is_object() || !j.contains("files") || !j["files"].is_array()) {
            outError = "Formato do manifesto incompativel (campo 'files' ausente)";
            return IntegrityStatus::ManifestInvalid;
        }

        for (const auto& item : j["files"]) {
            if (!item.contains("path") || !item.contains("sha256")) {
                continue;
            }
            FileIntegrityRecord rec;
            rec.relativePath = item["path"].get<std::string>();
            rec.expectedSha256 = item["sha256"].get<std::string>();
            rec.componentId = item.value("component", "core");
            rec.isCritical = item.value("critical", true);
            outRecords.push_back(std::move(rec));
        }

        if (outRecords.empty()) {
            outError = "Manifesto nao contem arquivos catalogados";
            return IntegrityStatus::ManifestInvalid;
        }

        return IntegrityStatus::Valid;
    } catch (const std::exception& e) {
        outError = std::string("Falha ao analisar JSON do manifesto: ") + e.what();
        return IntegrityStatus::ManifestInvalid;
    }
}

} // namespace security
} // namespace opitcore
