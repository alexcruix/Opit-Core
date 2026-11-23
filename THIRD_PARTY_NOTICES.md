# Avisos de Componentes de Terceiros — Opit-Core

O código principal do **Opit-Core** (motor adaptativo, orquestrador, interface Win32, governadores e segurança) é de autoria da **ACSTC Lab** e distribuído sob a **[PolyForm Shield License 1.0.0](LICENSE)**.

Este projeto utiliza os seguintes componentes de terceiros, cada um regido por sua respectiva licença original:

---

## 1. nlohmann/json
- **Localização no repositório:** `third_party/nlohmann/json.hpp`
- **Autor:** Niels Lohmann (`Copyright (c) 2013-2022 Niels Lohmann`)
- **Licença:** MIT License
- **Uso:** Serialização e leitura de configurações, manifestos e diagnósticos JSON.

## 2. SQLite3 (Amalgamation)
- **Localização no repositório:** `third_party/sqlite3/`
- **Autores:** D. Richard Hipp e contribuidores do SQLite
- **Licença:** Domínio Público (*Public Domain*)
- **Uso:** Banco de dados local embutido para persistência de configurações e diário transacional de reversão (*rollback journal*).

## 3. LibreHardwareMonitor / LibreHardwareMonitorLib *(Opcional em tempo de execução)*
- **Integração:** Ponte opcional C++/CLI em `src/fans/lhm_bridge/` (`opitcore_lhm_bridge.dll`)
- **Licença:** Mozilla Public License 2.0 (**MPL-2.0**)
- **Uso:** Leitura de sensores Super I/O de placa-mãe e ventoinhas PWM em pacotes de 64 bits.
- **Nota de conformidade MPL-2.0:** O código-fonte da ponte `src/fans/lhm_bridge/` encontra-se disponível neste repositório. As bibliotecas compiladas (`LibreHardwareMonitorLib.dll` e dependências NuGet `.NET` como `HidSharp`, `DiskInfoToolkit`, `RAMSPDToolkit-NDD`, `BlackSharp.Core` e `System.*`) pertencem aos seus respectivos autores e seguem suas próprias licenças.

## 4. Bibliotecas de Runtime da Microsoft *(Incluídas apenas nos pacotes binários de Release)*
- **Arquivos:** `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `ucrtbase.dll` e `api-ms-win-crt-*-l1-1-0.dll`
- **Titular:** Microsoft Corporation
- **Uso:** Redistribuíveis oficiais do Microsoft Visual C++ Runtime e Universal C Runtime (UCRT) para compatibilidade portátil no Windows. Não fazem parte do código-fonte deste repositório.
