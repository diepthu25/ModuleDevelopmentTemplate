#include "modules/ModuleA/moduleA.h"
#include <ModuleTypes.h>

ModuleA::ModuleA(const std::string& moduleName, const std::string& moduleVersion)
    : VisionRT_Module(moduleName) {
    // Constructor implementation
    LogI("ModuleA constructed.");
}

ModuleA::~ModuleA() {
    // Destructor implementation
    LogI("ModuleA destructed.");
}

bool ModuleA::Setup(){
    LogI("Setup method called. Initializing...");
    // Put 1-time setup code here
    // For example, initialize resources, subscribe to events, etc.
    LogI("Setup complete.");
    return true;
}
bool ModuleA::Loop(){
    // Put main loop code here
    // For example, process data, handle events, etc.    
    // Code in this method should be non-blocking and return true to continue running.
    
    // Here we will request the SimpleResource to calculate a+b then publish c to workspace
    auto resource = RequestResource<SimpleResource>("input");
    if (resource) {
        LogI("Resource acquired. a: " + std::to_string(resource->a) + ", b: " + std::to_string(resource->b));
        int c = resource->a + resource->b;
        LogI("Calculated c: " + std::to_string(c));

        // Publish the result to workspace
        auto result = std::make_shared<int>(c);
        PublishToWorkspace<int>("Result", result);
        LogI("Published result to workspace.");
        
        //this supposed to calculate 1 time so we stop it here. 
        return false; 
    } else {
        LogE("Failed to acquire SimpleResource.");
    }

    // Should check for stop signal (typically handled by VisionRT but good to have)
    if (_ShouldStop()) {
        LogI("Stop signal received from VisionRT. Exiting loop.");
        return false; // Module should stop
    }
    return true; // Continue running
}
void ModuleA::Exit(){
    LogI("Exit method called. Cleaning up...");
    // Put cleanup code here
    // For example, release resources, unsubscribe from events, etc.
    LogI("Exit complete.");
}