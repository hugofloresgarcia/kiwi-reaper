#include "audio_pixel.h"
#include "osc.h"
#include <thread>

#include "reaper_plugin_functions.h"

#define project nullptr

class OSCController : IReaperControlSurface {
  OSCController();
public: 
  OSCController(std::string& addr, int send_port, int recv_port)
    : m_manager(std::make_unique<OSCManager>(addr, send_port, recv_port)) {}

  virtual const char* GetTypeString() override { return "HapticScrubber"; }
  virtual const char* GetDescString() override { return "A tool for scrubbing audio using haptic feedback."; }
  virtual const char* GetConfigString() override { return ""; }

  bool init () {
    bool success = m_manager->init();
    add_callbacks();
    return success;
  }

  virtual ~OSCController() {}

  // TODO: should switch focus to last selected track
  void OnTrackSelection(MediaTrack *trackid) override {};
  
  // use this to register all callbacks with the osc manager
  void add_callbacks() {
    using Msg = oscpkt::Message;

    // add a callback to listen to
    m_manager->add_callback("/set_cursor",
    [](Msg& msg){
      int index;
      if (msg.arg().popInt32(index)
                   .isOkNoMoreArgs()){
        double t = index / GetHZoomLevel();
        SetEditCurPos(
          t, 
          true, true
        );
      }
    });

    m_manager->add_callback("/zoom",
    [](Msg& msg){
      double amt;
      if (msg.arg().popDouble(amt)
                   .isOkNoMoreArgs()){
        adjustZoom(GetHZoomLevel() * amt, 1, true, -1);
        // CSurf_OnZoom(xdir, 0);
      }
    });

    m_manager->add_callback("/init",
    [this](Msg& msg){
      // // the first selected media item for now
      MediaItem* item = GetSelectedMediaItem(0, 0);
      if (!item) {return;} // TODO: LOG ME

      MediaTrack* track = (MediaTrack*)GetSetMediaItemInfo(item, "P_TRACK", nullptr);
      std::vector<double> resolutions = {10250, 2561, 639, 107, 1};
      m_mipmap = std::make_unique<audio_pixel_mipmap_t>(track, resolutions);

      // get the current resolution
      double pix_per_s = GetHZoomLevel();
      audio_pixel_block_t interpolated_block = m_mipmap->get_pixels(std::nullopt, std::nullopt, pix_per_s);

      json j;
      interpolated_block.to_json(j);

      // just gonna send the first channel for now
      int channel = 0;
      const vec<audio_pixel_t>& pixels = interpolated_block.get_pixels()[channel];

      // send the pixels, one by one,
      // along w/ an index
      for (int i = 0 ; i < pixels.size() ; i++) {
        oscpkt::Message msg("/pixel");
        json j;
        j["id"] = i;
        j["value"] = pixels[i].m_max;

        msg.pushStr(j.dump());
        
        // send the message
        m_manager->send(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      
    } 


    );
  };

  // this runs about 30x per second. do all OSC polling here
  virtual void Run() override {
    // handle any packets
    m_manager->handle_receive(false);
  }

private:
  std::unique_ptr<OSCManager> m_manager {nullptr};
  std::unique_ptr<audio_pixel_mipmap_t> m_mipmap {nullptr};
};
