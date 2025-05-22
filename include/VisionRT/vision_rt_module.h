#ifndef VISION_RT_MODULE_H
#define VISION_RT_MODULE_H

#include "vision_rt_common.h"
#include "vision_rt.h"
#include <atomic>
#include <string>
#include <memory> // Required for std::enable_shared_from_this
#include <chrono> // For timeouts
#include <functional> // For std::function

// Forward declaration of VisionRT to break circular dependency for template methods
// class VisionRT; 

class VisionRT_Module : public std::enable_shared_from_this<VisionRT_Module> {
public:
    VisionRT_Module(ModuleID id);
    virtual ~VisionRT_Module() = default;

    // --- Lifecycle methods to be implemented by derived modules ---
    virtual bool Setup() = 0; 
    virtual bool Loop() = 0;  
    virtual void Exit() = 0;  

    // --- Getters ---
    const ModuleID& GetID() const;
    ModuleState GetCurrentState() const; 

    // --- Internal methods used by VisionRT to manage the module ---
    void _SetVisionRTManager(VisionRT* manager);
    void _SignalStart();  
    void _SignalStop();         
    bool _ShouldStop() const;   
    void _SetCurrentStateInternal(ModuleState state); 
    ModuleState _GetCurrentStateInternal() const; 

protected:
    ModuleID _id;
    VisionRT* _vision_rt_manager; 
    std::atomic<bool> _should_run_flag;     
    std::atomic<ModuleState> _internal_module_state; 

    // --- Convenience API for modules to interact with VisionRT ---
    void LogD(const std::string& message);
    void LogI(const std::string& message);
    void LogW(const std::string& message);
    void LogE(const std::string& message);

    // Request a resource (non-blocking).
    template<typename T>
    std::shared_ptr<T> RequestResource(const ResourceName& name);

    // Wait for a resource to be available (blocking).
    template<typename T>
    std::shared_ptr<T> WaitForResource(const ResourceName& name, 
                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    // Subscribe to resource registration. The callback provides a typed shared_ptr.
    template<typename T>
    bool SubscribeToResource(const ResourceName& name, 
                             std::function<void(const ResourceName&, std::shared_ptr<T>)> typed_callback);
    bool UnsubscribeFromResource(const ResourceName& name);


    // Publish data to the workspace.
    template<typename T>
    bool PublishToWorkspace(const WorkspaceKey& key, std::shared_ptr<T> data_to_publish);
    
    // Get typed data from workspace (non-blocking). Also returns publisher_id and timestamp.
    template<typename T>
    std::shared_ptr<T> GetFromWorkspace(const WorkspaceKey& key, 
                                        ModuleID& out_publisher_id, 
                                        std::chrono::system_clock::time_point& out_timestamp);
    
    // Get raw WorkspaceDataItem (non-blocking).
    WorkspaceDataItem GetRawFromWorkspace(const WorkspaceKey& key);

    // Wait for workspace data to be available (blocking).
    template<typename T>
    std::shared_ptr<T> WaitForWorkspaceData(const WorkspaceKey& key, 
                                            ModuleID& out_publisher_id, 
                                            std::chrono::system_clock::time_point& out_timestamp,
                                            std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    // Subscribe to workspace data updates. The callback provides typed data.
    template<typename T>
    bool SubscribeToWorkspaceData(const WorkspaceKey& key,
                                  std::function<void(const WorkspaceKey&, std::shared_ptr<T>, const ModuleID&, const std::chrono::system_clock::time_point&)> typed_callback);
    bool UnsubscribeFromWorkspaceData(const WorkspaceKey& key);


public:
    VisionRT_Module(const VisionRT_Module&) = delete;
    VisionRT_Module& operator=(const VisionRT_Module&) = delete;
    VisionRT_Module(VisionRT_Module&&) = delete;
    VisionRT_Module& operator=(VisionRT_Module&&) = delete;
};


template<typename T>
bool VisionRT_Module::PublishToWorkspace(const WorkspaceKey& key, std::shared_ptr<T> data_to_publish) {
    if (_vision_rt_manager) {
        std::shared_ptr<void> void_ptr = std::static_pointer_cast<void>(data_to_publish);
        return _vision_rt_manager->PublishToWorkspace(this->_id, key, void_ptr, typeid(T));
    }
    LogE("VisionRT manager not set, cannot publish to workspace key: " + key);
    return false;
}


template<typename T>
std::shared_ptr<T> VisionRT_Module::GetFromWorkspace(
    const WorkspaceKey& key, 
    ModuleID& out_publisher_id, 
    std::chrono::system_clock::time_point& out_timestamp) {
    if (_vision_rt_manager) {
        WorkspaceDataItem item = _vision_rt_manager->GetFromWorkspace(key); 
        if (item.data_ptr && item.type_info == std::type_index(typeid(T))) {
            out_publisher_id = item.publisher_id;
            out_timestamp = item.last_updated_timestamp;
            return std::static_pointer_cast<T>(item.data_ptr);
        }
        if (item.data_ptr && item.type_info != std::type_index(typeid(T))) { // Only log if data exists but type is wrong
            LogW("Type mismatch for workspace key '" + key + "'. Expected: " + typeid(T).name() + 
                 ", Got: " + item.type_info.name());
        }
    } else {
        LogE("VisionRT manager not set, cannot get from workspace key: " + key);
    }
    out_publisher_id.clear(); // Ensure output params are cleared on failure/no data
    out_timestamp = {};
    return nullptr;
}


template<typename T>
std::shared_ptr<T> VisionRT_Module::WaitForWorkspaceData(
    const WorkspaceKey& key, 
    ModuleID& out_publisher_id, 
    std::chrono::system_clock::time_point& out_timestamp,
    std::chrono::milliseconds timeout) {

    if (!_vision_rt_manager) {
        LogE("VisionRT manager not set, cannot wait for workspace data: " + key);
        out_publisher_id.clear(); out_timestamp = {};
        return nullptr;
    }

    WorkspaceDataItem item = _vision_rt_manager->WaitForWorkspaceKey(key, timeout);
    if (item.data_ptr) { // Check if data_ptr is valid (item is not default-constructed)
        if (item.type_info == std::type_index(typeid(T))) {
            out_publisher_id = item.publisher_id;
            out_timestamp = item.last_updated_timestamp;
            return std::static_pointer_cast<T>(item.data_ptr);
        } else {
            LogW("Type mismatch for waited workspace key '" + key + "'. Expected: " + typeid(T).name() + 
                 ", Got: " + item.type_info.name());
        }
    }
    out_publisher_id.clear();
    out_timestamp = {};
    return nullptr; // Timeout or type mismatch
}

template<typename T>
bool VisionRT_Module::SubscribeToWorkspaceData(
    const WorkspaceKey& key,
    std::function<void(const WorkspaceKey&, std::shared_ptr<T>, const ModuleID&, const std::chrono::system_clock::time_point&)> typed_callback) {
    
    if (!_vision_rt_manager) {
        LogE("VisionRT manager not set, cannot subscribe to workspace key: " + key);
        return false;
    }
    if (!typed_callback) {
        LogE("SubscribeToWorkspaceData: Null typed_callback provided for key '" + key + "'.");
        return false;
    }
    
    WorkspaceEventCallbackRaw raw_cb = 
        [this, key_cap = key, typed_callback_captured = std::move(typed_callback)] 
        (const WorkspaceKey& key_from_rt, const WorkspaceDataItem& item) {
        // 'this' capture is for LogW/LogE. Use key_from_rt for consistency.
        if (item.data_ptr) {
            if (item.type_info == std::type_index(typeid(T))) {
                try {
                    std::shared_ptr<T> typed_data = std::static_pointer_cast<T>(item.data_ptr);
                    typed_callback_captured(key_from_rt, typed_data, item.publisher_id, item.last_updated_timestamp);
                } catch (const std::bad_cast& e) { // Should not happen if type_info matches, but defensive
                    this->LogE("std::static_pointer_cast failed for workspace key '" + key_from_rt + "' despite type_info match. Details: " + e.what());
                }
            } else {
                this->LogW("Type mismatch in workspace subscription for key '" + key_from_rt + "'. Expected: " + typeid(T).name() + 
                           ", Got: " + item.type_info.name());
            }
        } else {
            // This case could happen if an empty WorkspaceDataItem is somehow passed, though VisionRT should pass populated one.
            this->LogW("Received notification for workspace key '" + key_from_rt + "' but data_ptr is null.");
        }
    };
    return _vision_rt_manager->SubscribeToWorkspaceKey(key, _id, std::move(raw_cb));
}

template<typename T>
std::shared_ptr<T> VisionRT_Module::RequestResource(const ResourceName& name) {
    if (_vision_rt_manager) {
        return _vision_rt_manager->RequestResource<T>(name);
    }
    LogE("VisionRT manager not set, cannot request resource: " + name);
    return nullptr;
}

template<typename T>
std::shared_ptr<T> VisionRT_Module::WaitForResource(const ResourceName& name, std::chrono::milliseconds timeout) {
    if (!_vision_rt_manager) {
        LogE("VisionRT manager not set, cannot wait for resource: " + name);
        return nullptr;
    }
    std::any resource_any = _vision_rt_manager->WaitForResource(name, timeout);
    if (resource_any.has_value()) {
        try {
            return std::any_cast<std::shared_ptr<T>>(resource_any);
        } catch (const std::bad_any_cast& e) {
            LogE("Type mismatch for waited resource '" + name + "'. Expected std::shared_ptr<" + typeid(T).name() +
                 ">, Got: " + resource_any.type().name() + ". Details: " + e.what());
            return nullptr;
        }
    }
    return nullptr; // Timeout or resource not found
}

template<typename T>
bool VisionRT_Module::SubscribeToResource(const ResourceName& name, 
                                          std::function<void(const ResourceName&, std::shared_ptr<T>)> typed_callback) {
    if (!_vision_rt_manager) {
        LogE("VisionRT manager not set, cannot subscribe to resource: " + name);
        return false;
    }
    if (!typed_callback) {
        LogE("SubscribeToResource: Null typed_callback provided for resource '" + name + "'.");
        return false;
    }

    // Create a raw callback that wraps the typed_callback
    ResourceEventCallbackRaw raw_cb = 
        [this, name, typed_callback_captured = std::move(typed_callback)] 
        (const ResourceName& res_name_from_rt, const std::any& resource_any) {
        // 'this' capture is for LogE, use res_name_from_rt for consistency
        if (resource_any.has_value()) {
            try {
                std::shared_ptr<T> typed_res = std::any_cast<std::shared_ptr<T>>(resource_any);
                typed_callback_captured(res_name_from_rt, typed_res);
            } catch (const std::bad_any_cast& e) {
                // Log from module context
                this->LogE("Type mismatch in resource subscription for '" + res_name_from_rt + "'. Expected std::shared_ptr<" + typeid(T).name() +
                     ">, Got: " + resource_any.type().name() + ". Details: " + e.what());
            }
        } else {
            // This case (empty any) shouldn't happen if VisionRT always passes a valid 'any' on registration.
            this->LogW("Received notification for resource '" + res_name_from_rt + "' but std::any has no value.");
        }
    };
    return _vision_rt_manager->SubscribeToResourceRegistration(name, _id, std::move(raw_cb));
}

#endif // VISION_RT_MODULE_H