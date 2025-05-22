#ifndef ModuleB_H
#define ModuleB_H

#include "VisionRT/vision_rt_module.h" 
#include <string>
#include <chrono>
#include <thread>

class ModuleB : public VisionRT_Module {
public:
    ModuleB(const std::string& moduleName, const std::string& moduleVersion);
    ~ModuleB();

    bool Setup() override;
    bool Loop() override;
    void Exit() override;

    private:
    // nothing

};

#endif //ModuleA_H