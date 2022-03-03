#include "audio_pixel.h"
#include "osc.h"
#include <thread>

#include "reaper_plugin_functions.h"

#define project nullptr

class HapticTrack {

public: 
  HapticTrack() {
    // the first selected media item for now
    m_item = GetSelectedMediaItem(0, 0);
    if (!m_item) {
      return; // TODO: LOG ME
    }

    // get a hold of the track
    MediaTrack* track = (MediaTrack*)GetSetMediaItemInfo(m_item, "P_TRACK", nullptr);

    setup(track);
  }

  
  void setup(MediaTrack *track) {
    if (!track) {
      return;// TODO: LOG ME
    }

    m_track = track;

    // TODO: how are we dealing when the resolution is larger than the track itself? 
    std::vector<int> resolutions = {256, 1024, 4096, 16384, 65536, 262144};
    std::vector<double> pix_per_s_res;
    for (int res : resolutions) {
      pix_per_s_res.push_back(samples_per_pix_to_pps(res, audio_pixel_mipmap_t::sample_rate()));
    }

    m_mipmap = std::make_unique<audio_pixel_mipmap_t>(m_track, pix_per_s_res);

    m_active_channel = m_mipmap->num_channels();
  }

  // moves to the prev take or track
  HapticTrack prev();

  // moves to the next take or track
  HapticTrack next();
 
  void next_channel() {
    m_active_channel = (m_active_channel + 1) % m_mipmap->num_channels();
  }

  void prev_channel() {
    m_active_channel = (m_active_channel + 1) % m_mipmap->num_channels();
  } 

  void set_cursor(int mip_map_idx) {
    double t = mip_map_idx / GetHZoomLevel();
    SetEditCurPos(t, true, true);
  }

  void zoom(double amt) {
    adjustZoom(GetHZoomLevel() * amt, 1, true, -1);
  }

  int get_active_channel() { return m_active_channel; }
  
  // returns the current track name
  std::string get_track_name();

  // returns the current take name
  std::string get_take_name();

  // moves the selected region by an amt
  // TODO: implement me
  void move();

  // slices at current cursor position
  // TODO: implement me
  void slice();

  // returns the pixels at the current resolution
  audio_pixel_block_t get_pixels() {
    // get the current resolution
    double pix_per_s = GetHZoomLevel();

    return m_mipmap->get_pixels(std::nullopt, std::nullopt, pix_per_s);
  }

private:
  MediaTrack* m_track {nullptr};
  MediaItem* m_item {nullptr};
  MediaItem_Take* m_take {nullptr};

  int m_active_channel {0};

  std::unique_ptr<audio_pixel_mipmap_t> m_mipmap {nullptr};
};

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
    // TODO: we should have a pointer to an
    // active track object 
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
    [this](Msg& msg){
      int index;
      if (msg.arg().popInt32(index)
                   .isOkNoMoreArgs()){
        m_track->set_cursor(index);
      }
    });

    m_manager->add_callback("/zoom",
    [this](Msg& msg){
      double amt;
      if (msg.arg().popDouble(amt)
                   .isOkNoMoreArgs()){
        m_track->zoom(amt);
      }
    });

    m_manager->add_callback("/init",
    [this](Msg& msg){
      m_track = std::make_unique<HapticTrack>();
      
      audio_pixel_block_t interpolated_block = m_track->get_pixels();
      const vec<audio_pixel_t>& pixels = interpolated_block.get_pixels()[m_track->get_active_channel()];

      json j;
      interpolated_block.to_json(j);

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
    });
  };

  // this runs about 30x per second. do all OSC polling here
  virtual void Run() override {
    // handle any packets
    m_manager->handle_receive(false);
  }

private:
  std::unique_ptr<OSCManager> m_manager {nullptr};
  std::unique_ptr<HapticTrack> m_track;

};
