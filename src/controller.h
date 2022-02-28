#include "audio_pixel.h"
#include "osc.h"

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
    m_manager->add_callback("/move_edit_cursor",
    [](Msg& msg){
      float move_amt;
      if (msg.arg().popFloat(move_amt)
                   .isOkNoMoreArgs()){
        SetEditCurPos(
          GetCursorPosition() + (double)move_amt, 
          true, true
        );
      }
    });

    m_manager->add_callback("/zoom",
    [](Msg& msg){
      int xdir;
      if (msg.arg().popInt32(xdir)
                   .isOkNoMoreArgs()){
        CSurf_OnZoom(xdir, 0);
      }
    });

    m_manager->add_callback("/init",
    [this](Msg& msg){
      // // the first selected media item for now
      MediaItem* item = GetSelectedMediaItem(0, 0);
      if (!item) {return;} // TODO: LOG ME

      MediaTrack* track = (MediaTrack*)GetSetMediaItemInfo(item, "P_TRACK", nullptr);
      std::vector<int> resolutions = {41000, 10250, 2561, 639, 107};
      m_mipmap = std::make_unique<audio_pixel_mipmap_t>(track, resolutions);

      // audio_pixel_block_t interpolated_block = m_mipmap->get_pixels(10, -1, 300);
      audio_pixel_block_t interpolated_block = m_mipmap->get_block(300);
      interpolated_block.flush();
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
