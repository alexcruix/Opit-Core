#pragma once

#include <string>
#include <vector>
#include <memory>
#include "SecurityTypes.h"

namespace opitcore {
namespace security {

class ManifestVerifier {
public:
    ManifestVerifier();
    ~ManifestVerifier() = default;

    /**
     * Define uma chave pública SPKI alternativa (para testes determinísticos de chave não confiável).
     * Se vazia, utiliza a chave pública oficial embutida de produção.
     */
    void SetCustomPublicKeySpki(const std::string& spkiB64);

    /**
     * Valida e carrega o manifesto de integridade.
     * @param manifestPath Caminho para opit_manifest.json
     * @param signaturePath Caminho para opit_manifest.sig (assinatura RSA-SHA256 em Base64 ou binária)
     * @param outRecords Vetor de registros de integridade preenchido em caso de sucesso
     * @param outError Detalhe textual do erro (para logs defensivos)
     */
    IntegrityStatus VerifyAndLoad(const std::wstring& manifestPath,
                                 const std::wstring& signaturePath,
                                 std::vector<FileIntegrityRecord>& outRecords,
                                 std::string& outError);

    /**
     * Valida diretamente um payload JSON e sua assinatura em Base64.
     */
    IntegrityStatus VerifyPayload(const std::string& jsonPayload,
                                  const std::string& signatureB64,
                                  std::vector<FileIntegrityRecord>& outRecords,
                                  std::string& outError);

private:
    std::string m_customSpkiB64;
};

} // namespace security
} // namespace opitcore
