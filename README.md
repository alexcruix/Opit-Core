# 🚀 OptiCore Engine v7.2 PRO

**OptiCore** é uma solução avançada de orquestração e otimização para Microsoft Windows (10/11). Desenvolvida em **Python**, a ferramenta atua como um sistema inteligente de gestão de hardware, focada em maximizar a responsividade em jogos (anti-lag), gerir a eficiência térmica e otimizar o consumo de energia de forma dinâmica e reversível.

---

## 🛠️ Arquitetura do Sistema

O OptiCore opera através de um motor de ciclo contínuo (thread dedicada) que segue o fluxo:
**Coleta de Telemetria** ➡️ **Decisão (Motores de IA)** ➡️ **Ação (Controladores)**



### 1. Monitorização em Tempo Real (Telemetry)
* **FPS & Frametime:** Detecção de *stuttering* e *lag* com precisão de milissegundos.
* **Hardware:** Monitorização completa de uso (CPU/GPU), consumo (Watts), temperaturas e RPM de ventoinhas.
* **Processos:** Identificação inteligente de aplicações em primeiro plano e priorização de jogos.

### 2. Motores de Inteligência (Intelligence Layers)
* **AntiLagEngine:** Dispara impulsos de performance ao detetar quedas de frametime.
* **CoreControl:** Gestão de afinidade de núcleos, priorizando **P-cores** para jogos e **E-cores** para processos de fundo em CPUs híbridas.
* **ThermalIntelligence:** Analisa tendências térmicas e aplica *throttling* preventivo ou ajustes de ventoinhas para evitar o superaquecimento.
* **LearningEngine:** Aprende com o uso do utilizador, registando estatísticas de performance por aplicação em `learning_data.json`.

### 3. Controladores e Atuadores (Actuators)
* **Power Management:** Alternância dinâmica entre planos de energia (Eco, Equilibrado, Alta Performance) com ajuste de *Core Parking*.
* **GPU Control:** Limitação de potência (Power Limit) via camadas de hardware (NVIDIA/WDDM).
* **Fan Control:** Curvas personalizadas e watchdog térmico via integração com *LibreHardwareMonitor*.
* **OS Optimization:** Ajuste da resolução do timer do Windows para **1ms** e gestão de políticas de QoS e Firewall para redução de latência de rede.

---

## 📂 Estrutura do Projeto

| Pasta | Descrição |
| :--- | :--- |
| `core/` | Motor principal, barramento de eventos e sistemas de rede. |
| `monitor/` | Sensores de telemetria (CPU, GPU, FPS, Processos). |
| `intelligence/` | Motores de decisão e lógica de aprendizagem. |
| `controllers/` | Atuadores de sistema (Energia, Fans, Processos, GPU). |
| `ui/` | Interface de utilizador (Bandeja do sistema e Dashboard). |
| `actions/` | Definições GUI, widgets e ferramentas de recuperação. |

---

## ⚙️ O que o OptiCore altera?

O programa atua a nível de software para otimizar o hardware de forma segura:
- ✅ **Planos de Energia:** Ajustes finos de frequências e suspensão de núcleos.
- ✅ **Prioridade de Processos:** Garante que o seu jogo tenha preferência total da CPU.
- ✅ **Latência de Rede:** Aplica regras de Firewall e QoS exclusivas para o tráfego de jogos.
- ✅ **Sistema:** Limpeza de RAM (MemoryOrchestrator) e compactação NTFS de pastas temporárias.

> [!NOTE]  
> **Segurança em primeiro lugar:** O OptiCore NÃO realiza overclock de hardware. Todas as alterações são baseadas em políticas do SO e são revertidas automaticamente ao fechar o programa.

---

## 🚀 Como Iniciar

### Requisitos Mínimos
* **SO:** Windows 10 ou 11.
* **Privilégios:** Execução como Administrador (necessário para controle de hardware).
* **Dependências:** Python 3.x, `psutil`, `customtkinter`, `pystray`.

### Instalação Oficial
Para garantir a melhor estabilidade e atualizações automáticas, utilize a versão distribuída na **Microsoft Store**.

---

## 🛡️ Modos de Segurança
* **Monitor-Only:** O programa apenas observa e reporta dados, sem aplicar alterações.
* **Safe Mode:** Força comportamentos conservadores para máxima estabilidade do sistema.
* **Restauração Automática:** Em caso de fecho inesperado, o sistema recupera o "Baseline" (estado original) no próximo arranque.

---
*Desenvolvido com foco em performance e eficiência por **ACSTCLab**.*
