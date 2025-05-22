#ifndef VISION_RT_COMMON_H
#define VISION_RT_COMMON_H

#include <string>
#include <memory>
#include <chrono>
#include <mutex>
#include <typeindex> // For std::type_index
#include <atomic>    // For ModuleState in ModuleRuntimeInfo if accessed from multiple threads
#include <functional> // For std::function
#include <any>

using ModuleID = std::string;
using ResourceName = std::string;
using WorkspaceKey = std::string;

enum class ModuleState {
    IDLE,           // Registered, not running
    INITIALIZING,   // Setup() called / in progress
    RUNNING,        // Loop() is active
    PAUSING,        // Transitioning to PAUSED (VisionRT signaled, module reacting)
    PAUSED,         // Loop() is suspended or in low-activity mode (confirmed by module or assumed)
    RESUMING,       // Transitioning from PAUSED to RUNNING
    STOPPING,       // Exit() called / in progress or Loop() returned false
    STOPPED,        // Thread finished execution
    ERROR_SETUP,    // Setup() failed
    ERROR_RUNTIME   // Loop() indicated an unrecoverable error or unhandled exception occurred
};

struct WorkspaceDataItem {
    std::shared_ptr<void> data_ptr;
    std::type_index type_info; 
    ModuleID publisher_id;
    std::chrono::system_clock::time_point last_updated_timestamp;
    mutable std::mutex item_mutex; // Protects this specific WorkspaceDataItem's members during modification/complex read.
                                   // For simple replacement of the item in the map, the outer _workspace_mutex is key.

    WorkspaceDataItem() : type_info(typeid(nullptr)) {} // Default for map operations

    WorkspaceDataItem(std::shared_ptr<void> ptr, const std::type_info& ti, ModuleID pub_id)
        : data_ptr(std::move(ptr)), 
          type_info(ti), 
          publisher_id(std::move(pub_id)),
          last_updated_timestamp(std::chrono::system_clock::now()) {}

    // Copy constructor: Creates a new mutex. Does not copy the locked state.
    WorkspaceDataItem(const WorkspaceDataItem& other)
        : data_ptr(other.data_ptr),
          type_info(other.type_info),
          publisher_id(other.publisher_id),
          last_updated_timestamp(other.last_updated_timestamp) {
        // std::mutex is not copyable. A new mutex is implicitly created.
    }

    // Copy assignment: Creates a new mutex.
    WorkspaceDataItem& operator=(const WorkspaceDataItem& other) {
        if (this == &other) return *this;
        data_ptr = other.data_ptr;
        type_info = other.type_info;
        publisher_id = other.publisher_id;
        last_updated_timestamp = other.last_updated_timestamp;
        // The existing this->item_mutex remains, new one not created for assignment.
        return *this;
    }

    // Move constructor
    WorkspaceDataItem(WorkspaceDataItem&& other) noexcept
        : data_ptr(std::move(other.data_ptr)),
          type_info(std::move(other.type_info)),
          publisher_id(std::move(other.publisher_id)),
          last_updated_timestamp(std::move(other.last_updated_timestamp)) {
        // std::mutex is not movable in the traditional sense of transferring state.
    }

    // Move assignment
    WorkspaceDataItem& operator=(WorkspaceDataItem&& other) noexcept {
        if (this == &other) return *this;
        data_ptr = std::move(other.data_ptr);
        type_info = std::move(other.type_info);
        publisher_id = std::move(other.publisher_id);
        last_updated_timestamp = std::move(other.last_updated_timestamp);
        return *this;
    }
};

// Callback types for VisionRT level (dealing with raw WorkspaceDataItem and std::any)
using WorkspaceEventCallbackRaw = std::function<void(const WorkspaceKey&, const WorkspaceDataItem&)>;
using ResourceEventCallbackRaw = std::function<void(const ResourceName&, const std::any&)>;


#endif // VISION_RT_COMMON_H