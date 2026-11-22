#include "metrics/SystemMonitor.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    opitcore::SystemMonitor mon;
    if (!mon.Initialize()) {
        std::cerr << "Falha ao inicializar SystemMonitor" << std::endl;
        return 1;
    }

    // Amostra 1 (calibração de tempo)
    mon.Sample();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Amostra 2
    auto snap = mon.Sample();
    if (snap.cpuUsagePercent < 0.0 || snap.cpuUsagePercent > 100.0) {
        std::cerr << "Falha: CPU usage percent fora da faixa: " << snap.cpuUsagePercent << std::endl;
        return 1;
    }
    if (snap.ramTotalMB == 0) {
        std::cerr << "Falha: RAM total = 0" << std::endl;
        return 1;
    }
    if (snap.ramUsedMB > snap.ramTotalMB) {
        std::cerr << "Falha: RAM usada > RAM total" << std::endl;
        return 1;
    }

    std::cout << "test_metrics: SUCESSO (CPU: " << snap.cpuUsagePercent << "%, RAM: " 
              << snap.ramUsedMB << "/" << snap.ramTotalMB << " MB, Temp Sensor: " 
              << (snap.hasTempSensor ? "Detectado" : "Nao disponivel") << ")" << std::endl;
    return 0;
}
