================================================================================
  OptiCore — Leitura completa do programa (descrição funcional)
  Documento gerado a partir da análise do código-fonte do repositório.
  Versão referida no código: OptiCore Engine v7.2 PRO
================================================================================

1. O QUE É O OPTICORE
--------------------------------------------------------------------------------
O OptiCore é uma aplicação para Microsoft Windows (10/11) escrita em Python,
com interface gráfica (CustomTkinter), ícone na bandeja do sistema (pystray) e
um motor em segundo plano (thread dedicada) que percorre um ciclo contínuo:

    Coleta de telemetria → Decisão (motores de inteligência) → Ação (controladores)

Objectivos declarados no código e na documentação: melhorar responsividade
em jogos (anti-lag), gestão térmica e de ventoinhas, planos de energia,
afinidade e prioridade de processos, e estimativas de consumo — com ênfase
em modos reversíveis (snapshots, baseline, restauração ao sair).


2. PONTOS DE ENTRADA (COMO A APLICAÇÃO ARRANCA)
--------------------------------------------------------------------------------
2.1 Caminho principal (o que o executável / main.py usa)
    - Ficheiro: main.py
    - Configura logging (setup_opticore_logging), faulthandler opcional, tema
      escuro CustomTkinter.
    - Verifica privilégios de administrador (ctypes / IsUserAnAdmin); regista
      aviso se não for admin — na prática muitas acções continuam a tentar
      arrancar (comentário no código indica que o bloqueio total por falta de
      admin pode ser relaxado em produção).
    - Cria CTk() oculto (withdraw), instancia OptiCoreEngine, chama engine.start(),
      cria OptiCoreTray(engine), arranca a bandeja em modo "detached" (compatível
      com Tk na thread principal no Windows), agenda janela de boas-vindas /
      recuperação (show_recovery_welcome), e entra em root.mainloop().

2.2 Caminho legado / alternativo (código ainda presente no repositório)
    - Ficheiro: actions/gui.py — classe SystemTrayApp com MonitoringLoop
      (logic/monitor.py). É uma arquitectura mais antiga "unificada" com Tk
      noutra thread. O fluxo principal actual do produto é main.py + core/engine.py;
      este ficheiro pode existir para compatibilidade ou desenvolvimento antigo.


3. NÚCLEO: OptiCoreEngine (core/engine.py)
--------------------------------------------------------------------------------
O OptiCoreEngine é o orquestrador central. Responsabilidades principais:

3.1 Monitorização (instâncias)
    - FPSMonitor (monitor/fps.py): frametime, detecção de lag/stutter/stall;
      em jogo pode receber "inject" de frametime a partir do limite de FPS
      configurado por jogo (evita falsos positivos de lag quando o tick do motor
      é ~1 s).
    - CPUMonitor: uso, watts estimados, dados por núcleo.
    - GPUMonitor: uso e telemetria (com fallback WDDM / LHM conforme hardware).
    - ThermalMonitor (monitor/temp.py): temperaturas e taxa de subida (rise rate).
    - ProcessMonitor: processo em primeiro plano, se é "jogo" (lista vinda de
      configuração + jogos por defeito internos no construtor).

3.2 Motores de inteligência (pasta intelligence/)
    - AntiLagEngine: com jogo em foco, analisa o FPSMonitor; em caso de mau
      frametime dispara decisão "boost_perf" (com limite de acções por segundo).
      Em desktop não usa o frametime como lag (evita boost no Explorer).
    - CoreControlEngine: escalonamento progressivo do número de "núcleos
      desbloqueados" para lógica interna; expõe máscaras de afinidade para jogo
      (prioriza P-cores em CPUs híbridas Intel quando disponível) e para fundo
      (E-cores ou primeiros threads).
    - AutoModeEngine: perfis "game_desktop", "game_notebook", "desk_desktop",
      "desk_notebook"; lê bateria / tomada; se térmica crítica limita agressividade
      em jogo ou força eco.
    - LearningEngine: regista tempo por aplicação, sessões, frequência; grava
      eventos de lag por app em learning_data.json.
    - ThermalIntelligenceEngine: histórico curto de temperaturas; detecta subida
      rápida; sugere cap de GPU, forçar plano equilibrado, enviesar ventoinhas,
      fan a 100% em limiares altos de CPU/GPU.
    - PriorityEngine: com process_controls nível max, percorre processos e
      pode rebaixar prioridade de processos pesados em segundo plano (exclui
      lista crítica: Explorer, svchost, serviços base, etc.).
    - FrameTimeEngine: calcula alvos de frametime/FPS aproximados por perfil
      (telemetria/OSD; não impõe limite no driver).

3.3 Controladores (pasta controllers/)
    - PowerPlanController: troca entre planos eco / balanced / high_perf com
      contextos de atraso (notebook vs desktop, jogo, etc.) e restauração do
      baseline capturado ao início.
    - ProcessorPlanMinMaxTuning: ajustes reversíveis PROCTHROTTLEMIN/MAX nos
      planos; snapshot e restore_pending_from_disk após crash.
    - CoreParkingTuning: snapshots por GUID de esquema; alinhado com tuning de
      "threads activos" no plano actual.
    - ThermalGuardian: reduz agressividade do CPU (PROCTHROTTLEMAX) quando
      temperatura ou taxa de subida indicam perigo; restore em disco para crash.
    - FanController: modos auto/manual/turbo; curvas por temperatura; PID;
      escrita via LibreHardwareMonitor quando activo; watchdog térmico; perfis
      quiet/balanced/aggressive com offsets.
    - ProcessController: afinidade e prioridade por PID com reversão segura;
      compactação NTFS da pasta temp do utilizador (opt-in no orquestrador).
    - GPUController: limite de potência em percentagem (50–100%), via camada
      HardwareInfo (tipicamente nvidia-smi quando disponível).
    - OSOptimizer: NtSetTimerResolution para ~1 ms (melhor granularidade de timer
      no Windows); opcionalmente parar/iniciar alguns serviços (SysMain, WSearch,
      Spooler) e limites GPU legados via nvidia-smi (paralelo ao GPUController).

3.4 Outros subsistemas ligados à engine
    - EventBus: emissão de eventos (ex.: lag_detectado).
    - PingMonitor (core/ping_monitor.py): opcional; ping a um host configurável;
      notificações na bandeja; foco em apps "sensíveis" ou jogo.
    - HardwareInfo (core/hardware.py, monitor/hardware.py): informação de
      hardware, TDP, temperaturas, fans, limites GPU, etc. (usado em vários módulos).
    - Rede: importa remove_all_opticore_firewall_rules e
      remove_all_opticore_qos_policies — ao sair, em modo monitor-only ou
      restauração, remove regras criadas pelo módulo de rede da aplicação.
      A aplicação propriamente dita das regras está na UI de definições
      (apply_network_controls_from_config em settings_gui).

3.5 Configuração e ficheiros
    - settings.json: safe_mode, modes.monitor_only, pause_until_ts, ping_alerts,
      tips_state, perfil_automatico, etc.
    - throttling_config.json: orquestrador (chaves apply_* por módulo),
      níveis min/med/max, lista de jogos (exe, modo CPU, modo GPU, FPS, afinidade
      híbrida max_p_cores / max_e_cores), OSD, fan_mode, fan_control_ids,
      engine_loop_sleep_sec, preço kWh, moeda, widgets, network_controls (quando
      exposto na UI), etc.
    - stats.json + pasta history_stats (ou history): total_saved_wh e histórico
      diário para dashboard.
    - learning_data.json: escrito pelo LearningEngine.

3.6 Modos de segurança e aplicação de mudanças
    - monitor_only (defeito True em settings modes): não aplica optimizações;
      ao activar, tenta reverter planos, min/max, core parking, GPU, fans,
      processos, guardião, regras de rede.
    - pause_until_ts: pausa temporizada das optimizações.
    - safe_mode / safety_profile observe: força comportamento mais conservador;
      cautious aumenta floor de atraso nas mudanças de plano de energia.
    - orchestrator_enabled e cada apply_*: só com tudo coerente e can_apply True
      é que o motor aplica planos, processos, GPU, fans, RAM, deep economy, etc.
      can_apply exige: is_opt, não safe_mode, is_auto, orch_enabled, não
      monitor_only, não is_paused().

3.7 Ciclo principal (_main_loop) — resumo do fluxo por iteração
    1) Actualiza CPU, GPU, processos.
    2) Se jogo: actualiza FPS / inject frametime; senão reset ao desktop.
    3) Lê watts totais estimados, temperaturas CPU/GPU, RPMs de fans.
    4) Actualiza ThermalMonitor e Learning (track_app).
    5) AutoModeEngine.decide(is_game) → perfil; power_delay_context para planos.
    6) ThermalIntelligenceEngine.analyze → acções sugeridas (cap GPU, fan_bias,
       force_balanced, fast_rise, fan_full).
    7) Opcional: ThermalGuardian.update (se apply_thermal_guardian e can_apply);
       em emergência pode forçar eco se planos estiverem activos.
    8) Deep Economy (apply_deep_economy): se CPU baixa e tempo idle longo,
       força plano eco; sai com input ou carga — restaura baseline.
    9) AntiLagEngine.decide → boost_perf / cpu_limit / gpu_limit / normal.
    10) FrameTimeEngine.target_frametime_ms (telemetria).
    11) CoreControlEngine.decide (escalonamento unlocked_cores vs uso CPU).
    12) Se can_apply:
        - Core parking via _maybe_apply_core_parking (powercfg / WindowsOptimizer).
        - Processos: afinidade e prioridade do jogo; PriorityEngine em nível max
          para fundo; regras diferentes em safety_profile cautious.
        - GPU: release em boost; cap sugerido pela térmica; preferências por jogo
          (gpu_mode turbo/eco).
        - Plano de energia alvo: combina térmica, bateria, core_strategy, auto_bias,
          overrides por jogo; PowerPlanController.set_plan se apply_power_plans.
        - Fans: perfil resolvido + update térmico com pull nocturno opcional.
        - A cada 300 s: opcional compact NTFS temp; RAM compression com
          MemoryOrchestrator.compress_cold_pages em intervalos que dependem do nível.
    13) Baseline de watts e acumulação heurística de "energia poupada".
    14) Actualiza estado para UI (CPU, GPU, RAM, fans, estabilidade).
    15) Dicas (generate_tips) a cada N ciclos.
    16) OSD / RTSS: actualiza OSDManager se configurado; overlay standalone Tk.
    17) Persiste stats periodicamente; audit/heartbeat opcional; sleep do loop
        (0,5–5 s, ou OPTICORE_ENGINE_SLEEP_SEC / engine_loop_sleep_sec).

3.8 Arranque da engine (start)
    - Define resolução de timer alta (1 ms) excepto OPTICORE_TEST_MODE=1.
    - Inicia thread do _main_loop e PingMonitor.

3.9 Encerramento (shutdown)
    - Para o loop (is_running=False), ping monitor, repõe fans (reset hardware),
      ProcessorPlanMinMax, core parking snapshots, ThermalGuardian, plano de
      energia baseline, remove regras firewall/QoS OptiCore, fallback
      WindowsOptimizer.restore_core_parking, persiste stats, GPU release_full,
      ProcessController.restore_all, agenda destruição Tk na thread principal
      e repõe timer do Windows.


4. INTERFACE DO UTILIZADOR
--------------------------------------------------------------------------------
4.1 Bandeja (ui/tray.py — OptiCoreTray)
    - Ícone pystray com estado visual; menu de contexto; integração Win32
      (WM_NOTIFY, refresh suave na thread correcta para não corromper Tk).
    - Fila de notificações para Shell_NotifyIcon na thread do ícone.
    - Ligação à engine para notify, abrir dashboard/definições, pausa,
      monitor-only, saída, etc.

4.2 Dashboard, definições, widgets, OSD
    - engine.open_dashboard / open_settings / toggle_widget abrem janelas
      CustomTkinter no root oculto.
    - actions/settings_gui.py: janela extensa de configuração (inclui módulo
      network_controls: firewall por executável, QoS por executável quando
      disponível).
    - ui/dashboard.py, actions/widget.py: HUD / monitores flutuantes.
    - core/osd_manager.py + overlay: métricas em ecrã; opção RTSS na config.

4.3 Primeira execução / recuperação
    - actions/recovery_welcome.py: janela amigável de segurança (agendada após
      arranque).


5. MÓDULOS DE APOIO RELEVANTES
--------------------------------------------------------------------------------
5.1 core/system_check.py — verificações (admin, QoS, etc.) para relatório na UI.
5.2 core/recommendations.py — dicas contextuais (CPU/GPU/temperatura/RAM/jogo).
5.3 core/idle.py — segundos sem input do utilizador (Deep Economy).
5.4 core/step_audit.py — trilho de auditoria opcional do motor (debug/CI).
5.5 core/backup_restore.py — cópias / restauração de configurações (quando usado
      pelos fluxos de UI ou manutenção).
5.6 core/utils.py — caminhos de config, JSON, comandos discretos, normalização
      de nomes de executáveis de jogos.
5.7 logic/system_optimizer.py — WindowsOptimizer (powercfg, core parking),
      PowerShifter, MemoryOrchestrator (compressão de páginas "frias"), usados pelo
      motor legado e por pontos da engine.
5.8 logic/game_mode.py / actions/controller.py — integrações legadas com o loop
      antigo MonitoringLoop.
5.9 tests/test_opticore_core.py — testes unitários / smoke do núcleo (ex.: modo
      OPTICORE_TEST_MODE).


6. PASTAS DE CÓDIGO (VISÃO DE ARQUITECTURA)
--------------------------------------------------------------------------------
core/          Motor, engine, rede, ping, logging, auditoria, recomendações
monitor/       Leitura CPU, GPU, FPS, processos, temperatura, hardware
intelligence/  Decisão: anti-lag, auto modo, térmica, prioridade, cores, frames
controllers/   Actuadores: energia, CPU, GPU, fans, processos, OS, guardião
ui/            Bandeja, dashboard
actions/       Definições GUI, widgets, recuperação, gui legado
logic/         Monitor legado, optimizador Windows, traduções, game mode
config/        Ficheiros JSON por defeito (distribuídos ou gerados em runtime)


7. O QUE O PROGRAMA PODE ALTERAR NO SISTEMA E NO HARDWARE (RESUMO)
--------------------------------------------------------------------------------
- Planos de energia do Windows e parâmetros associados (throttle min/max, core
  parking / número efectivo de threads activos no plano).
- Prioridade e máscara de afinidade de CPU de processos seleccionados (jogo em
  foco; processos pesados em fundo em níveis agressivos).
- Limite de potência da GPU (quando a stack suporta, tipicamente NVIDIA com
  nvidia-smi).
- Curvas ou percentagens de ventoinhas via LibreHardwareMonitor, quando o
  hardware expõe controlos; reset ao BIOS/automático ao sair conforme implementação.
- Resolução global do timer do Windows (1 ms) durante a sessão da aplicação.
- Serviços Windows opcionais (SysMain, WSearch, Spooler) se essa via for chamada.
- Regras de Firewall do Windows e políticas QoS (NetQos) com prefixo OptiCore,
  configuráveis na UI — removidas em shutdown / monitor-only / restauração rápida.
- Compactação NTFS da pasta temp do utilizador (opt-in).
- Heurísticas de RAM (MemoryOrchestrator) — opt-in apply_ram_compression.

O programa NÃO substitui a BIOS para overclock "duro"; as alterações são sobretudo
ao nível do SO e de políticas reversíveis, com caminhos de restauração.


8. REQUISITOS E DEPENDÊNCIAS TÍPICAS
--------------------------------------------------------------------------------
- Windows 10/11.
- Python 3.x + pacotes em requirements.txt (psutil, pystray, customtkinter,
  Pillow, pythonnet para LHM, etc.) quando se corre a partir do código-fonte.
- LibreHardwareMonitor (DLL) para máxima cobertura de sensores e controlo de
  fans — a aplicação pode funcionar com funcionalidades reduzidas sem isso.
- Muitas operações requerem execução como Administrador.


9. NOTA SOBRE "LEITURA COMPLETA"
--------------------------------------------------------------------------------
Este documento resume todos os subsistemas identificados nos módulos Python
principais da raiz do projecto (excluindo builds em dist/, staging MSIX e
bibliotecas embutidas). O ficheiro actions/settings_gui.py é muito extenso e
concentra grande parte das opções de interface; qualquer nova secção de menu
adicionada pelo autor aparecerá aí e em throttling_config.json.

Para detalhe linha-a-linha, use o próprio código-fonte e os comentários de cada
módulo.

================================================================================
Fim do documento
================================================================================
