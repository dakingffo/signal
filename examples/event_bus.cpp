#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <vector>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include "signal.hpp" 

using namespace daking;
using namespace stdexec;
using namespace std::chrono_literals;

// --- 1. Signal Contracts (Events) ---
struct OnSystemReady     : signal<std::string>      { using base::base; };      // FirmWare Version
struct OnTelemetryUpdate : signal<double, double>   { using base::base; };      // Temperature, Power Load
struct OnProductionStep  : signal<std::string, int> { using base::base; };      // Step Name, Completion %
struct OnEmergencyStop   : signal<int, std::string> { using base::base; };      // Error Code, Reason
 
// --- 2. Advanced Factory Engine ---
class FactoryController : public enable_signal<
    OnSystemReady, OnTelemetryUpdate, OnProductionStep, OnEmergencyStop
> {
public:
    // Orchestrate a high-load production line
    auto run_production_line(std::string batch_id) {
        return just(std::move(batch_id))
            | then([this](std::string id) {
                emit(OnSystemReady{"v2.0.4-LTS"}, broadcast, this);
                return id;
            })
            | then([this](std::string id) {
                // Simulate telemetry heartbeats in a loop
                for (int i = 0; i < 5; ++i) {
                    std::this_thread::sleep_for(100ms);
                    OnTelemetryUpdate(45.5 + i, 800.0 + (i * 50)) >> emit(broadcast, this);
                    OnProductionStep{"Assembling", (i + 1) * 20} >> emit(broadcast, this);
                }
                
                if (id == "BATCH_ERR_99") {
                    OnEmergencyStop{ 99, "Thermal Overload Detected"} >> emit(broadcast, this);
                    throw std::runtime_error("Hardware Failure");
                }
                return id + " SUCCESS";
            });
    }
};

// --- 3. Visualization Utilities ---
struct Visualizer {
    static void print_header(const std::string& title) {
        std::cout << "\n\033[1;34m" << std::string(50, '=') << "\n"
                  << " SYSTEM: " << title << "\n"
                  << std::string(50, '=') << "\033[0m" << std::endl;
    }

    static void draw_progress(const std::string& step, int percent) {
        int width = 20;
        int pos = width * percent / 100;
        std::cout << "\033[1;32m[PROD]\033[0m " << std::left << std::setw(15) << step 
                  << " [\033[1;33m" << std::string(pos, '#') << std::string(width - pos, ' ') 
                  << "\033[0m] " << percent << "%" << std::endl;
    }
};

int main() {
    exec::static_thread_pool pool{8}; // Increased concurrency
    auto sch = pool.get_scheduler();
    
    FactoryController controller;

    // --- 4. Subscriber Dynamic Orchestration ---

    // A. The Telemetry Dashboard (Low-level Data)
    auto conn_telemetry = daking::connect<OnTelemetryUpdate>(controller, 
        then([](double temp, double load) {
            std::cout << "\033[1;90m[TELEMETRY] Temp: " << temp << "°C | Load: " << load << "kW\033[0m" << std::endl;
        })
    );

    // B. The Production UI (Visual Feedback)
    auto conn_ui = daking::connect<OnProductionStep>(controller, 
        then([](std::string step, int percent) {
            Visualizer::draw_progress(step, percent);
        })
    );

    // C. Safety Interlock (Critical Actions)
    auto conn_safety = daking::connect<OnEmergencyStop>(controller, 
        then([](int code, std::string reason) {
            std::cerr << "\n\033[1;31m[!!! EMERGENCY STOP !!!]\033[0m\n"
                      << "Error: " << code << " | Reason: " << reason << std::endl;
        })
    );

    // --- 5. Execution ---

    Visualizer::print_header("STARTING NOMINAL PRODUCTION");
    sync_wait(on(sch, controller.run_production_line("GOLD_BATCH_001")));

    Visualizer::print_header("STARTING STRESS TEST (FAILURE SIMULATION)");
    
    // Disable UI updates to simulate a "headless" or "blinded" state, 
    // but the safety interlock must remain active!
    conn_ui.disable(); 
    
    try {
        sync_wait(on(sch, controller.run_production_line("BATCH_ERR_99")));
    } catch (...) {
        std::cout << "\033[1;31mPipeline terminated. \033[1;32m [Meet expectations]\033[0m" << std::endl;
    }

    return 0;
}