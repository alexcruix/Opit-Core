// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "ManifestVerifier.h"
#include "core/Logger.h"
#include <nlohmann/json.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

#else

#include "utils/StringUtils.h"
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <vector>

#endif

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

#ifdef _WIN32

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

#else

// Os simbolos EVP e d2i do libcrypto sao resolvidos em tempo de execucao (dlopen), como o NVAPI e
// o DWM no Windows. Provar a assinatura exige-os: sem eles a rota devolve false e o manifesto e
// classificado como UntrustedSignature. Nada e jamais declarado integro por falta de medio.
struct CryptoFunctions {
    const void* (*EVP_sha256)() = nullptr;
    void* (*EVP_MD_CTX_new)() = nullptr;
    void (*EVP_MD_CTX_free)(void*) = nullptr;
    int (*EVP_DigestVerifyInit)(void*, void**, const void*, void*, void*) = nullptr;
    int (*EVP_DigestVerifyUpdate)(void*, const void*, size_t) = nullptr;
    int (*EVP_DigestVerifyFinal)(void*, const unsigned char*, size_t) = nullptr;
    void (*EVP_PKEY_free)(void*) = nullptr;
    void* (*d2i_PUBKEY)(void**, const unsigned char**, long) = nullptr;
    bool loaded = false;
};

const CryptoFunctions& Crypto() {
    static CryptoFunctions f;
    static bool attempted = false;
    if (attempted) return f;
    attempted = true;
    void* h = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return f;
    f.EVP_sha256 = reinterpret_cast<decltype(f.EVP_sha256)>(dlsym(h, "EVP_sha256@@OPENSSL_3.0.0"));
    if (!f.EVP_sha256) f.EVP_sha256 = reinterpret_cast<decltype(f.EVP_sha256)>(dlsym(h, "EVP_sha256"));
    f.EVP_MD_CTX_new = reinterpret_cast<decltype(f.EVP_MD_CTX_new)>(dlsym(h, "EVP_MD_CTX_new"));
    f.EVP_MD_CTX_free = reinterpret_cast<decltype(f.EVP_MD_CTX_free)>(dlsym(h, "EVP_MD_CTX_free"));
    f.EVP_DigestVerifyInit =
        reinterpret_cast<decltype(f.EVP_DigestVerifyInit)>(dlsym(h, "EVP_DigestVerifyInit"));
    f.EVP_DigestVerifyUpdate =
        reinterpret_cast<decltype(f.EVP_DigestVerifyUpdate)>(dlsym(h, "EVP_DigestVerifyUpdate"));
    f.EVP_DigestVerifyFinal =
        reinterpret_cast<decltype(f.EVP_DigestVerifyFinal)>(dlsym(h, "EVP_DigestVerifyFinal"));
    f.EVP_PKEY_free = reinterpret_cast<decltype(f.EVP_PKEY_free)>(dlsym(h, "EVP_PKEY_free"));
    f.d2i_PUBKEY = reinterpret_cast<decltype(f.d2i_PUBKEY)>(dlsym(h, "d2i_PUBKEY"));
    f.loaded = f.EVP_sha256 && f.EVP_MD_CTX_new && f.EVP_MD_CTX_free && f.EVP_DigestVerifyInit &&
               f.EVP_DigestVerifyUpdate && f.EVP_DigestVerifyFinal && f.EVP_PKEY_free && f.d2i_PUBKEY;
    return f;
}

// Decodificador base64 de fluxo: mantem no maximo 8 bits por confirmar, por isso nenhum
// caractere invalido consegue empurrar o acumulador para la do intervalo definido.
bool Base64Decode(const std::string& in, std::vector<unsigned char>& out) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out.clear();
    if (in.empty()) return false;

    int val = 0;
    int bits = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '=') break;
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == '-') continue;
        const char* found = static_cast<const char*>(std::memchr(kAlphabet, c, 64));
        if (!found) return false;
        val = (val << 6) | static_cast<int>(found - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((val >> bits) & 0xFF));
            val &= (1 << bits) - 1;
        }
    }
    return !out.empty();
}

bool VerifyRsaSha256(const std::string& payload, const std::string& signatureB64, const std::string& spkiB64) {
    const CryptoFunctions& cf = Crypto();
    if (!cf.loaded) return false;

    std::vector<unsigned char> spki;
    if (!Base64Decode(spkiB64, spki) || spki.empty()) {
        return false;
    }

    std::vector<unsigned char> sig;
    if (!Base64Decode(signatureB64, sig) || sig.empty()) {
        return false;
    }

    // O SPKI (SubjectPublicKeyInfo ASN.1) e o mesmo DER que no Windows e dado ao
    // CryptImportPublicKeyInfo; aqui entra direto no d2i_PUBKEY.
    const unsigned char* derPtr = spki.data();
    void* pkey = cf.d2i_PUBKEY(nullptr, &derPtr, static_cast<long>(spki.size()));
    if (!pkey) return false;

    void* ctx = cf.EVP_MD_CTX_new();
    if (!ctx) {
        cf.EVP_PKEY_free(pkey);
        return false;
    }

    bool ok = false;
    if (cf.EVP_DigestVerifyInit(ctx, nullptr, cf.EVP_sha256(), nullptr, pkey) == 1 &&
        cf.EVP_DigestVerifyUpdate(ctx, payload.data(), payload.size()) == 1) {
        // O OpenSSL recebe a assinatura como o inteiro em big-endian, tal como o arquivo base64:
        // a invertagem que o CryptoAPI exigia nao tem equivalente aqui.
        ok = (cf.EVP_DigestVerifyFinal(ctx, sig.data(), sig.size()) == 1);
    }

    cf.EVP_MD_CTX_free(ctx);
    cf.EVP_PKEY_free(pkey);
    return ok;
}

#endif

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

#ifdef _WIN32
    std::ifstream manifestFile(manifestPath, std::ios::binary);
#else
    // O std::ifstream de libstdc++ aceita apenas o caminho em UTF-8: a conversao e a unica
    // diferenca para o corpo identico do Windows.
    std::string manifestUtf8 = StringUtils::ToUtf8(manifestPath);
    std::ifstream manifestFile(manifestUtf8, std::ios::binary);
#endif
    if (!manifestFile.is_open()) {
        outError = "Arquivo de manifesto nao encontrado";
        return IntegrityStatus::ManifestMissing;
    }

    std::stringstream manifestSs;
    manifestSs << manifestFile.rdbuf();
    std::string jsonPayload = manifestSs.str();
    manifestFile.close();

#ifdef _WIN32
    std::ifstream sigFile(signaturePath, std::ios::binary);
#else
    std::string signatureUtf8 = StringUtils::ToUtf8(signaturePath);
    std::ifstream sigFile(signatureUtf8, std::ios::binary);
#endif
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
