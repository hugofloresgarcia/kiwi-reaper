#include "reaper_plugin_functions.h"
#include "osc.h"


class OSCController : public IReaperControlSurface {

public: 
    const char* GetTypeString() override { return ""; }
    const char* GetDescString() override { return ""; }
    const char* GetConfigString() override { return ""; }

    OSCController(OSCManager manager):
        m_manager(manager) {

    }

    void Run() override {
        // this runs about 30x per second. do all OSC polling here
        
    }

private:
    OSCManager m_manager;

};