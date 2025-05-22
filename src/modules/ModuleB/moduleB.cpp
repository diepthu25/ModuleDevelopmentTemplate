#include "modules/ModuleB/moduleB.h"
#include <iostream>
#include <string>

ModuleB::ModuleB(const std::string& moduleName, const std::string& moduleVersion)
    : VisionRT_Module(moduleName) {
    // Constructor implementation
    LogI("ModuleB constructed.");
}
ModuleB::~ModuleB() {
    // Destructor implementation
    LogI("ModuleB destructed.");
}

bool ModuleB::Setup(){
    LogI("Setup method called. Initializing...");
    // Put 1-time setup code here
    // For example, initialize resources, subscribe to events, etc.
    LogI("Setup complete.");
    return true;
}

bool ModuleB::Loop(){
    // Put main loop code here
    // For example, process data, handle events, etc.    
    // Code in this method should be non-blocking and return true to continue running.
    
    // Here we will request the SimpleResource to calculate a+b then publish c to workspace
    ModuleID publisher_id("ModuleA");
    std::chrono::system_clock::time_point timestamp;
    auto result = GetFromWorkspace<int>("Result", publisher_id, timestamp);
    if (result) {
        LogI("Result from workspace: " + std::to_string(*result));
        return false;
    } else {
        LogE("Failed to get result from workspace. Waiting for data...");
    }
    if(_ShouldStop()){
        LogI("Stop signal received from VisionRT. Exiting loop.");
        return false; // Module should stop
    }
    return true; // Continue running

}

void ModuleB::Exit(){
    LogI("Exit method called. Cleaning up...");
    // Put cleanup code here
    // For example, release resources, unsubscribe from events, etc.
    LogI("Exit complete.");
}