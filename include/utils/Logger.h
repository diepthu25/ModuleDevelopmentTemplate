#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip> // For std::put_time

class SystemLogger {
public:
    enum class Level { DEBUG, INFO, WARNING, ERROR };

    SystemLogger(const std::string& filename = "log.txt");
    ~SystemLogger();

    void log(Level level, const std::string& componentID, const std::string& message);

    // Static convenience methods
    static void debug(const std::string& componentID, const std::string& message);
    static void info(const std::string& componentID, const std::string& message);
    static void warning(const std::string& componentID, const std::string& message);
    static void error(const std::string& componentID, const std::string& message);

    static void initialize(const std::string& filename = "log.txt");
    static SystemLogger* getInstance();

private:
    std::ofstream _logFile;
    std::mutex _mutex;
    static SystemLogger* _instance;

    std::string levelToString(Level level);
    std::string getCurrentTimestamp();
};

#endif // LOGGER_H