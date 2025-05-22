#ifndef ModuleA_H
#define ModuleA_H

#include "VisionRT/vision_rt_module.h" 
#include <string>
#include <chrono>
#include <thread>

class ModuleA : public VisionRT_Module {
public:
    ModuleA(const std::string& moduleName, const std::string& moduleVersion);
    ~ModuleA();

    bool Setup() override;
    bool Loop() override;
    void Exit() override;

    private:
    // nothing

};

#endif //ModuleA_H