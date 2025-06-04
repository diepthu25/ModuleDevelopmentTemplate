#include "VisionRT/vision_rt.h"
#include "utils/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <set>
#include <signal.h>
#include <atomic>
#include <memory> 
#include <ModuleA/moduleA.h>
#include <ModuleB/moduleB.h>
#include <ModuleTypes.h>
#include <any>

/*

This show an example of using VisionRT API
to manage the system & software modules.

This take a example of get int a & int b from console
as an resource and using ModuleA to calculate a+b -> save c to workspace
and ModuleB to print the result to console.

Distribute level: Internal-circulation.
Versys Research @2025

*/
// Create an instance of VisionRT manager
std::unique_ptr<VisionRT> g_vision_rt_manager;
// Bool state to shutdown the application
std::atomic<bool> g_shutdown_flag{false};

void SignalHandler(int signum) {
    SystemLogger::info("MainApp", "Interrupt signal (" + std::to_string(signum) + ") received. Setting shutdown flag.");
    g_shutdown_flag.store(true, std::memory_order_relaxed);
}
// A helper function to convert ModuleState to string
std::string ModuleStateToString(ModuleState state) {
    switch (state) {
        case ModuleState::IDLE: return "IDLE";
        case ModuleState::INITIALIZING: return "INITIALIZING";
        case ModuleState::RUNNING: return "RUNNING";
        case ModuleState::PAUSING: return "PAUSING";
        case ModuleState::PAUSED: return "PAUSED";
        case ModuleState::RESUMING: return "RESUMING";
        case ModuleState::STOPPING: return "STOPPING";
        case ModuleState::STOPPED: return "STOPPED";
        case ModuleState::ERROR_SETUP: return "ERROR_SETUP";
        case ModuleState::ERROR_RUNTIME: return "ERROR_RUNTIME";
        default: return "UNKNOWN_STATE (" + std::to_string(static_cast<int>(state)) + ")";
    }
}

// A support function to clean module.
void HandleModuleCleanup(const ModuleID& mod_id, const std::string& reason) {
    if (!g_vision_rt_manager) return;

    SystemLogger::info("MainApp", "Module '" + mod_id + "' " + reason + ". Initiating cleanup.");
    g_vision_rt_manager->TerminateModule(mod_id, true); // join=true
    g_vision_rt_manager->UnregisterModule(mod_id);
    SystemLogger::info("MainApp", "Module '" + mod_id + "' cleanup complete.");
}


int main(){
    /*
    
    int a, b;
    std::cin >> a >> b;
    int c = a + b;
    std::cout << "Result: " << c << std::endl;
    
    */
    // std::cout << "Hello, World!" << std::endl;
    // First initialize the logger
    SystemLogger::initialize("modules_test.log");
    // Log the start of application
    SystemLogger::info("MainApp", "Application starting...");
    // Register the signal handler for debugging
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // A setup schedule would be:
    // 1. Initialize VisionRT (Construct VisionRT)
    // 2. Register modules
    // 3. Register resources
    // 4. Invoke module
    // 5. Main loop
    // 6. Cleanup
    // 7. Exit

    // Initialize VisionRT
    g_vision_rt_manager = std::make_unique<VisionRT>();

    // Create ModuleA & ModuleB instance then
    // Register ModuleA & B to VisionRT
    auto mA = std::make_shared<ModuleA>("ModuleA", "v1.0.0"); // new ModuleA() | std::make_shared<ten module>("ModuleA" <- ID module);
    auto mB = std::make_shared<ModuleB>("ModuleB", "v1.0.0");

    SystemLogger::info("MainApp", "All module loaded. Pending registration.");

    if(g_vision_rt_manager->RegisterModule(mA)){
        SystemLogger::info("MainApp", "ModuleA registered successfully.");
    }else{
        // handle module registration failure
        SystemLogger::error("MainApp", "ModuleA registration failed.");
    }
    if(g_vision_rt_manager->RegisterModule(mB)){
        SystemLogger::info("MainApp", "ModuleB registered successfully.");
    }else{
        // handle module registration failure
        SystemLogger::error("MainApp", "ModuleB registration failed.");
    }
    
    //register resources
    // - Create a SimpleResource instance
    std::shared_ptr<SimpleResource> res_instance = std::make_shared<SimpleResource>();
    // - Get data from console
    res_instance->input();
    // - Register the resource with VisionRT as name "input"
    //   using std::any to store the resource
    if(g_vision_rt_manager->RegisterResource("input", res_instance)){
        SystemLogger::info("MainApp", "Resource 'input' registered successfully.");
    }else{
        // handle resource registration failure
        SystemLogger::error("MainApp", "Resource 'input' registration failed.");
        return -1;
    }

    // Invoke the modules
    if(g_vision_rt_manager->InvokeModule("ModuleA")){ // -> tạo thread life-cycle setup loop exit
        SystemLogger::info("MainApp", "ModuleA invoked successfully.");
    }
    if(g_vision_rt_manager->InvokeModule("ModuleB")){
        SystemLogger::info("MainApp", "ModuleA invoked successfully.");
    }

    while (!g_shutdown_flag.load(std::memory_order_relaxed)) { // Giữ cho main luôn chạy -> không bị dừng
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if(g_vision_rt_manager){
            ModuleState mA_state = g_vision_rt_manager->GetModuleState("ModuleA");
            ModuleState mB_state = g_vision_rt_manager->GetModuleState("ModuleB");
            ModuleState states[2] = {mA_state, mB_state};
            std::string state_names[2] = {"ModuleA", "ModuleB"};
            for(int i=0; i<2; i++){
                if(states[i] == ModuleState::IDLE){
                    SystemLogger::info("MainApp", state_names[i] + " is IDLE.");
                }else if(states[i] == ModuleState::RUNNING){
                    SystemLogger::info("MainApp", state_names[i] + " is RUNNING.");
                }else if(states[i] == ModuleState::PAUSED){
                    SystemLogger::info("MainApp", state_names[i] + " is PAUSED.");
                }else if(states[i] == ModuleState::STOPPED){
                    SystemLogger::info("MainApp", state_names[i] + " is STOPPED.");
                }else{
                    SystemLogger::info("MainApp", state_names[i] + " is in unknown state.");
                }
            }
        }
    }

    SystemLogger::info("MainApp", "Main loop exited. Initiating full system shutdown...");

    std::vector<ModuleID> modules_to_shutdown;
    if (g_vision_rt_manager) {
        modules_to_shutdown = g_vision_rt_manager->ListAllModules();
    } else {
        SystemLogger::warning("MainApp", "VisionRT manager is null during shutdown sequence.");
    }

    SystemLogger::info("MainApp", "Phase 1: Signaling all active modules to stop.");
    for (const auto& mod_id : modules_to_shutdown) {
        if (g_vision_rt_manager) {
            ModuleState current_state = g_vision_rt_manager->GetModuleState(mod_id);
            if (current_state == ModuleState::RUNNING ||
                current_state == ModuleState::PAUSING ||
                current_state == ModuleState::PAUSED ||
                current_state == ModuleState::RESUMING ||
                current_state == ModuleState::INITIALIZING) {
                SystemLogger::info("MainApp", "Signaling module '" + mod_id + "' (state: " + ModuleStateToString(current_state) + ") to terminate.");
                g_vision_rt_manager->TerminateModule(mod_id, false); // false = don't join yet
            } else {
                 SystemLogger::info("MainApp", "Module '" + mod_id + "' already in a terminal/idle state: " + ModuleStateToString(current_state));
            }
        }
    }

    if (!modules_to_shutdown.empty()) {
        SystemLogger::info("MainApp", "Waiting a short period for modules to process stop signals (e.g., 1 second)...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    SystemLogger::info("MainApp", "Phase 2: Joining threads and unregistering modules.");
    for (const auto& mod_id : modules_to_shutdown) {
        if (g_vision_rt_manager) {
            HandleModuleCleanup(mod_id, "is being shut down by main application");
        }
    }

    if (g_vision_rt_manager && !g_vision_rt_manager->ListAllModules().empty()) {
        SystemLogger::warning("MainApp", "Some modules still found in registry after explicit shutdown loop. Attempting cleanup again.");
        std::vector<ModuleID> remaining_modules = g_vision_rt_manager->ListAllModules();
        for (const auto& mod_id : remaining_modules) {
            HandleModuleCleanup(mod_id, "is a remaining module during final cleanup");
        }
    }

    if (g_vision_rt_manager) {
        SystemLogger::info("MainApp", "Unregistering resources...");
        g_vision_rt_manager->UnregisterResource("global_config");
    }

    SystemLogger::info("MainApp", "Application shutdown sequence complete. Releasing VisionRT manager.");
    g_vision_rt_manager.reset();

    SystemLogger::info("MainApp", "Exiting.");
    return 0;
}