#include "VisionRT/vision_rt_module.h"
#include "VisionRT/vision_rt.h" 
#include "utils/Logger.h"

VisionRT_Module::VisionRT_Module(ModuleID id)
    : _id(std::move(id)), 
      _vision_rt_manager(nullptr), 
      _should_run_flag(false),
      _internal_module_state(ModuleState::IDLE) {}

const ModuleID& VisionRT_Module::GetID() const {
    return _id;
}

ModuleState VisionRT_Module::GetCurrentState() const {
    if (_vision_rt_manager) {
        return _vision_rt_manager->GetModuleState(_id);
    }
    return _internal_module_state.load(std::memory_order_relaxed);
}

void VisionRT_Module::_SetVisionRTManager(VisionRT* manager) {
    _vision_rt_manager = manager;
}
void VisionRT_Module::_SignalStart() {
    _should_run_flag.store(true, std::memory_order_release);
}
void VisionRT_Module::_SignalStop() {
    _should_run_flag.store(false, std::memory_order_release);
}

bool VisionRT_Module::_ShouldStop() const {
    return !_should_run_flag.load(std::memory_order_acquire);
}

void VisionRT_Module::_SetCurrentStateInternal(ModuleState state){
    _internal_module_state.store(state, std::memory_order_relaxed);
}

ModuleState VisionRT_Module::_GetCurrentStateInternal() const {
    return _internal_module_state.load(std::memory_order_relaxed);
}

void VisionRT_Module::LogD(const std::string& message) {
    SystemLogger::debug(GetID(), message);
}

void VisionRT_Module::LogI(const std::string& message) {
    SystemLogger::info(GetID(), message);
}

void VisionRT_Module::LogW(const std::string& message) {
    SystemLogger::warning(GetID(), message);
}

void VisionRT_Module::LogE(const std::string& message) {
    SystemLogger::error(GetID(), message);
}

// --- Resource Conveniences ---


bool VisionRT_Module::UnsubscribeFromResource(const ResourceName& name) {
    if (_vision_rt_manager) {
        return _vision_rt_manager->UnsubscribeFromResourceRegistration(name, _id);
    }
    LogE("VisionRT manager not set, cannot unsubscribe from resource: " + name);
    return false;
}


// --- Workspace Conveniences ---

WorkspaceDataItem VisionRT_Module::GetRawFromWorkspace(const WorkspaceKey& key) {
    if (_vision_rt_manager) {
        return _vision_rt_manager->GetFromWorkspace(key);
    }
    LogE("VisionRT manager not set, cannot get raw from workspace key: " + key);
    return {}; 
}

bool VisionRT_Module::UnsubscribeFromWorkspaceData(const WorkspaceKey& key) {
    if (_vision_rt_manager) {
        return _vision_rt_manager->UnsubscribeFromWorkspaceKey(key, _id);
    }
    LogE("VisionRT manager not set, cannot unsubscribe from workspace key: " + key);
    return false;
}

