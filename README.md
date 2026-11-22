<div align="center">

<img src="assets/store/icon_300x300.png" alt="Opit-Core Logo" width="130" height="130" />

# ⚡ OPIT-CORE

### Motor Adaptativo de Hardware, Desempenho, Energia e Governança Térmica para Windows

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](LICENSE)
[![Language: C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](CMakeLists.txt)
[![Platform: Windows 10/11](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20(x64%20%2F%20x86)-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#requisitos-e-compatibilidade)
[![Tests: 23 Suites](https://img.shields.io/badge/CTest-23%20Su%C3%ADtes%20Verificadas-2ea44f?style=for-the-badge)](#suíte-de-testes-e-verificação)
[![Telemetry: Zero](https://img.shields.io/badge/Telemetria-ZERO%20(100%25%20Offline)-critical?style=for-the-badge)](#privacidade-e-segurança-defensiva)

**Código aberto sob `GPL-3.0-only` • Sem ativação • Sem servidor de licença • Sem telemetria • Instalou, usou.**

[Como Baixar e Usar](#-como-baixar-e-usar-no-windows-guia-completo-e-arquivos-necessários) •
[Por que Open Source?](#-por-que-o-opit-core-se-tornou-open-source-gpl-30) •
[Arquitetura e Módulos](#-arquitetura-e-módulos) •
[Diferenciais Técnicos](#-três-compromissos-de-engenharia) •
[Como Compilar](#-como-compilar-do-código-fonte) •
[Licença e Proteção de Marca](#-licença-proteção-de-marca-e-direitos-exclusivos-gpl-30--seção-7) •
[Apoie (PIX)](#-apoie-o-projeto-donationware)

</div>

---

## 🚀 Como Baixar e Usar no Windows (Guia Completo e Arquivos Necessários)

### Passo a Passo Rápido (Versão Pronta em Releases)
1. Acesse a aba **[Releases](../../releases)** aqui no repositório do GitHub.
2. Em **Assets** (no final da página da versão `v8.1.2`), baixe o pacote correspondente ao seu sistema:
   - **`Opit-Core-8.1.2-x64.zip`** *(Windows de 64 bits — Recomendado)*
   - **`Opit-Core-8.1.2-x86.zip`** *(Windows de 32 bits)*
3. **Extraia (descompacte) TODOS os arquivos do `.zip` juntos na mesma pasta** (ex.: `C:\Opit-Core\`). **Não tire o `Opit-Core.exe` de dentro da pasta sozinho**, pois ele precisa das DLLs que estão ao lado dele.
4. Clique com o botão direito em **`Opit-Core.exe`** e selecione **"Executar como Administrador"** *(necessário para ajustar planos de energia ACPI e serviços do Windows)*.
   - *Aviso do Windows SmartScreen:* Como o software é gratuito e independente, o Windows pode exibir a tela azul *"O Windows protegeu o seu computador"*. Clique em **"Mais informações"** e depois em **"Executar assim mesmo"**.
5. **Para sair totalmente:** Clicar no `X` minimiza o programa para a bandeja do sistema (ao lado do relógio) para manter a proteção ativa. Para encerrar e restaurar as configurações originais do Windows, clique com o botão direito no ícone da bandeja e escolha **"Sair"**.

---

### 📦 Arquivos da Microsoft e de Terceiros: O que não vem de fábrica no Windows e onde colocar?

Para que o **Opit-Core** rode em qualquer computador sem dar erro de *"DLL ausente"* (como `VCRUNTIME140.dll não foi encontrado` ou `ucrtbase.dll ausente`), os arquivos abaixo devem ficar **exatamente na mesma pasta onde está o `Opit-Core.exe`** (no pacote `.zip` da aba *Releases* eles já vêm colocados no lugar certo; se você compilar o código-fonte do zero, veja onde obtê-los e onde colocá-los):

#### 1. Runtime Visual C++ da Microsoft (Não vem de fábrica em instalações limpas do Windows 10 / 11 / 8.1 / 7)
O Windows recém-formatado não traz as bibliotecas do Visual Studio 2022 instaladas por padrão. Você tem duas opções:
- **Opção A (Portátil — sem instalar nada no Windows):** Coloque estas **3 DLLs da Microsoft na mesma pasta do `Opit-Core.exe`** *(já incluídas no `.zip` de Release)*:
  - `MSVCP140.dll` *(Biblioteca padrão C++ da Microsoft)*
  - `VCRUNTIME140.dll` *(Runtime C do Visual Studio)*
  - `VCRUNTIME140_1.dll` *(Tratamento de exceções C++ x64 — necessário apenas na versão 64 bits)*
  - *Onde encontrar se você compilou do zero:* Em `C:\Windows\System32\` (para 64 bits) ou `C:\Windows\SysWOW64\` (para 32 bits), ou na pasta `VC\Redist\MSVC\` do Visual Studio.
- **Opção B (Instalador Oficial da Microsoft):** Se preferir instalar no sistema operacional (`C:\Windows\System32`), baixe e instale o pacote oficial gratuito **[Microsoft Visual C++ Redistributable 2015–2022](https://learn.microsoft.com/pt-br/cpp/windows/latest-supported-vc-redist)** (`vc_redist.x64.exe` para 64 bits ou `vc_redist.x86.exe` para 32 bits).

#### 2. Universal C Runtime — UCRT (Vem de fábrica no Windows 10 e 11, mas NÃO vem no Windows 7 SP1 / 8.1)
Nos sistemas **Windows 7 SP1** e **Windows 8.1**, a Microsoft não incluía o Universal CRT de fábrica. Para funcionar nesses sistemas sem depender do Windows Update, coloque estas **10 DLLs diretamente na mesma pasta do `Opit-Core.exe`** *(já incluídas no `.zip` de Release)*:
- `ucrtbase.dll`
- `api-ms-win-crt-convert-l1-1-0.dll`
- `api-ms-win-crt-filesystem-l1-1-0.dll`
- `api-ms-win-crt-heap-l1-1-0.dll`
- `api-ms-win-crt-locale-l1-1-0.dll`
- `api-ms-win-crt-math-l1-1-0.dll`
- `api-ms-win-crt-runtime-l1-1-0.dll`
- `api-ms-win-crt-stdio-l1-1-0.dll`
- `api-ms-win-crt-string-l1-1-0.dll`
- `api-ms-win-crt-time-l1-1-0.dll`
- *Onde encontrar se você compilou do zero:* No Windows 10 SDK em `C:\Program Files (x86)\Windows Kits\10\Redist\ucrt\DLLs\x64\` (ou `\x86\`).

#### 3. Microsoft .NET Framework 4.8 (Vem de fábrica no Windows 10 e 11, mas NÃO vem no Windows 7 SP1 / 8.1)
A ponte de leitura de sensores da placa-mãe (`opitcore_lhm_bridge.dll`) utiliza o `.NET Framework 4.8`:
- **No Windows 10 (versão 1903+) e Windows 11:** Já vem instalado de fábrica no sistema operacional (não precisa fazer nada).
- **No Windows 7 SP1 e Windows 8.1:** Não vem de fábrica. É necessário baixar e instalar o **[Microsoft .NET Framework 4.8 Runtime](https://dotnet.microsoft.com/pt-br/download/dotnet-framework/net48)** oficial da Microsoft antes de abrir o programa.

#### 4. Bibliotecas de Sensores de Placa-Mãe (`LibreHardwareMonitor` — Não vêm no Windows nem no código-fonte puro do GitHub)
Por regras de licenciamento do GitHub, o código-fonte do repositório não armazena DLLs compiladas de terceiros. Para que a leitura avançada de sensores Super I/O de placa-mãe funcione (em 64 bits), estes **10 arquivos devem ficar na mesma pasta do `Opit-Core.exe`** *(todos já vêm prontos dentro do `Opit-Core-8.1.2-x64.zip` na aba Releases)*:
- `opitcore_lhm_bridge.dll` *(Ponte C++/CLI compilada pelo projeto)*
- `LibreHardwareMonitorLib.dll` *(Motor de sensores MPL-2.0)*
- `HidSharp.dll`, `DiskInfoToolkit.dll`, `RAMSPDToolkit-NDD.dll`, `BlackSharp.Core.dll`
- `System.Buffers.dll`, `System.Memory.dll`, `System.Numerics.Vectors.dll`, `System.Runtime.CompilerServices.Unsafe.dll`

### Resumo Visual: Como deve ficar a pasta do programa para funcionar sem erros

```text
📁 Sua-Pasta-do-Opit-Core/
 ├── 🚀 Opit-Core.exe                                  <-- Executável principal (Mantenha junto com as DLLs abaixo!)
 ├── 📜 opit_manifest.json / opit_manifest.sig         <-- Manifesto assinado de integridade dos arquivos
 │
 ├── 🏢 [Arquivos da Microsoft — Visual C++ Runtime]   <-- Devem ficar na mesma pasta do .exe (ou em C:\Windows\System32)
 │    ├── MSVCP140.dll
 │    ├── VCRUNTIME140.dll
 │    └── VCRUNTIME140_1.dll                           (Apenas na versão x64)
 │
 ├── 🏢 [Arquivos da Microsoft — Universal CRT / UCRT] <-- Devem ficar na mesma pasta do .exe para compatibilidade total
 │    ├── ucrtbase.dll
 │    └── api-ms-win-crt-*.dll                         (9 arquivos api-ms-win-crt-*-l1-1-0.dll)
 │
 └── 🌡️ [Arquivos de Sensores — Ponte LhmBridge x64]   <-- Devem ficar na mesma pasta do .exe (versão x64)
      ├── opitcore_lhm_bridge.dll
      ├── LibreHardwareMonitorLib.dll
      ├── BlackSharp.Core.dll / DiskInfoToolkit.dll / HidSharp.dll / RAMSPDToolkit-NDD.dll
      └── System.Buffers.dll / System.Memory.dll / System.Numerics.Vectors.dll / System.Runtime.CompilerServices.Unsafe.dll
```

---

## 🎯 O que é o Opit-Core?

O **Opit-Core** é um orquestrador nativo em **C++17 / Win32** (~28,5 mil linhas de código, zero dependências de frameworks pesados de UI) que ajusta em tempo real os planos de energia (ACPI/PPM), limites de frequência de CPU, *core parking*, prioridades de processos, estados de serviços do Windows e curvas de ventoinhas.

Diferente de "otimizadores" que aplicam tweaks cegos no Registro do Windows e deixam o sistema instável, o Opit-Core opera em **malha fechada**: ele mede a temperatura, a carga real de CPU/GPU/RAM/E/S, o tempo de quadro (*frame time*) e o custo de cada transição — **e reverte absolutamente tudo ao estado original exato quando você fecha o programa**.

> [!IMPORTANT]
> **Natureza Não-Overclocking (Segurança de Silício):** O Opit-Core **não** faz overclocking nem *overvolting*. Ele opera estritamente dentro dos parâmetros nominais de fábrica (*stock*) fornecidos pelos fabricantes (Intel, AMD, NVIDIA, Microsoft) via APIs nativas do Windows e dos drivers oficiais.

---

## 🔓 Por que o Opit-Core se tornou Open Source (GPL-3.0)?

O Opit-Core nasceu originalmente como um software comercial fechado: exigia login, validava assinatura em servidor remoto, vinculava a licença ao hardware (*fingerprint*) e bloqueava recursos. Em novembro de 2026, toda essa camada de restrição e comunicação com servidores foi **definitivamente arrancada da árvore de código**.

A razão dessa decisão é direta e transparente: **na era atual da Inteligência Artificial, a obscuridade de binários deixou de existir.** Hoje, agentes e ferramentas de IA conseguem inspecionar o uso de memória, rastrear chamadas de sistema de um executável em execução e reconstruir um software equivalente a partir do seu comportamento. Tentar proteger um software local com DRM cada vez mais invasivo só pune o usuário legítimo e não impede a engenharia reversa assistida por IA.

Por isso, escolhemos o caminho oposto: **transparência total protegida pela lei do software livre (`GNU General Public License v3.0`)**.

- **Contra cópia fechada e apropriação:** A GPL-3.0 garante que o conhecimento e o código do Opit-Core pertençam à comunidade, mas **proíbe legalmente** que qualquer pessoa ou empresa copie este código (ou derive dele via IA ou refatoração), feche o código-fonte e o venda como produto proprietário.
- **Liberdade recíproca (*Copyleft*):** Qualquer um pode usar, estudar, auditar, modificar e distribuir o Opit-Core gratuitamente — desde que qualquer versão modificada ou derivada também seja publicada integralmente sob a **GPL-3.0**, preservando os créditos de autoria originais da **ACSTCLab**.

**Isto é software livre e *donationware*.** Não existe versão "Pro" escondida nem recursos trancados atrás de chave.

---

## 🧩 Arquitetura e Módulos

O projeto é dividido em **13 subsistemas modulares** em `src/`, desenhados com separação estrita entre **Observação → Decisão → Execucao → Prova de Reversão**:

```mermaid
flowchart LR
    subgraph Observação["1. Observação (Sensores & Telemetria Local)"]
        HW["hardware\n(CPU, GPU NVAPI/ADL, SMBIOS)"]
        MET["metrics\n(Frame Time, Potência, Carga)"]
        FAN_IN["fans\n(ACPI, NVAPI, ADL, LhmBridge)"]
    end

    subgraph Decisão["2. Decisão & Proteção"]
        ADP["adaptive\n(Diagnóstico de Gargalos)"]
        PROT["protection\n(Guardião Térmico)"]
        AI["ai\n(Copiloto BYOK Local)"]
    end

    subgraph Execução["3. Execução Reversível (Win32 / ACPI)"]
        PWR["power\n(Planos ACPI, Core Parking, Min/Max)"]
        CTL["control\n(Governador de Processos & Serviços)"]
        FAN_OUT["fans\n(Curvas PWM & Segurança)"]
    end

    subgraph Núcleo["4. Núcleo & Governança"]
        CORE["core\n(SQLite3, Diário de Rollback, Ciclo)"]
        SEC["security\n(Manifesto RSA-SHA256, .text Check)"]
        UI["ui\n(Interface Nativa Win32 - 14 Seções)"]
    end

    Observação --> Decisão
    Decisão --> Execução
    CORE --> Decisão
    CORE --> Execução
    SEC --> CORE
    UI --> CORE
```

| Módulo (`src/`) | Responsabilidade na Prática |
| :--- | :--- |
| **`core`** | Orquestrador central, ciclo de decisão determinístico, configuração persistida em **SQLite3** embutido e diário transacional de recuperação (*rollback journal*). |
| **`adaptive`** | Diagnostica em tempo real o gargalo dominante (CPU, GPU, Memória, E/S ou Térmico) e seleciona a política ideal com histerese para evitar oscilações. |
| **`power`** | Controle nativo de planos de energia do Windows (ACPI/PPM), limites Min/Max de processador para Tomada (AC) e Bateria (DC), e desbloqueio controlado de *Core Parking*. |
| **`control`** | Governador de prioridade de processos (com proteção de processos críticos do sistema) e governador de serviços do Windows com orçamento de impacto e trava anti-vaivém. |
| **`hardware`** | Reconhecimento profundo de hardware: topologia de CPU, telemetria de GPU (NVIDIA NVAPI / AMD / Intel) e leitura direta de tabelas **SMBIOS** (placa-mãe, BIOS e pentes de RAM). |
| **`fans`** | Controle de curvas de ventoinhas com histerese e controlador de segurança; suporta ACPI/WMI, NVIDIA NVAPI, AMD ADL, Intel e ponte opcional `LhmBridge` para Super I/O. |
| **`protection`** | Guardião térmico autônomo: monitora limites críticos de temperatura, aplica degradação progressiva de potência para proteger o hardware e restaura após o resfriamento. |
| **`metrics`** | Função-objetivo medida diretamente no sistema real (tempo de quadro DWM/DXGI, consumo de GPU em Watts, utilização por núcleo e pressão de memória). |
| **`platform/windows`** | Camada de abstração Win32: WMI, Service Control Manager (SCM), Registro transacional, APIs `Power*`, Agendador de Tarefas e verificação de elevação UAC. |
| **`security`** | Verificação defensiva de integridade: manifesto assinado (`RSA-SHA256`), checksum em memória da seção `.text`, mitigação contra *DLL Hijacking* e sanitização de logs locais. |
| **`ai`** | Copiloto de diagnóstico técnico estilo *BYOK (Bring Your Own Key)*: funciona exclusivamente se você digitar a **sua própria chave de API**, sem intermediários. |
| **`ui`** | Interface gráfica Win32 pura e ultraleve: 14 seções especializadas, Painel Unificado de Configurações, suporte a DPI *PerMonitorV2*, tema escuro e bandeja do sistema (*system tray*). |
| **`utils`** | Utilitários de alta performance, manipulação segura de strings UTF-8/UTF-16 e helpers de concorrência. |

---

## 🛡️ Três Compromissos de Engenharia

1. **Nada é feito às cegas (Execução Baseada em Evidência):**
   Cada decisão lê o estado real do sistema operacional **antes** e **depois** da chamada de API. Se uma alteração não puder ser comprovada pela leitura posterior, ela é classificada como *não provada* nos relatórios locais, nunca como um falso sucesso.
2. **Tudo é 100% reversível (Restauração Transacional):**
   Antes de tocar em qualquer plano de energia, prioridade de processo ou serviço, o estado original é gravado no diário de recuperação. Ao fechar o Opit-Core (ou mesmo na inicialização seguinte após uma queda de energia), o estado exato de origem é restaurado — uma chave que não existia volta a não existir.
3. **Zero Telemetria (Privacidade Absoluta por Design):**
   O Opit-Core **não coleta dados**, não possui analytics oculto e não conversa com servidores da ACSTCLab. Além disso, os logs gerados localmente na sua máquina passam por um sanitizador automático que mascara caminhos de pastas contendo o seu nome de usuário (`C:\Users\<usuario>`), permitindo compartilhar logs em *issues* do GitHub com segurança.

---

## 🔒 Privacidade e Segurança Defensiva

O módulo `src/security/` permanece ativo na versão Open Source com uma filosofia clara: **informar e proteger o usuário contra adulterações de terceiros, nunca bloquear sua liberdade.**

- **Prevenção contra Sequestro de DLL (*DLL Search Order Hijacking*):** Endurecimento imediato da ordem de busca de bibliotecas dinâmicas no início de `wWinMain`.
- **Control Flow Guard (`/guard:cf`), ASLR (`/DYNAMICBASE`), DEP (`/NXCOMPAT`) e Stack Canaries (`/GS`):** Habilitados por padrão na compilação MSVC.
- **Baseline de Memória (`.text`) e Manifesto Criptográfico (`RSA-SHA256`):** Detecta se binários pré-compilados foram corrompidos ou infectados após o build e registra nos diagnósticos locais. Em builds comunitários feitos a partir do código-fonte sem a chave privada da ACSTCLab, o programa compila e roda livremente sem qualquer bloqueio.

---

## 🛠️ Como Compilar do Código-Fonte

### Requisitos e Compatibilidade

- **Sistema Operacional:**
  - **Windows 10 e Windows 11 (x64 e x86):** Totalmente suportados e testados.
  - **Windows 7 SP1 e Windows 8.1:** Compatíveis a nível de binário (o SQLite3 é compilado junto ao executável sem depender de `winsqlite3.dll`, e APIs modernas do DWM são resolvidas dinamicamente em tempo de execução).
- **Ferramentas de Compilação:**
  - **Visual Studio 2022** (ou *Build Tools for Visual Studio 2022*) com a carga de trabalho *"Desenvolvimento para desktop com C++"* (MSVC C++17 e Windows 10/11 SDK).
  - **CMake 3.16** ou superior.

### Passo a Passo (PowerShell / Prompt de Comando)

#### 1. Compilar para 64 bits (x64 — Recomendado)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j 8 -- /nodeReuse:false
```
O executável final estará em: `build\Release\Opit-Core.exe`

#### 2. Compilar para 32 bits (x86)
```powershell
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release -j 8 -- /nodeReuse:false
```
O executável final estará em: `build-x86\Release\Opit-Core.exe`

> [!TIP]
> **Sensores opcionais via LibreHardwareMonitor (`LhmBridge`):**
> O código-fonte da ponte C++/CLI (`src/fans/lhm_bridge/`) acompanha o repositório. Se você colocar a biblioteca `LibreHardwareMonitorLib.dll` (MPL-2.0) e suas dependências na raiz do projeto antes de rodar o CMake, o alvo `opitcore_lhm_bridge.dll` será compilado e copiado automaticamente. Caso contrário, o CMake compila o `Opit-Core.exe` puro (100% nativo) utilizando os provedores de hardware nativos (ACPI/WMI, NVIDIA NVAPI, AMD ADL e Intel).

---

## 🧪 Suíte de Testes e Verificação

O Opit-Core conta com **23 suítes de testes automatizados** em `tests/`, compiladas como executáveis independentes e orquestradas pelo **CTest**. Nenhuma suíte depende de conexão de rede ou serviços externos:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

As suítes validam:
- Ciclo de vida de configurações, persistência SQLite3 e diário de recuperação (*rollback*);
- Detecção de hardware, topologia de CPU e parser SMBIOS;
- Governadores de planos de energia, prioridades de processos e estados de serviços;
- Cálculo de métricas, função-objetivo, curvas de ventoinhas e controlador de segurança térmica;
- Verificação criptográfica de manifesto, sanitização de privacidade, testes adversariais e responsividade da UI Win32.

---

## 📂 Estrutura do Repositório

```text
Opit-Core/
├── assets/                 # Ícones oficiais (.ico, .png) e recursos visuais
├── docs/                   # Documentação de arquitetura, compilação e avisos de terceiros
├── scripts/                # Script de geração e verificação de manifesto (PowerShell)
├── src/                    # Código-fonte principal C++17 (~28.500 linhas)
│   ├── adaptive/           # Diagnóstico de gargalos e motor de decisão adaptativa
│   ├── ai/                 # Copiloto de diagnóstico local (BYOK)
│   ├── control/            # Governadores de processos e serviços do Windows
│   ├── core/               # Orquestrador, configuração, SQLite e diário de reversão
│   ├── fans/               # Provedores de ventoinhas, curvas PWM e ponte LhmBridge
│   ├── hardware/           # Sondas de CPU, GPU (NVAPI/ADL) e SMBIOS
│   ├── metrics/            # Coleta de métricas reais e função-objetivo
│   ├── platform/windows/   # Integração de baixo nível com APIs Win32, WMI, SCM e ACPI
│   ├── power/              # Gerenciamento de planos de energia e Core Parking
│   ├── protection/         # Guardião térmico e proteção contra superaquecimento
│   ├── security/           # Integridade de binários, proteção de memória e logs sanitizados
│   ├── ui/                 # Interface gráfica nativa Win32 (14 painéis + bandeja)
│   ├── utils/              # Utilitários gerais de sistema e strings
│   ├── main.cpp            # Ponto de entrada (wWinMain), segurança e inicialização
│   └── OpitCore.rc         # Manifesto DPI PerMonitorV2, ícone e metadados de versão
├── tests/                  # 23 suítes de testes automatizados (CTest)
├── third_party/            # Dependências embutidas de código aberto:
│   ├── nlohmann/           # nlohmann/json (MIT License)
│   └── sqlite3/            # SQLite3 Amalgamation (Public Domain)
├── .gitignore              # Regras de exclusão de artefatos de build e chaves locais
├── CMakeLists.txt          # Configuração principal de build (CMake 3.16+)
├── LICENSE                 # Texto integral da GNU General Public License v3.0
└── README.md               # Este documento
```

---

## ⚖️ Licença, Proteção de Marca e Direitos Exclusivos (GPL-3.0 + Seção 7)

```text
Opit-Core — Motor adaptativo de hardware, desempenho e energia para Windows
Copyright (C) 2026 ACSTCLab (Todos os direitos autorais e de marca reservados à autora original)

Este programa é um software livre: você pode redistribuí-lo e/ou modificá-lo
sob os termos da Licença Pública Geral GNU (GNU General Public License),
conforme publicada pela Free Software Foundation, exclusivamente na versão 3
da Licença (GPL-3.0-only), sujeito aos Termos Adicionais da Seção 7 abaixo.

Este programa é distribuído na esperança de que possa ser útil, mas SEM
NENHUMA GARANTIA; mesmo sem a garantia implícita de COMERCIALIZAÇÃO ou
ADEQUAÇÃO A UM DETERMINADO FIM. Consulte a Licença Pública Geral GNU no
arquivo LICENSE e o arquivo TRADEMARK_AND_RIGHTS.md para obter mais detalhes.
```

### 🔒 Termos Adicionais Vinculantes (Seção 7 da GPL-3.0) e Exclusividade Comercial

Nos termos da **Seção 7 (Termos Adicionais)** da `GNU General Public License v3.0`, a **ACSTCLab** estabelece as seguintes restrições legais obrigatórias sobre este repositório e todo o seu conteúdo:

1. **Reserva Exclusiva da Família de Marcas (`Opit-*`), Nome e Identidade Visual em Todos os Sistemas Operacionais (`Seção 7(e)`):**
   A licença GPL-3.0 aplica-se ao código-fonte, mas **NÃO concede qualquer direito ou licença de uso sobre as marcas, família de marcas, nomes comerciais, identidade visual (*Trade Dress*) ou logotipos**.
   - Os nomes **"ACSTCLab"**, **"ACSTC Lab"**, **"Opit-Core"**, **"OpitCore"**, bem como toda a **Família de Marcas `Opit-*`** — incluindo expressamente **"Opit-Graphics"**, **"Opit-Graphic"**, **"Opit-Grafic"**, **"Opit-Sound"**, **"Opit-Audio"**, **"Opit-AI"**, **"Opit-Network"**, **"Opit-Memory"**, **"Opit-Storage"**, **"Opit-Boost"**, **"Opit-Engine"**, **"Opit-Pro"** e qualquer outra denominação de software ou serviço iniciada pelo prefixo **`"Opit-"`** ou **`"Opit"`** — e todos os logotipos/ícones presentes em `assets/` são de **titularidade e reserva exclusiva da ACSTCLab em todos os sistemas operacionais e plataformas (Windows, Ubuntu, Debian, Fedora, Arch, SteamOS e qualquer distribuição GNU/Linux, macOS, iOS, Android e Web)**.
   - **É estritamente proibido a terceiros** vender, cobrar assinatura, comercializar, realizar *ports* para outros sistemas operacionais usando a marca, publicar em lojas de aplicativos ou repositórios de pacotes (**Microsoft Store, Winget, Chocolatey, Scoop, Ubuntu Snap Store / Snapcraft, Flathub / Flatpak, AppImageHub, APT/PPA, AUR, Steam, Epic Games Store, Apple App Store, Google Play**), registrar ou utilizar domínios web da marca (**`acstclab.com`**, **`acstclab.com.br`**, **`acstclab.*`**, **`opitcore.*`**, **`opit-core.*`** ou **`opit-*.*`**, sujeitos a retomada por má-fé via SACI-Adm/Registro.br e UDRP/ICANN) ou distribuir versões originais, modificadas (*forks*) ou softwares derivados utilizando o nome **"Opit-Core"**, **"Opit-Graphics" / "Opit-Graphic" / "Opit-Grafic"**, **"Opit-Sound"**, qualquer variação **`Opit-*`**, a marca **"ACSTCLab"** ou os ícones oficiais do projeto.
   - Qualquer versão modificada ou port para outro sistema operacional distribuído por terceiros deve manter o código aberto sob a GPL-3.0 e remover integralmente o prefixo **`Opit`**, a marca **`ACSTCLab`** e seus logotipos da interface e dos executáveis (*rebranding completo*), além de declarar visivelmente que não é uma versão oficial (`Seção 7(c)` e `7(d)`).
2. **Exclusividade de Assinatura e Licenciamento Comercial (*Dual-Licensing*):**
   Como única detentora original da totalidade dos Direitos Autorais (*Copyright*) do **Opit-Core**, **somente a ACSTCLab** detém a prerrogativa jurídica exclusiva de:
   - Lançar versões futuras, edições comerciais, planos de assinatura ou módulos sob licença proprietária fechada;
   - Oferecer pacotes comerciais oficiais, garantias ou suporte pago sob a marca **Opit-Core**.
   - **Nenhum terceiro** está autorizado a fechar este código, adicionar chaves de ativação/assinatura pagas ou se apropriar do projeto para exploração comercial proprietária.
3. **Isenção Absoluta de Responsabilidade sobre Hardware, Queima de Componentes, Sistema Operacional e Dados (`Seções 15 e 16`) + Indenização por Terceiros (`Seção 7(f)`):**
   - O software é disponibilizado gratuitamente *"NO ESTADO EM QUE SE ENCONTRA" (AS IS)*, e **o usuário assume 100% do risco e da responsabilidade exclusiva** pelo uso do programa em seu computador.
   - **Nem a autora nem a ACSTCLab têm qualquer culpa ou responsabilidade civil, técnica ou financeira** caso o computador do usuário apresente superaquecimento, queima ou dano físico/elétrico em qualquer peça de hardware (**CPU, GPU/Placa de Vídeo, Placa-Mãe, RAM, SSD/HD, Fonte, Bateria ou Ventoinhas/Water Cooler**), seja por mau uso, curvas de ventoinhas mal configuradas pelo usuário, falha mecânica de refrigeração, leitura imprecisa de sensores ou defeito prévio do equipamento, bem como por **Telas Azuis (BSOD), travamentos, corrupção do Windows, falha de drivers/serviços ou perda de arquivos/dados**.
   - Nenhum usuário ou terceiro possui direito de exigir suporte técnico, conserto, atualizações ou indenização da **ACSTCLab** (responsabilidade financeira igual a **zero**). Nos termos da **Seção 7(f)**, qualquer terceiro que redistribuir ou modificar o código assume 100% da responsabilidade legal perante terceiros e obriga-se a indenizar e isentar a **ACSTCLab** de qualquer reclamação.
4. **Proteção da Arquitetura/Comportamento do Orquestrador (*SSO*), Esquemas de Dados e Contra Clonagem por IA ou Inspeção de Memória:**
   A proteção autoral do **Opit-Core** abrange não apenas o código literal em C++17, mas toda a **Estrutura, Sequência, Organização (*SSO — Structure, Sequence, and Organization*) e Comportamento Sistêmico do seu Orquestrador Adaptativo em Malha Fechada** — incluindo: *(1)* Pipeline de 4 estágios (*Observação Multimodal* ➔ *Diagnóstico de Gargalo com Histerese e Trava Anti-Vaivém* ➔ *Execução Coordenada ACPI/Processos/Serviços/PWM* ➔ *Verificação Pós-Execução Baseada em Evidência e Função-Objetivo*); *(2)* *Diário Transacional de Reversão Exata e Esquema de Banco SQLite3* (Art. 7º, XIII da Lei 9.610/98); *(3)* *Guardião Térmico Progressivo*; *(4)* *Ponte Isolada de Sensores C ABI sobre C++/CLI (`OpitLhmBridge`)*; *(5)* *Arquitetura de Segurança Defensiva em Camadas (`.text` baseline pós-ASLR, manifesto RSA-SHA256 e `PrivacySanitizer`)*; *(6)* *Motor de Diagnóstico de Hardware por IA Multi-Provedor*; e *(7)* *Trade Dress e UX das 14 Seções Win32 Nativas*. Qualquer software, módulo ou código gerado em qualquer linguagem (C#, Rust, Python, Go, C++, etc.) ou produzido com auxílio de **IA (LLMs, agentes de codificação, inspeção de memória RAM, rastreamento de chamadas de sistema ou tradução automatizada)** que replique ou extraia esse comportamento e arquitetura constitui juridicamente **Plágio Estrutural e Obra Derivada** (Seções 0 e 5 da GPL-3.0 e Art. 5º, VIII, "g" e Art. 7º, XII e XIII da Lei 9.610/98), sendo estritamente proibido o seu uso em programas fechados ou comercialização proprietária por terceiros. Esta especificação também constitui **Publicação Defensiva (*Prior Art*)** internacional, impedindo o patenteamento desses mecanismos por terceiros.
5. **Prova Criptográfica de Autoria (`RSA-2048`) e Foro Judicial no Brasil:**
   A titularidade original do projeto é provada matematicamente pela posse da chave privada correspondente à chave pública RSA-2048 oficial embutida em `src/security/ManifestVerifier.cpp`. Qualquer violação resulta na rescisão imediata da licença (Seção 8 da GPL-3.0), aplicando-se as Leis Brasileiras (nº 9.609/98, 9.610/98 e 9.279/96) no Foro do domicílio da titular da **ACSTCLab** no Brasil. Consulte [`TRADEMARK_AND_RIGHTS.md`](TRADEMARK_AND_RIGHTS.md) para o texto jurídico completo.

### Resumo Prático dos Direitos

| ✅ O que a Comunidade PODE fazer | 🚫 O que é PROIBIDO a terceiros (Exclusivo da ACSTCLab) |
| :--- | :--- |
| ✔️ **Usar gratuitamente** o Opit-Core em seu computador pessoal ou profissional. | ❌ **Proibido usar o nome "Opit-Core", a marca "ACSTCLab" ou a logo oficial** para vender o software ou cobrar assinaturas. |
| ✔️ **Estudar e auditar** o código-fonte aberto para aprendizado e transparência. | ❌ **Proibido fechar o código-fonte** ou criar versões pagas com DRM/chave de ativação a partir deste código. |
| ✔️ **Modificar o código** para uso próprio (mantendo a GPL-3.0 e os créditos originais de Copyright da ACSTCLab no código). | ❌ **Proibido remover os avisos de Copyright da ACSTCLab** ou alegar autoria sobre o motor e sua arquitetura. |
| ✔️ **Contribuir** com melhorias sabendo que a titularidade comercial do projeto pertence à ACSTCLab. | ❌ **Proibido exigir garantias, suporte ou responsabilização** da autora pelo uso do software gratuito. |

> [!NOTE]
> **Licenças de Terceiros Compatíveis:**
> O repositório inclui `nlohmann/json` (**MIT**) e `SQLite3` (**Domínio Público**). A ponte opcional `LhmBridge` interage em tempo de execução com `LibreHardwareMonitorLib` (**MPL-2.0**). Consulte [`docs/THIRD_PARTY_LICENSES.txt`](docs/THIRD_PARTY_LICENSES.txt) e [`TRADEMARK_AND_RIGHTS.md`](TRADEMARK_AND_RIGHTS.md) para os termos completos.

---

## 🤝 Como Contribuir (Acordo de Cessão — CLA)

Contribuições técnicas são bem-vindas! Ao enviar um *Pull Request* ou contribuição para este repositório, você declara que a contribuição é de sua autoria e **concede à ACSTCLab uma licença perpétua, irrevogável, mundial e irrestrita (incluindo o direito de relicenciamento comercial e distribuição sob qualquer licença, aberta ou fechada)** sobre o código enviado, mantendo também a distribuição pública sob a **GPL-3.0**.

Para manter a qualidade de engenharia do Opit-Core, seguimos três regras de ouro:

1. **Números e medições, não adjetivos:** PRs que dizem *"melhora a velocidade do PC"* sem medição não são aceitos. Documente o estado medido antes e depois com a mesma métrica.
2. **Prova de reversão obrigatória:** Qualquer nova rotina que altere uma configuração do Windows deve incluir a lógica de reversão exata no encerramento e um teste correspondente em `tests/`.
3. **Ausência declarada:** Se um sensor ou métrica não estiver disponível num hardware específico, o código deve reportar explicitamente como *não medido / não suportado*, nunca inventar ou estimar números falsos.

---

## 💚 Apoie o Projeto (Donationware)

O **Opit-Core** é 100% gratuito, livre de anúncios, livre de telemetria e sem qualquer recurso bloqueado.

Se o programa ajudou a reduzir a temperatura do seu computador, silenciou suas ventoinhas, economizou bateria ou estabilizou a taxa de quadros dos seus jogos, considere apoiar o desenvolvimento contínuo com uma contribuição voluntária via **PIX**:

<div align="center">

### 💠 Chave PIX (E-mail): `alexcstos@gmail.com`
**Mantenedora:** ACSTCLab

</div>

---

<div align="center">
  <sub>Desenvolvido com rigor em C++17 por <b>ACSTCLab</b> • Distribuído sob a licença <b>GNU GPL v3.0</b>.</sub>
</div>
