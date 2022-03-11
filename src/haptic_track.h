#pragma once

#include "audio_pixel.h"
#include "log.h"

#include "reaper_plugin_functions.h"


using std::unordered_map;

// TODO: we're gonna need to figure out how to find out 
// if a track has been destroyed, and, if so, how to 
// clean up any haptic tracks associated with it. 
// see: ValidatePtr2
class haptic_track_t {
public: 
  haptic_track_t() {};
  haptic_track_t(MediaTrack* track): m_track(track) {
      setup();
  };
  
  void setup() {
    debug("setting up haptic track with address {:x}", (void*)m_track);
    if (!m_track) {
      return;// TODO: LOG ME
    }

    // TODO: how are we dealing when the resolution is larger than the track itself? 
    std::vector<int> resolutions = {256, 1024, 4096, 16384, 65536, 262144};
    std::vector<double> pix_per_s_res;
    for (int res : resolutions) {
      pix_per_s_res.push_back(samples_per_pix_to_pps(res, audio_pixel_mipmap_t::sample_rate()));
    }

    m_mipmap = std::make_unique<audio_pixel_mipmap_t>(m_track, pix_per_s_res);

    m_active_channel = 0;
  }

  // TODO:should probably update mipmaps
  void update() {}; 
 
  void next_channel() { m_active_channel = (m_active_channel + 1) % m_mipmap->num_channels(); }
  void prev_channel() { m_active_channel = (m_active_channel - 1) % m_mipmap->num_channels(); } 
  int get_active_channel() {  return m_active_channel; }
  
  // returns the current track name
  std::string get_track_name();
  std::string get_take_name();

  int get_pixel_frame_width() { return m_pixel_frame_width; }

  // moves the selected region by an amt
  // TODO: implement me
  void move();

  // slices at current cursor position
  // TODO: implement me
  void slice();

  // returns the pixels at the current resolution
  // note: only returns a small frame of pixels, (size get_pixel_frame_width())
  // centered around the current cursor position
  audio_pixel_block_t get_pixels() {
    debug("getting pixels for track {:x}", (void*)m_track);
    if (!m_mipmap) {
      debug("no mipmap for track {:x}", (void*)m_track);
      return audio_pixel_block_t();
    }
    // get the current resolution
    double pix_per_s = GetHZoomLevel();

    // get the current cursor position
    double curr_t = GetCursorPosition();

    // the mipmap should be able to do the clamping against the track length
    int center_idx = time_to_pixel_idx(curr_t, pix_per_s);
    int left_idx = std::max(0, center_idx - m_pixel_frame_width / 2);
    int right_idx = center_idx + m_pixel_frame_width / 2;

    double t0 = pixel_idx_to_time(left_idx, pix_per_s);
    double t1 = pixel_idx_to_time(right_idx, pix_per_s);

    return m_mipmap->get_pixels(t0, t1, pix_per_s);
  }

  static void set_cursor(int mip_map_idx) {
    double t = mip_map_idx / GetHZoomLevel();
    debug("setting cursor to mipmap index {}, at time {}", mip_map_idx, t);
    SetEditCurPos(t, true, true);
  }

  static void zoom(double amt) {
    debug("zooming by {}", amt);
    adjustZoom(GetHZoomLevel() * amt, 1, true, -1);
  }

  //  track number 1-based, 0=not found, -1=master track (read-only, returns the int directly)
  static int get_track_number(MediaTrack* track) {
    debug("looking for the track number of track {:x}", (void*)track);
    return GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
  }

  audio_pixel_mipmap_t& mipmap() { 
    assert(m_mipmap);
    return *m_mipmap; 
  }

private:
  MediaTrack* m_track {nullptr};
  MediaItem* m_item {nullptr};
  MediaItem_Take* m_take {nullptr};

  int m_active_channel {0};
  int m_pixel_frame_width {512};

  std::unique_ptr<audio_pixel_mipmap_t> m_mipmap {nullptr};
};

// a map to hold one haptic track per MediaTrack
class haptic_track_map_t {
public:
  haptic_track_t& active() { 
    // TODO: check that the track we're returning actually 
    // still exists
    debug("returning active haptic track with index: {}", active_track);
    return tracks[active_track]; 
  };

  void add(MediaTrack* track) {
    debug("adding haptic track with address {:x}", (void*)track);
    int tracknum = haptic_track_t::get_track_number(track);
    if (tracknum == 0) 
      track = nullptr;
    
    // only add if it's new
    if (tracks.find(tracknum) == tracks.end()) {
      debug("track is new. adding!");
      tracks[tracknum] = haptic_track_t(track);

      active_track = tracknum;
    } else {
      debug("track is already in the map. not adding!");
    }
  }

  unordered_map<int, haptic_track_t> tracks;
  int active_track {-1}; // master
};
