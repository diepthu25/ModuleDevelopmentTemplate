#include "VisionRT/vision_rt.h"
#include "VisionRT/vision_rt_module.h"
#include "utils/Logger.h"
#include <sstream>
#include <vector> // Required for collecting callbacks

VisionRT::VisionRT() {
    SystemLogger::info("VisionRT", "VisionRT Manager instance created.");
}

VisionRT::~VisionRT() {
    SystemLogger::info("VisionRT", "VisionRT Manager instance shutting down...");
    std::vector<ModuleID> module_ids_to_terminate;
    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        for (const auto& pair : _module_registry) {
            module_ids_to_terminate.push_back(pair.first);
        }
    }

    for (const auto& id : module_ids_to_terminate) {
        SystemLogger::info("VisionRT", "Terminating module '" + id + "' during VisionRT shutdown.");
        TerminateModule(id, true); // Joins thread
    }
    
    // Ensure all threads are joined, even if TerminateModule was somehow bypassed or failed to join
    // This block might be redundant if TerminateModule always joins correctly.
    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        for (auto& pair : _module_registry) {
            if (pair.second.module_thread.joinable()) {
                SystemLogger::warning("VisionRT", "Module thread '" + pair.first +
                                    "' was still joinable during final shutdown. Forcing join.");
                pair.second.module_thread.join();
            }
        }
        _module_registry.clear();
    }


    // Clear event system resources
    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        // Notify any remaining waiters that system is shutting down (they will likely timeout or see cleared data)
        for (auto& pair : _workspace_key_wait_contexts) {
            if (pair.second) pair.second->cv.notify_all();
        }
        _workspace_key_wait_contexts.clear();
        _workspace_event_callbacks.clear();

        for (auto& pair : _resource_wait_contexts) {
             if (pair.second) pair.second->cv.notify_all();
        }
        _resource_wait_contexts.clear();
        _resource_event_callbacks.clear();
    }
    
    // Clear workspace and resources
    ClearAllWorkspace(); // Already logs
    {
        std::lock_guard<std::recursive_mutex> lock(_resource_registry_mutex);
        _resource_registry.clear();
        SystemLogger::info("VisionRT", "All resources cleared.");
    }

    SystemLogger::info("VisionRT", "VisionRT Manager shutdown complete.");
}

bool VisionRT::RegisterModule(std::shared_ptr<VisionRT_Module> module) {
    if (!module) {
        SystemLogger::error("VisionRT", "Attempted to register a null module pointer.");
        return false;
    }
    const ModuleID& id = module->GetID();
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);

    if (_module_registry.count(id)) {
        SystemLogger::error("VisionRT", "Module with ID '" + id + "' already registered.");
        return false;
    }

    module->_SetVisionRTManager(this);
    auto [it, success] = _module_registry.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(id),
        std::forward_as_tuple(module)
    );

    if (success) {
        _SetStateForIterator(it, ModuleState::IDLE);
        SystemLogger::info("VisionRT", "Module '" + id + "' registered successfully.");
        return true;
    } else {
        SystemLogger::error("VisionRT", "Failed to emplace module '" + id + "' into registry.");
        return false;
    }
}

bool VisionRT::UnregisterModule(const ModuleID& id) {
    std::unique_lock<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it == _module_registry.end()) {
        SystemLogger::warning("VisionRT", "Attempted to unregister non-existent module: " + id);
        return false;
    }

    ModuleState current_state = it->second.state.load(std::memory_order_relaxed);
    if (current_state != ModuleState::STOPPED &&
        current_state != ModuleState::IDLE &&
        current_state != ModuleState::ERROR_SETUP) {
        lock.unlock();
        SystemLogger::info("VisionRT", "Attempting to terminate module '" + id + "' before unregistering.");
        TerminateModule(id, true); // true to join
        lock.lock(); // Re-acquire lock

        // Re-check if module still exists, might have been concurrently removed or state changed
        it = _module_registry.find(id);
        if (it == _module_registry.end()) {
            SystemLogger::info("VisionRT", "Module '" + id + "' was removed during termination attempt (possibly by another thread).");
            // Also clean up subscriptions for this module ID if it was removed this way
        } else {
            current_state = it->second.state.load(std::memory_order_relaxed);
             if (current_state != ModuleState::STOPPED && current_state != ModuleState::ERROR_SETUP) {
                SystemLogger::error("VisionRT", "Failed to stop module '" + id + "' during unregistration attempt. State: " + std::to_string(static_cast<int>(current_state)));
                // Proceed with unregistration cautiously
            }
        }
    }
    
    if (it != _module_registry.end() && it->second.module_thread.joinable()) {
        SystemLogger::info("VisionRT", "Joining thread for module '" + id + "' during unregistration.");
        it->second.module_thread.join();
    }

    if (it != _module_registry.end()) {
        _module_registry.erase(it);
        SystemLogger::info("VisionRT", "Module '" + id + "' unregistered from module registry.");
    }


    // Clean up event subscriptions for this module
    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        for (auto& ws_pair : _workspace_event_callbacks) {
            ws_pair.second.erase(id);
        }
        for (auto& res_pair : _resource_event_callbacks) {
            res_pair.second.erase(id);
        }
        SystemLogger::info("VisionRT", "Cleaned up event subscriptions for module '" + id + "'.");
    }

    return true;
}

void VisionRT::_SetStateForIterator(std::map<ModuleID, ModuleRuntimeInfo>::iterator& it, ModuleState new_state) {
    // Assumes _module_registry_mutex is ALREADY HELD
    it->second.state.store(new_state, std::memory_order_relaxed);
    if(it->second.module_ptr) {
        it->second.module_ptr->_SetCurrentStateInternal(new_state);
    }
}

void VisionRT::SetModuleStateAtomically(const ModuleID& id, ModuleState new_state) {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it != _module_registry.end()) {
        _SetStateForIterator(it, new_state);
    }
}


void VisionRT::ModuleThreadLifecycle(ModuleID module_id_param) {
    std::shared_ptr<VisionRT_Module> module_local_ptr;
    std::map<ModuleID, ModuleRuntimeInfo>::iterator registry_iterator; 

    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        registry_iterator = _module_registry.find(module_id_param);
        if (registry_iterator == _module_registry.end() || !registry_iterator->second.module_ptr) {
            SystemLogger::error("VisionRT", "ModuleThreadLifecycle: Module '" + module_id_param +
                                "' not found or null in registry at thread start.");
            return;
        }
        module_local_ptr = registry_iterator->second.module_ptr;
        _SetStateForIterator(registry_iterator, ModuleState::INITIALIZING);
    }

    SystemLogger::info("VisionRT", "Module '" + module_id_param + "' thread started. Calling Setup().");
    bool setup_ok = false;
    try {
        setup_ok = module_local_ptr->Setup();
    } catch (const std::exception& e) {
        SystemLogger::error("VisionRT", "Exception in module '" + module_id_param + "' Setup(): " + e.what());
    } catch (...) {
        SystemLogger::error("VisionRT", "Unknown exception in module '" + module_id_param + "' Setup().");
    }

    if (!setup_ok) {
        SystemLogger::error("VisionRT", "Module '" + module_id_param + "' Setup() failed.");
        try { module_local_ptr->Exit(); } catch (...) { /* Ignore */ }
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        registry_iterator = _module_registry.find(module_id_param); 
        if (registry_iterator != _module_registry.end()) {
            _SetStateForIterator(registry_iterator, ModuleState::ERROR_SETUP);
        }
        SystemLogger::info("VisionRT", "Module '" + module_id_param + "' thread finishing after Setup failure.");
        return;
    }

    SystemLogger::info("VisionRT", "Module '" + module_id_param + "' Setup() successful. Starting Loop().");
    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        registry_iterator = _module_registry.find(module_id_param);
        if (registry_iterator != _module_registry.end()) {
            _SetStateForIterator(registry_iterator, ModuleState::RUNNING);
        } else { 
            SystemLogger::error("VisionRT", "Module '" + module_id_param + "' disappeared after Setup(). Terminating thread.");
            return;
        }
    }

    bool loop_continue = true;
    ModuleState effective_state_for_loop = ModuleState::RUNNING;

    while (loop_continue && !module_local_ptr->_ShouldStop()) {
        ModuleState current_visionrt_state;
        {
            std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
            registry_iterator = _module_registry.find(module_id_param);
            if (registry_iterator == _module_registry.end()) {
                 SystemLogger::error("VisionRT", "Module '" + module_id_param + "' disappeared from registry during loop. Terminating thread.");
                 loop_continue = false; break;
            }
            current_visionrt_state = registry_iterator->second.state.load(std::memory_order_relaxed);
        }

        if (current_visionrt_state == ModuleState::PAUSING) {
            if (effective_state_for_loop != ModuleState::PAUSED) {
                 SystemLogger::info("VisionRT", "Module '" + module_id_param + "' transitioning to PAUSED state as requested by VisionRT.");
                 effective_state_for_loop = ModuleState::PAUSED;
                 // VisionRT already set its state to PAUSING. Module doesn't confirm PAUSED back to VisionRT explicitly yet.
                 // For now, assume module honors this.
            }
        } else if ((current_visionrt_state == ModuleState::RUNNING || current_visionrt_state == ModuleState::RESUMING) && effective_state_for_loop == ModuleState::PAUSED) {
             SystemLogger::info("VisionRT", "Module '" + module_id_param + "' resuming from PAUSED state.");
             effective_state_for_loop = ModuleState::RUNNING;
             if(current_visionrt_state == ModuleState::RESUMING) { // If VisionRT was in RESUMING, move it to RUNNING
                std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
                registry_iterator = _module_registry.find(module_id_param);
                if(registry_iterator != _module_registry.end() && registry_iterator->second.state.load(std::memory_order_relaxed) == ModuleState::RESUMING){
                    _SetStateForIterator(registry_iterator, ModuleState::RUNNING);
                }
             }
        }


        if (effective_state_for_loop == ModuleState::PAUSED) {
            // If module needs to do low-activity work in pause, it should handle it in Loop()
            // For now, VisionRT's thread for this module just sleeps.
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
            continue; 
        }

        try {
            loop_continue = module_local_ptr->Loop();
        } catch (const std::exception& e) {
            SystemLogger::error("VisionRT", "Exception in module '" + module_id_param + "' Loop(): " + e.what());
            loop_continue = false; // Stop loop on exception
            std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
            registry_iterator = _module_registry.find(module_id_param);
            if (registry_iterator != _module_registry.end()) {
                _SetStateForIterator(registry_iterator, ModuleState::ERROR_RUNTIME);
            }
        } catch (...) {
            SystemLogger::error("VisionRT", "Unknown exception in module '" + module_id_param + "' Loop().");
            loop_continue = false; // Stop loop
            std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
            registry_iterator = _module_registry.find(module_id_param);
            if (registry_iterator != _module_registry.end()) {
                _SetStateForIterator(registry_iterator, ModuleState::ERROR_RUNTIME);
            }
        }
    }

    SystemLogger::info("VisionRT", "Module '" + module_id_param + "' Loop() finished or stop signaled.");
    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        registry_iterator = _module_registry.find(module_id_param);
        if (registry_iterator != _module_registry.end()) {
            // Only transition to STOPPING if not already in an error state
            if (registry_iterator->second.state.load(std::memory_order_relaxed) != ModuleState::ERROR_RUNTIME &&
                registry_iterator->second.state.load(std::memory_order_relaxed) != ModuleState::ERROR_SETUP) {
                 _SetStateForIterator(registry_iterator, ModuleState::STOPPING);
            }
        } else {
             SystemLogger::warning("VisionRT", "Module '" + module_id_param + "' not in registry at start of Exit phase.");
        }
    }

    try {
        module_local_ptr->Exit();
    } catch (const std::exception& e) {
        SystemLogger::error("VisionRT", "Exception in module '" + module_id_param + "' Exit(): " + e.what());
        // State remains ERROR_RUNTIME or STOPPING. VisionRT will later set to STOPPED if not error.
    } catch (...) {
        SystemLogger::error("VisionRT", "Unknown exception in module '" + module_id_param + "' Exit().");
    }

    SystemLogger::info("VisionRT", "Module '" + module_id_param + "' Exit() called. Thread finishing.");
    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        registry_iterator = _module_registry.find(module_id_param);
         if (registry_iterator != _module_registry.end()) {
            // If it was stopping normally, set to STOPPED. If error, keep error state.
            ModuleState current_state_final = registry_iterator->second.state.load(std::memory_order_relaxed);
            if (current_state_final != ModuleState::ERROR_RUNTIME && current_state_final != ModuleState::ERROR_SETUP) {
                _SetStateForIterator(registry_iterator, ModuleState::STOPPED);
            }
        } else {
             SystemLogger::warning("VisionRT", "Module '" + module_id_param + "' not in registry at end of Exit phase.");
        }
    }
}

bool VisionRT::InvokeModule(const ModuleID& id) {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it == _module_registry.end()) {
        SystemLogger::error("VisionRT", "Cannot invoke non-existent module: " + id);
        return false;
    }

    ModuleState current_state = it->second.state.load(std::memory_order_relaxed);
    if (current_state != ModuleState::IDLE &&
        current_state != ModuleState::STOPPED &&
        current_state != ModuleState::ERROR_SETUP) { // Allow re-invoke from ERROR_SETUP
        SystemLogger::warning("VisionRT", "Module '" + id + "' not in a startable state. Current state: " + std::to_string(static_cast<int>(current_state)));
        return false;
    }

    if (it->second.module_thread.joinable()) {
        SystemLogger::info("VisionRT", "Joining previous thread for module '" + id + "' before invoking again.");
        it->second.module_thread.join(); // Ensure previous thread is cleaned up
    }

    it->second.module_ptr->_SignalStart(); // Set flag for module's _ShouldStop()
    it->second.module_thread = std::thread(&VisionRT::ModuleThreadLifecycle, this, id);
    // State will be set to INITIALIZING by ModuleThreadLifecycle
    SystemLogger::info("VisionRT", "Module '" + id + "' invoked. Thread launched.");
    return true;
}

bool VisionRT::TerminateModule(const ModuleID& id, bool join_thread) {
    std::shared_ptr<VisionRT_Module> module_to_signal_ptr;
    std::thread* thread_handle_to_join = nullptr; // Using raw pointer, careful with lifetime

    {
        std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
        auto it = _module_registry.find(id);
        if (it == _module_registry.end()) {
            SystemLogger::warning("VisionRT", "Cannot terminate non-existent module: " + id);
            return false;
        }

        module_to_signal_ptr = it->second.module_ptr;
        ModuleState current_state = it->second.state.load(std::memory_order_relaxed);

        if (current_state == ModuleState::STOPPED || current_state == ModuleState::IDLE || current_state == ModuleState::ERROR_SETUP) {
            SystemLogger::info("VisionRT", "Module '" + id + "' is already in a non-running or non-startable state ("+ std::to_string(static_cast<int>(current_state)) +").");
            if (it->second.module_thread.joinable()) {
                 thread_handle_to_join = &it->second.module_thread;
            }
             // No need to signal if already stopped/idle/error_setup
            module_to_signal_ptr = nullptr; 
        } else if (current_state == ModuleState::STOPPING || current_state == ModuleState::ERROR_RUNTIME) {
            SystemLogger::info("VisionRT", "Module '" + id + "' is already stopping or in runtime error. State: " + std::to_string(static_cast<int>(current_state)));
             if (it->second.module_thread.joinable()) {
                 thread_handle_to_join = &it->second.module_thread;
            }
            // Potentially re-signal if it was just STOPPING but not yet fully stopped.
            // But _SignalStop is idempotent, so it's fine.
        } else {
            SystemLogger::info("VisionRT", "Signaling module '" + id + "' (state: " + std::to_string(static_cast<int>(current_state)) + ") to stop.");
            // ModuleThreadLifecycle will set state to STOPPING then STOPPED/ERROR.
            // VisionRT does not force state to STOPPING here, lets lifecycle manage.
            if (it->second.module_thread.joinable()) {
                 thread_handle_to_join = &it->second.module_thread;
            }
        }
    } // _module_registry_mutex released

    if (module_to_signal_ptr) {
        module_to_signal_ptr->_SignalStop(); // Signal module to stop its loop
    }

    if (join_thread && thread_handle_to_join && thread_handle_to_join->joinable()) {
        SystemLogger::info("VisionRT", "Waiting for module '" + id + "' thread to join...");
        try {
            thread_handle_to_join->join();
            SystemLogger::info("VisionRT", "Module '" + id + "' thread joined.");
            // State should be STOPPED or ERROR_RUNTIME/ERROR_SETUP by now, set by ModuleThreadLifecycle
            // Optionally re-check and force state if needed, but lifecycle should handle it.
        } catch (const std::system_error& e) {
            SystemLogger::error("VisionRT", "System error while joining thread for module '" + id + "': " + e.what());
            // State might be indeterminate here if join fails catastrophically
        }
    }
    return true;
}


bool VisionRT::PauseModule(const ModuleID& id) {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it == _module_registry.end()) {
        SystemLogger::warning("VisionRT", "Cannot pause non-existent module: " + id);
        return false;
    }
    ModuleState current_state = it->second.state.load(std::memory_order_relaxed);
    // Can only pause a RUNNING module.
    if (current_state == ModuleState::RUNNING) {
        _SetStateForIterator(it, ModuleState::PAUSING);
        SystemLogger::info("VisionRT", "Module '" + id + "' signaled to pause. Transitioning to PAUSING.");
        // ModuleThreadLifecycle will see PAUSING and effectively pause its loop.
        // It does not transition to PAUSED itself. User can poll GetModuleState.
        // For a stricter PAUSED state, module would need to confirm it.
        return true;
    }
    SystemLogger::warning("VisionRT", "Module '" + id + "' not in RUNNING state, cannot pause. Current state: " + std::to_string(static_cast<int>(current_state)));
    return false;
}

bool VisionRT::ResumeModule(const ModuleID& id) {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it == _module_registry.end()) {
        SystemLogger::warning("VisionRT", "Cannot resume non-existent module: " + id);
        return false;
    }
    ModuleState current_state = it->second.state.load(std::memory_order_relaxed);
    // Can resume from PAUSED or PAUSING (if it's quick and resume is called before it fully processes PAUSING)
    if (current_state == ModuleState::PAUSED || current_state == ModuleState::PAUSING) {
        _SetStateForIterator(it, ModuleState::RESUMING);
        SystemLogger::info("VisionRT", "Module '" + id + "' signaled to resume. Transitioning to RESUMING.");
        // ModuleThreadLifecycle will see RESUMING and transition to RUNNING.
        return true;
    }
    SystemLogger::warning("VisionRT", "Module '" + id + "' not in a pausable state (PAUSED/PAUSING), cannot resume. Current state: " + std::to_string(static_cast<int>(current_state)));
    return false;
}

ModuleState VisionRT::GetModuleState(const ModuleID& id) const {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    auto it = _module_registry.find(id);
    if (it != _module_registry.end()) {
        return it->second.state.load(std::memory_order_relaxed);
    }
    // If module not found, what state? Could argue it's effectively IDLE or non-existent.
    // Let's return IDLE as a sentinel for "not actively managed" or "unknown".
    // Or throw an exception. For now, IDLE.
    SystemLogger::warning("VisionRT", "GetModuleState called for non-existent module ID: " + id + ". Returning IDLE.");
    return ModuleState::IDLE; 
}

std::vector<ModuleID> VisionRT::ListAllModules() const {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    std::vector<ModuleID> ids;
    ids.reserve(_module_registry.size());
    for(const auto& pair : _module_registry) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::vector<std::pair<ModuleID, ModuleState>> VisionRT::ListModulesWithStates() const {
    std::lock_guard<std::recursive_mutex> lock(_module_registry_mutex);
    std::vector<std::pair<ModuleID, ModuleState>> result;
    result.reserve(_module_registry.size());
    for(const auto& pair : _module_registry) {
        result.emplace_back(pair.first, pair.second.state.load(std::memory_order_relaxed));
    }
    return result;
}

// --- Resource Management ---

bool VisionRT::RegisterResource(const ResourceName& name, std::any resource_handle) {
    std::any resource_snapshot; // For notifications
    bool newly_registered = false;

    {
        std::lock_guard<std::recursive_mutex> lock(_resource_registry_mutex);
        if (_resource_registry.count(name)) {
            SystemLogger::error("VisionRT", "Resource with name '" + name + "' already registered. Overwriting not supported by default.");
            // To support overwrite, would need to unregister then register, or modify existing.
            // For now, fail if already exists.
            return false;
        }
        _resource_registry[name] = resource_handle; // Using operator[] which might default construct then assign
                                                    // or use emplace for efficiency if `resource_handle` is rvalue
        // _resource_registry.emplace(name, std::move(resource_handle)); // Better if resource_handle can be moved
        
        resource_snapshot = _resource_registry[name]; // Make a copy for notification (std::any is copyable)
        newly_registered = true;
        SystemLogger::info("VisionRT", "Resource '" + name + "' registered (type: " + resource_handle.type().name() + ").");
    } // _resource_registry_mutex released

    if (newly_registered) {
        NotifyResourceRegisteredSubscribers(name, resource_snapshot);
    }
    return true;
}

bool VisionRT::UnregisterResource(const ResourceName& name) {
    bool erased = false;
    {
        std::lock_guard<std::recursive_mutex> lock(_resource_registry_mutex);
        if (_resource_registry.erase(name) > 0) {
            SystemLogger::info("VisionRT", "Resource '" + name + "' unregistered.");
            erased = true;
        } else {
            SystemLogger::warning("VisionRT", "Attempted to unregister non-existent resource: " + name);
            return false; // Resource not found
        }
    } // _resource_registry_mutex released

    if (erased) {
        std::shared_ptr<WaitContext> wc_to_notify;
        {
            std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
            auto it_wc = _resource_wait_contexts.find(name);
            if (it_wc != _resource_wait_contexts.end()) {
                wc_to_notify = it_wc->second;
                // Optionally remove the wait context now that resource is gone
                // _resource_wait_contexts.erase(it_wc); // If waiters should not re-wait for this key
            }
            // Subscriptions remain, but callbacks would get nothing or see resource gone.
            // For simplicity, not clearing subscriptions on unregister, they just won't fire again
            // unless resource is re-registered.
        }
        if (wc_to_notify) {
            wc_to_notify->cv.notify_all(); // Wake up waiters, they will find resource gone
        }
    }
    return true;
}


std::any VisionRT::WaitForResource(const ResourceName& name, std::chrono::milliseconds timeout_duration) {
    std::shared_ptr<WaitContext> wc;
    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        auto it_wc = _resource_wait_contexts.find(name);
        if (it_wc == _resource_wait_contexts.end()) {
            wc = std::make_shared<WaitContext>();
            _resource_wait_contexts.emplace(name, wc);
        } else {
            wc = it_wc->second;
        }
    }

    std::unique_lock<std::mutex> lock(wc->mtx);
    // Initial check (already holding wc->mtx)
    {
        std::lock_guard<std::recursive_mutex> res_lock(_resource_registry_mutex);
        auto it = _resource_registry.find(name);
        if (it != _resource_registry.end()) {
            return it->second; // Return a copy
        }
    }

    bool wait_indefinitely = (timeout_duration == std::chrono::milliseconds(0));
    bool success = false;

    auto predicate = [&]() {
        std::lock_guard<std::recursive_mutex> res_lock_pred(_resource_registry_mutex);
        return _resource_registry.count(name) > 0;
    };

    if (wait_indefinitely) {
        wc->cv.wait(lock, predicate);
        success = true; // Assume wait doesn't return unless predicate is true or spurious wake
    } else {
        success = wc->cv.wait_for(lock, timeout_duration, predicate);
    }
    
    if (success) { // Predicate must be true, or spurious wakeup (predicate rechecked by standard)
        std::lock_guard<std::recursive_mutex> res_lock(_resource_registry_mutex);
        auto it = _resource_registry.find(name);
        if (it != _resource_registry.end()) {
            return it->second; // Return copy
        }
        // If predicate was true but now it's gone, means it was unregistered between wake and this lock.
        SystemLogger::warning("VisionRT", "Resource '" + name + "' disappeared after waiting. Returning empty.");
    } else {
        SystemLogger::debug("VisionRT", "WaitForResource timed out for: " + name);
    }
    return {}; // Timeout or resource not found after wait
}

bool VisionRT::SubscribeToResourceRegistration(const ResourceName& name, const ModuleID& subscriber_id, ResourceEventCallbackRaw callback) {
    if (!callback) {
        SystemLogger::error("VisionRT", "SubscribeToResourceRegistration: Null callback provided for resource '" + name + "'.");
        return false;
    }
    std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
    _resource_event_callbacks[name][subscriber_id] = std::move(callback);
    SystemLogger::info("VisionRT", "Module '" + subscriber_id + "' subscribed to resource '" + name + "'.");

    // Immediate callback if resource already exists
    std::any existing_resource_snapshot;
    bool resource_exists = false;
    {
        std::lock_guard<std::recursive_mutex> res_lock(_resource_registry_mutex);
        auto it = _resource_registry.find(name);
        if (it != _resource_registry.end()) {
            existing_resource_snapshot = it->second; // copy
            resource_exists = true;
        }
    }
    if (resource_exists) {
        SystemLogger::info("VisionRT", "Resource '" + name + "' already exists. Invoking callback immediately for subscriber '" + subscriber_id + "'.");
        try {
             // Access the just stored callback directly to avoid re-locking _event_system_mutex problems
            _resource_event_callbacks[name][subscriber_id](name, existing_resource_snapshot);
        } catch (const std::exception& e) {
            SystemLogger::error("VisionRT", "Exception in immediate resource callback for '" + name + "': " + e.what());
        }
    }
    return true;
}

bool VisionRT::UnsubscribeFromResourceRegistration(const ResourceName& name, const ModuleID& subscriber_id) {
    std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
    auto it_key = _resource_event_callbacks.find(name);
    if (it_key != _resource_event_callbacks.end()) {
        if (it_key->second.erase(subscriber_id) > 0) {
            SystemLogger::info("VisionRT", "Module '" + subscriber_id + "' unsubscribed from resource '" + name + "'.");
            if (it_key->second.empty()) {
                _resource_event_callbacks.erase(it_key); // Clean up map if no subscribers for this key
            }
            return true;
        }
    }
    SystemLogger::warning("VisionRT", "Module '" + subscriber_id + "' attempted to unsubscribe from resource '" + name + "' but was not subscribed or key not found.");
    return false;
}


void VisionRT::NotifyResourceRegisteredSubscribers(const ResourceName& name, const std::any& resource_snapshot) {
    std::vector<ResourceEventCallbackRaw> callbacks_to_run;
    std::shared_ptr<WaitContext> wc_to_notify;

    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        auto it_wc = _resource_wait_contexts.find(name);
        if (it_wc != _resource_wait_contexts.end()) {
            wc_to_notify = it_wc->second;
        }

        auto it_subs = _resource_event_callbacks.find(name);
        if (it_subs != _resource_event_callbacks.end()) {
            for (const auto& sub_pair : it_subs->second) { // sub_pair is <ModuleID, callback>
                callbacks_to_run.push_back(sub_pair.second);
            }
        }
    } // _event_system_mutex released

    if (wc_to_notify) {
        // No need to lock wc_to_notify->mtx before notifying.
        // CV wait acquires the lock itself.
        wc_to_notify->cv.notify_all();
    }

    if (!callbacks_to_run.empty()) {
        SystemLogger::debug("VisionRT", "Notifying " + std::to_string(callbacks_to_run.size()) + " subscribers for resource '" + name + "'.");
        for (const auto& cb : callbacks_to_run) {
            try {
                cb(name, resource_snapshot); // Pass copy
            } catch (const std::exception& e) {
                SystemLogger::error("VisionRT", "Exception in resource event callback for '" + name + "': " + e.what());
            } catch (...) {
                SystemLogger::error("VisionRT", "Unknown exception in resource event callback for '" + name + "'.");
            }
        }
    }
}


// --- Workspace Management ---

bool VisionRT::PublishToWorkspace(const ModuleID& publisher_module_id, const WorkspaceKey& key,
                                  std::shared_ptr<void> data, const std::type_info& type) {
    WorkspaceDataItem item_snapshot;
    bool item_updated = false;

    {
        std::lock_guard<std::recursive_mutex> lock(_workspace_mutex);
        auto it = _workspace.find(key);
        if (it != _workspace.end()) {
            std::lock_guard<std::mutex> item_lock(it->second.item_mutex); // Lock specific item
            it->second.data_ptr = std::move(data);
            it->second.type_info = std::type_index(type);
            it->second.publisher_id = publisher_module_id;
            it->second.last_updated_timestamp = std::chrono::system_clock::now();
            item_snapshot = it->second; // Make a copy for notification while item_lock is held
        } else {
            // Create new WorkspaceDataItem instance directly for emplace
            WorkspaceDataItem newItem(std::move(data), type, publisher_module_id);
            auto emplace_it = _workspace.emplace(key, std::move(newItem)).first;
            // Lock the newly emplaced item's mutex to safely take a snapshot
            std::lock_guard<std::mutex> item_lock(emplace_it->second.item_mutex);
            item_snapshot = emplace_it->second; // Make a copy for notification
        }
        item_updated = true;
        // SystemLogger::debug("VisionRT", "Published to workspace key: " + key); // Can be verbose
    } // _workspace_mutex (and item_mutex if held) released

    if (item_updated) {
        NotifyWorkspaceUpdateSubscribers(key, item_snapshot);
    }
    return true;
}

WorkspaceDataItem VisionRT::GetFromWorkspace(const WorkspaceKey& key) {
    std::lock_guard<std::recursive_mutex> lock(_workspace_mutex);
    auto it = _workspace.find(key);
    if (it != _workspace.end()) {
        std::lock_guard<std::mutex> item_lock(it->second.item_mutex); // Lock for consistent copy
        return it->second; // Return a copy (WorkspaceDataItem copy ctor makes new mutex)
    }
    return {}; // Default constructed (empty) WorkspaceDataItem
}

bool VisionRT::WorkspaceKeyExists(const WorkspaceKey& key) const {
    std::lock_guard<std::recursive_mutex> lock(_workspace_mutex);
    return _workspace.count(key) > 0;
}

void VisionRT::ClearWorkspaceKey(const WorkspaceKey& key) {
    bool erased_from_workspace = false;
    {
        std::lock_guard<std::recursive_mutex> lock(_workspace_mutex);
        if(_workspace.erase(key) > 0) {
            SystemLogger::info("VisionRT", "Workspace key cleared: " + key);
            erased_from_workspace = true;
        }
    } // _workspace_mutex released

    if (erased_from_workspace) {
        std::shared_ptr<WaitContext> wc_to_notify;
        {
            std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
            // Remove subscriptions and wait contexts for this key
            auto it_wc = _workspace_key_wait_contexts.find(key);
            if (it_wc != _workspace_key_wait_contexts.end()) {
                wc_to_notify = it_wc->second;
                _workspace_key_wait_contexts.erase(it_wc);
            }
            _workspace_event_callbacks.erase(key);
            SystemLogger::info("VisionRT", "Event subscriptions and wait contexts cleared for workspace key: " + key);
        }

        if (wc_to_notify) {
            wc_to_notify->cv.notify_all(); // Wake up waiters, they'll find data gone
        }
    }
}

void VisionRT::ClearAllWorkspace() {
    {
        std::lock_guard<std::recursive_mutex> lock(_workspace_mutex);
        _workspace.clear();
    } // _workspace_mutex released

    std::vector<std::shared_ptr<WaitContext>> all_wcs_to_notify;
    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        for(auto const& [key, val] : _workspace_key_wait_contexts) {
            if(val) all_wcs_to_notify.push_back(val);
        }
        _workspace_key_wait_contexts.clear();
        _workspace_event_callbacks.clear();
    }
    SystemLogger::info("VisionRT", "Entire workspace cleared, including event subscriptions and wait contexts.");

    for (const auto& wc : all_wcs_to_notify) {
        wc->cv.notify_all();
    }
}

WorkspaceDataItem VisionRT::WaitForWorkspaceKey(const WorkspaceKey& key, std::chrono::milliseconds timeout_duration) {
    std::shared_ptr<WaitContext> wc;
    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        auto it_wc = _workspace_key_wait_contexts.find(key);
        if (it_wc == _workspace_key_wait_contexts.end()) {
            wc = std::make_shared<WaitContext>();
            _workspace_key_wait_contexts.emplace(key, wc);
        } else {
            wc = it_wc->second;
        }
    }

    std::unique_lock<std::mutex> lock(wc->mtx);
    // Initial check (already holding wc->mtx)
    {
        std::lock_guard<std::recursive_mutex> ws_lock(_workspace_mutex);
        auto it = _workspace.find(key);
        if (it != _workspace.end()) {
            std::lock_guard<std::mutex> item_lock(it->second.item_mutex);
            return it->second; // Return a copy
        }
    }
    
    bool wait_indefinitely = (timeout_duration == std::chrono::milliseconds(0));
    bool success = false;

    auto predicate = [&]() {
        std::lock_guard<std::recursive_mutex> ws_lock_pred(_workspace_mutex);
        return _workspace.count(key) > 0; // Just check existence, GetFromWorkspace will handle copy
    };

    if (wait_indefinitely) {
        wc->cv.wait(lock, predicate);
        success = true; // Assume wait doesn't return unless predicate is true or spurious wake
    } else {
        success = wc->cv.wait_for(lock, timeout_duration, predicate);
    }

    if (success) { // Predicate must be true (or was true for a moment if spurious)
        // Re-acquire and return data, predicate already confirmed existence (or did briefly)
        return GetFromWorkspace(key); // GetFromWorkspace is thread-safe and returns copy
    } else {
        SystemLogger::debug("VisionRT", "WaitForWorkspaceKey timed out for: " + key);
    }
    return {}; // Timeout or data not found after wait
}


bool VisionRT::SubscribeToWorkspaceKey(const WorkspaceKey& key, const ModuleID& subscriber_id, WorkspaceEventCallbackRaw callback) {
    if (!callback) {
        SystemLogger::error("VisionRT", "SubscribeToWorkspaceKey: Null callback provided for key '" + key + "'.");
        return false;
    }
    std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
    _workspace_event_callbacks[key][subscriber_id] = std::move(callback);
    SystemLogger::info("VisionRT", "Module '" + subscriber_id + "' subscribed to workspace key '" + key + "'.");

    // Immediate callback if data already exists for this key
    WorkspaceDataItem existing_item_snapshot;
    bool item_exists = false;
    {
        std::lock_guard<std::recursive_mutex> ws_lock(_workspace_mutex);
        auto it = _workspace.find(key);
        if (it != _workspace.end()) {
            std::lock_guard<std::mutex> item_lck(it->second.item_mutex);
            existing_item_snapshot = it->second; // copy
            item_exists = true;
        }
    }

    if (item_exists) {
        SystemLogger::info("VisionRT", "Data for key '" + key + "' already exists. Invoking callback immediately for subscriber '" + subscriber_id + "'.");
        try {
            // Access the just stored callback directly to avoid re-locking _event_system_mutex problems
           _workspace_event_callbacks[key][subscriber_id](key, existing_item_snapshot);
        } catch (const std::exception& e) {
            SystemLogger::error("VisionRT", "Exception in immediate workspace callback for '" + key + "': " + e.what());
        }
    }
    return true;
}

bool VisionRT::UnsubscribeFromWorkspaceKey(const WorkspaceKey& key, const ModuleID& subscriber_id) {
    std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
    auto it_key = _workspace_event_callbacks.find(key);
    if (it_key != _workspace_event_callbacks.end()) {
        if (it_key->second.erase(subscriber_id) > 0) {
            SystemLogger::info("VisionRT", "Module '" + subscriber_id + "' unsubscribed from workspace key '" + key + "'.");
            if (it_key->second.empty()) {
                _workspace_event_callbacks.erase(it_key); // Clean up map if no subscribers for this key
            }
            return true;
        }
    }
    SystemLogger::warning("VisionRT", "Module '" + subscriber_id + "' attempted to unsubscribe from key '" + key + "' but was not subscribed or key not found.");
    return false;
}

void VisionRT::NotifyWorkspaceUpdateSubscribers(const WorkspaceKey& key, const WorkspaceDataItem& item_snapshot) {
    std::vector<WorkspaceEventCallbackRaw> callbacks_to_run;
    std::shared_ptr<WaitContext> wc_to_notify;

    {
        std::lock_guard<std::recursive_mutex> event_lock(_event_system_mutex);
        auto it_wc = _workspace_key_wait_contexts.find(key);
        if (it_wc != _workspace_key_wait_contexts.end()) {
            wc_to_notify = it_wc->second;
        }

        auto it_subs = _workspace_event_callbacks.find(key);
        if (it_subs != _workspace_event_callbacks.end()) {
            for (const auto& sub_pair : it_subs->second) { // sub_pair is <ModuleID, callback>
                callbacks_to_run.push_back(sub_pair.second);
            }
        }
    } // _event_system_mutex released

    if (wc_to_notify) {
        // No need to lock wc_to_notify->mtx before notifying.
        // CV wait acquires the lock itself.
        wc_to_notify->cv.notify_all();
    }

    if (!callbacks_to_run.empty()) {
        SystemLogger::debug("VisionRT", "Notifying " + std::to_string(callbacks_to_run.size()) + " subscribers for workspace key '" + key + "'.");
        for (const auto& cb : callbacks_to_run) {
            try {
                cb(key, item_snapshot); // Pass snapshot
            } catch (const std::exception& e) {
                SystemLogger::error("VisionRT", "Exception in workspace event callback for key '" + key + "': " + e.what());
            } catch (...) {
                SystemLogger::error("VisionRT", "Unknown exception in workspace event callback for key '" + key + "'.");
            }
        }
    }
}