#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace opitcore {

// Baloes de notificacao do tray — o "olhar" da IA avisando a usuaria quando o motor VE algo
// acontecendo: jogo detectado, modo economia, plano trocado, servico pausado/devolvido, processo
// protegido pelo Windows, recomendacao nova da IA. Coalescido por categoria para nao virar chuva.
class NotificationManager {
public:
    enum class Category {
        ModeChange = 0,  // Modo Jogo / Economia Maxima ligado ou desligado, jogo detectado
        PlanSwitch = 1,  // Orquestrador trocou o plano de energia
        Service = 2,     // Servico pausado ou devolvido (com ganho medido)
        Process = 3,     // Prioridade de processo escrita ou negada pelo Windows
        Thermal = 4,     // Guardiao termico / teto de CPU restaurado
        AiAdvice = 5,    // Recomendacao nova da analise periodica da IA
        Count = 6
    };

    static NotificationManager& Instance();

    // Janela principal registra o balao do tray; sem sink as notificacoes ficam so no log.
    using Sink = std::function<void(const std::wstring& title, const std::wstring& message)>;
    void SetSink(Sink sink);

    void Notify(Category cat, const std::string& titleUtf8, const std::string& messageUtf8);

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enable);
    void LoadSettings();

private:
    NotificationManager() = default;
    mutable std::mutex m_mutex;
    Sink m_sink;
    bool m_enabled = true;
    uint64_t m_lastNotifyMs[static_cast<int>(Category::Count)] = {0, 0, 0, 0, 0, 0};
};

} // namespace opitcore
