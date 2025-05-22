#include "utils/Logger.h"
#include <iostream> // For console output mirror
#include <sstream> // For string stream
#include <iomanip> // For std::put_time

SystemLogger* SystemLogger::_instance = nullptr;

void SystemLogger::initialize(const std::string& filename) {
    if (!_instance) {
        _instance = new SystemLogger(filename);
    }
}

SystemLogger* SystemLogger::getInstance() {
    // Ensure initialized, or handle error/default init
    if (!_instance) {
        initialize(); // Default initialization if not called explicitly
    }
    return _instance;
}

SystemLogger::SystemLogger(const std::string& filename) {
    _logFile.open(filename, std::ios::out | std::ios::app);
    if (!_logFile.is_open()) {
        std::cerr << "ERROR: Could not open log file: " << filename << std::endl;
    }
    log(Level::INFO, "Logger", "SystemLogger initialized. Logging to: " + filename);
}

SystemLogger::~SystemLogger() {
    log(Level::INFO, "Logger", "SystemLogger shutting down.");
    if (_logFile.is_open()) {
        _logFile.close();
    }
}

std::string SystemLogger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    // Add milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string SystemLogger::levelToString(Level level) {
    switch (level) {
        case Level::DEBUG:   return "DEBUG";
        case Level::INFO:    return "INFO";
        case Level::WARNING: return "WARNING";
        case Level::ERROR:   return "ERROR";
        default:           return "UNKNOWN";
    }
}

void SystemLogger::log(Level level, const std::string& componentID, const std::string& message) {
    std::lock_guard<std::mutex> lock(_mutex);
    std::string logEntry = "[" + getCurrentTimestamp() + "] [" + levelToString(level) + "] [" + componentID + "] " + message;
    
    // Log to console (optional, good for dev)
    std::cout << logEntry << std::endl;

    // Log to file
    if (_logFile.is_open()) {
        _logFile << logEntry << std::endl;
    }
}

// Static convenience methods
void SystemLogger::debug(const std::string& componentID, const std::string& message) {
    if (_instance) _instance->log(Level::DEBUG, componentID, message);
}
void SystemLogger::info(const std::string& componentID, const std::string& message) {
    if (_instance) _instance->log(Level::INFO, componentID, message);
}
void SystemLogger::warning(const std::string& componentID, const std::string& message) {
    if (_instance) _instance->log(Level::WARNING, componentID, message);
}
void SystemLogger::error(const std::string& componentID, const std::string& message) {
    if (_instance) _instance->log(Level::ERROR, componentID, message);
}