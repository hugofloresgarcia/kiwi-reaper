#include "reaper_plugin_functions.h"
#include "osc.h"

class OSCController : public IReaperControlSurface {

public: 
  const char* GetTypeString() override { return "HapticScrubber"; }
  const char* GetDescString() override { return "A tool for scrubbing audio using haptic feedback."; }
  const char* GetConfigString() override { return ""; }

  OSCController(OSCManager manager):
    m_manager(manager) {
  }

  // TODO: should switch focus to last selected track
  void OnTrackSelection(MediaTrack *trackid) override;
  
  // use this to register all callbacks with the osc manager
  void add_callbacks() {
    using Msg = oscpkt::Message;

    // add a callback to listen to
    m_manager.add_callback(
      "/example", [](Msg& msg){
        
    });
  };

  // this runs about 30x per second. do all OSC polling here
  void Run() override {
    // handle any packets
    m_manager.handle_receive(false);
  }

private:
  OSCManager m_manager;

};