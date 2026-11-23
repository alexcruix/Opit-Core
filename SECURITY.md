# Política de Segurança — Opit-Core

A **ACSTC Lab** leva a segurança e a integridade do **Opit-Core** a sério, especialmente por se tratar de um software nativo que interage com recursos de baixo nível do sistema operacional (planos de energia ACPI/PPM, prioridades de processos, serviços e sensores de hardware).

## Versões Suportadas

| Versão | Suporte de Segurança |
| :--- | :---: |
| **8.2.x (Atual)** | ✅ Suportada |
| `< 8.2.0` | ❌ Descontinuada |

## Como Reportar uma Vulnerabilidade

**Não abra uma Issue pública** para relatar falhas de segurança exploráveis (como escalonamento de privilégio, sequestro de DLL, corrupção de memória ou adulteração de integridade).

Envie um relato detalhado diretamente para:
- **E-mail de Segurança:** `alexcstos@gmail.com`
- **Assunto sugerido:** `[SECURITY] Opit-Core — <resumo da falha>`

Inclua, sempre que possível:
1. Versão exata e arquitetura (`x64` ou `x86`) do `Opit-Core.exe`.
2. Versão do Windows e passos claros para reproduzir o problema.
3. Logs relevantes (os logs locais do Opit-Core já mascaram caminhos de usuário automaticamente via `PrivacySanitizer`).

## Verificação de Autenticidade dos Binários Oficiais

Os binários oficiais da **ACSTC Lab** são distribuídos **exclusivamente** pela aba [Releases](https://github.com/alexcruix/Opit-Core/releases) do repositório oficial e acompanham:
1. **Hashes criptográficos oficiais** no arquivo `SHA256SUMS.txt` da Release.
2. **Manifesto criptográfico assinado (`opit_manifest.json` + `opit_manifest.sig`)** verificado em tempo de execução pela chave pública RSA-2048 oficial embutida em `src/security/ManifestVerifier.cpp`.
3. **Verificação de integridade em memória (`.text` baseline pós-ASLR)** e mitigação ativa contra *DLL Search Order Hijacking*.

Desconfie de qualquer cópia baixada fora dos canais oficiais cujo hash `SHA-256` não confira com o `SHA256SUMS.txt` oficial.
