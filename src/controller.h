#include "reaper_plugin_functions.h"
#include "osc.h"
#include "include/json/json.hpp"

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

    m_manager->add_callback("/get_peaks", 
    [this](Msg& msg){
      float t0; float t1;
      if (!msg.arg().popFloat(t0)
                   .popFloat(t1)
                   .isOkNoMoreArgs())
        // TODO: log bad message;
        { return; }
        
      // the first selected media item for now
      MediaItem* item = GetSelectedMediaItem(project, 0);
      if (!item) {return;} // TODO: LOG ME

      // crop t0 and t1 to the media item's bounds
      // TODO: make t0 and t1 doubles from the begging to avoid casting down.
      t0 = std::max(t0, (float)*(double*)GetSetMediaItemInfo(item, "D_POSITION", nullptr));
      t1 = std::min(t1, (float)*(double*)GetSetMediaItemInfo(item, "D_LENGTH", nullptr));

      if (t0 >= t1) {return;} // TODO: LOG ME

      MediaTrack* track = (MediaTrack*)GetSetMediaItemInfo(item, "P_TRACK", nullptr);
      if (!track) {return;} // TODO: LOG ME

      // int num_channels = GetMediaTrackInfo_Value(track, "I_NCHAN");
      int num_channels = 1;

      // get the active take so we can look at it's peaks
      MediaItem_Take* take = GetActiveTake(item);
      if (!take) {return;} // TODO: LOG ME

      PCM_source* source = GetMediaItemTake_Source(take);
      if (!source) {return;} // TODO: LOG ME

      // the peaks!
      double pix_per_s = GetHZoomLevel();
      int num_pixels = (int)((t1 - t0) * pix_per_s + 0.5);
      std::vector<double> peaks(num_channels * num_pixels * 3);

      int peak_rv = PCM_Source_GetPeaks(source, pix_per_s, t0, num_channels, 
                                  num_pixels, 115, peaks.data());
      auto spl_cnt  = (peak_rv & 0xfffff);
      auto ext_type = (peak_rv & 0x1000000)>>24;
      auto out_mode = (peak_rv & 0xf00000)>>20;


          // {return;} // TODO: log me

      // // put the peaks into json
      // json j;
      // j["peaks"] = peaks;
      // j["num_channels"] = num_channels;
      // j["num_pixels"] = num_pixels;
      // j["pix_per_s"] = pix_per_s;

      // // send!
      // oscpkt::Message reply;
      // reply.init("/peaks").pushStr(j.dump());
      // m_manager->send(reply);
    });
  };

  // this runs about 30x per second. do all OSC polling here
  virtual void Run() override {
    // handle any packets
    m_manager->handle_receive(false);
  }

private:
  std::unique_ptr<OSCManager> m_manager {nullptr};
};
