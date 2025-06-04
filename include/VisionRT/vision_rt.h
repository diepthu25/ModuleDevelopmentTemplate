#ifndef VISION_RT_H
#define VISION_RT_H

#include "VisionRT/vision_rt_common.h"
#include <map>
#include <vector>
#include <thread>
#include <mutex> 
#include <condition_variable>
#include <any>
#include <functional>
#include <memory>
#include "utils/Logger.h"

class VisionRT_Module;

class VisionRT {
public:
    VisionRT();
    ~VisionRT();

    std::shared_ptr<VisionRT_Module> GetModule(const ModuleID& id) const; 
    bool RegisterModule(std::shared_ptr<VisionRT_Module> module);
    bool UnregisterModule(const ModuleID& id);
    bool InvokeModule(const ModuleID& id);
    bool TerminateModule(const ModuleID& id, bool join_thread = false);
    bool PauseModule(const ModuleID& id);
    bool ResumeModule(const ModuleID& id);
    ModuleState GetModuleState(const ModuleID& id) const;
    std::vector<ModuleID> ListAllModules() const;
    std::vector<std::pair<ModuleID, ModuleState>> ListModulesWithStates() const;

    bool RegisterResource(const ResourceName& name, std::any resource_handle);
    bool UnregisterResource(const ResourceName& name);

    template<typename T>
    std::shared_ptr<T> RequestResource(const ResourceName& name); // Non-blocking request

    // Blocking wait for a resource to be registered.
    std::any WaitForResource(const ResourceName& name, 
                             std::chrono::milliseconds timeout = std::chrono::milliseconds(0)); // 0 for indefinite

    // Subscribe to resource registration events. Callback receives a copy of std::any.
    bool SubscribeToResourceRegistration(const ResourceName& name, const ModuleID& subscriber_id, 
                                         ResourceEventCallbackRaw callback);
    bool UnsubscribeFromResourceRegistration(const ResourceName& name, const ModuleID& subscriber_id);


    bool PublishToWorkspace(const ModuleID& publisher_module_id, const WorkspaceKey& key,
                            std::shared_ptr<void> data, const std::type_info& type);
    // Non-blocking get from workspace.
    WorkspaceDataItem GetFromWorkspace(const WorkspaceKey& key);
    bool WorkspaceKeyExists(const WorkspaceKey& key) const;
    void ClearWorkspaceKey(const WorkspaceKey& key);
    void ClearAllWorkspace();

    // Blocking wait for a workspace key to have data.
    WorkspaceDataItem WaitForWorkspaceKey(const WorkspaceKey& key, 
                                          std::chrono::milliseconds timeout = std::chrono::milliseconds(0)); // 0 for indefinite

    // Subscribe to workspace data publication/update events. Callback receives a copy of WorkspaceDataItem.
    bool SubscribeToWorkspaceKey(const WorkspaceKey& key, const ModuleID& subscriber_id, 
                                 WorkspaceEventCallbackRaw callback);
    bool UnsubscribeFromWorkspaceKey(const WorkspaceKey& key, const ModuleID& subscriber_id);


    VisionRT(const VisionRT&) = delete;
    VisionRT& operator=(const VisionRT&) = delete;
    VisionRT(VisionRT&&) = delete;
    VisionRT& operator=(VisionRT&&) = delete;

private:
    struct ModuleRuntimeInfo {
        std::shared_ptr<VisionRT_Module> module_ptr;
        std::thread module_thread;
        std::atomic<ModuleState> state;

        ModuleRuntimeInfo(std::shared_ptr<VisionRT_Module> ptr)
            : module_ptr(std::move(ptr)), state(ModuleState::IDLE) {}

        ModuleRuntimeInfo(ModuleRuntimeInfo&& other) noexcept
            : module_ptr(std::move(other.module_ptr)),
              module_thread(std::move(other.module_thread)),
              state(other.state.load(std::memory_order_relaxed)) {}

        ModuleRuntimeInfo& operator=(ModuleRuntimeInfo&& other) noexcept {
            if (this != &other) {
                module_ptr = std::move(other.module_ptr);
                module_thread = std::move(other.module_thread);
                state.store(other.state.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }
        ModuleRuntimeInfo(const ModuleRuntimeInfo&) = delete;
        ModuleRuntimeInfo& operator=(const ModuleRuntimeInfo&) = delete;
    };

    struct WaitContext {
        std::mutex mtx;
        std::condition_variable cv;
        // No need for explicit data_ready_flag if predicate always re-checks main store
    };

    void ModuleThreadLifecycle(ModuleID module_id_param);
    void _SetStateForIterator(std::map<ModuleID, ModuleRuntimeInfo>::iterator& it, ModuleState new_state);
    void SetModuleStateAtomically(const ModuleID& id, ModuleState new_state);

    void NotifyWorkspaceUpdateSubscribers(const WorkspaceKey& key, const WorkspaceDataItem& item_snapshot);
    void NotifyResourceRegisteredSubscribers(const ResourceName& name, const std::any& resource_snapshot);

    std::map<ModuleID, ModuleRuntimeInfo> _module_registry;
    mutable std::recursive_mutex _module_registry_mutex;

    std::map<ResourceName, std::any> _resource_registry;
    mutable std::recursive_mutex _resource_registry_mutex;

    std::map<WorkspaceKey, WorkspaceDataItem> _workspace;
    mutable std::recursive_mutex _workspace_mutex;

    // Event and Waiter Management
    mutable std::recursive_mutex _event_system_mutex; // Protects the maps below

    std::map<WorkspaceKey, std::shared_ptr<WaitContext>> _workspace_key_wait_contexts;
    std::map<WorkspaceKey, std::map<ModuleID, WorkspaceEventCallbackRaw>> _workspace_event_callbacks;

    std::map<ResourceName, std::shared_ptr<WaitContext>> _resource_wait_contexts;
    std::map<ResourceName, std::map<ModuleID, ResourceEventCallbackRaw>> _resource_event_callbacks;

};

template<typename T>
std::shared_ptr<T> VisionRT::RequestResource(const ResourceName& name) {
    std::lock_guard<std::recursive_mutex> lock(_resource_registry_mutex);
    auto it = _resource_registry.find(name);
    if (it != _resource_registry.end()) {
        try {
            return std::any_cast<std::shared_ptr<T>>(it->second);
        } catch (const std::bad_any_cast& e) {
            SystemLogger::error("VisionRT", "Type mismatch for resource '" + name +
                                "'. Requested std::shared_ptr<" + typeid(T).name() +
                                ">. Stored type: " + it->second.type().name() + ". Details: " + e.what());
            return nullptr;
        }
    }
    SystemLogger::warning("VisionRT", "Resource not found: " + name);
    return nullptr;
}

#endif // VISION_RT_H