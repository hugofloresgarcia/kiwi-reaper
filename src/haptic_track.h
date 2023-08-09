#pragma once

#include "mipmap.h"
#include "log.h"

#include "reaper_plugin_functions.h"

#define project nullptr
using std::unordered_map;
using std::shared_ptr;

// TODO: we're gonna need to figure out how to find out 
// if a track has been destroyed, and, if so, how to 
// clean up any haptic tracks associated with it. 
// see: ValidatePtr2
class haptic_track_t {
public: 
    haptic_track_t()
      :m_accessor(nullptr) {};
    haptic_track_t(MediaTrack* track)
      :m_track(track), 
       m_accessor(std::make_shared<audio_accessor_t>(track)) {
        setup();
    };
  
    void setup() {
        // debug("setting up haptic track with address {:p}", (void*)m_track);
        if (!m_track) {
            info ("track is null. nothing to set up");
            return;
        }

        // TODO: how are we dealing when the resolution is larger than the track itself? 
        std::vector<int> resolutions = {256, 1024, 4096, 16384, 65536, 262144};
        std::vector<double> pix_per_s_res;
        for (int res : resolutions) {
            double pps_res = samples_per_pix_to_pps(res, m_accessor->sample_rate());
            // avoid zero resolutions as th         ey don't make sense
            if (pps_res > 0.00001)
                pix_per_s_res.push_back(pps_res);
        }

        m_mipmap = std::make_shared<audio_pixel_mipmap_t>(m_accessor, pix_per_s_res);
        m_mipmap->update([this] (audio_pixel_mipmap_t& map) {
            this->update_active_block();
        }, true);
        m_active_channel = 0;
    } 
    
    void next_channel() {
        m_active_channel = (m_active_channel + 1) % m_accessor->num_channels(); 
    }
    void prev_channel() {
        m_active_channel = (m_active_channel - 1) % m_accessor->num_channels(); 
    } 
    int get_active_channel() {  
        return m_active_channel; 
    }

    // returns an audio pixel block 
    // at the current resolution
    audio_pixel_block_t& get_pixels() {
        update_active_block();
        return m_active_block;
    }

    audio_pixel_t get_pixel(int mip_map_index) {
        update_active_block();
        return m_active_block.get_pixels().at(m_active_channel).at(mip_map_index);
    }

    void set_cursor(int mip_map_idx) {
        double t = mip_map_idx / GetHZoomLevel() + m_accessor->get_time_bounds().first;
        // debug("setting cursor to mipmap  l;index {}, at time {}", mip_map_idx, t);
        SetEditCurPos(t, true, true);
    }

    int get_cursor_mip_map_idx() {
        double t0 = m_accessor->get_time_bounds().first;
        double t = GetCursorPosition() - t0;
        int mip_map_idx = floor(t * GetHZoomLevel());
        // debug("getting cursor position, returning mipmap index {}", mip_map_idx);
        return mip_map_idx;
    }

    static void zoom(double amt) {
        debug("zooming by {}", amt);
        adjustZoom(GetHZoomLevel() * amt, 1, true, -1);
    }

    int get_track_number() const {
        return get_track_number(m_track);
    }

    MediaTrack *get_track() const {
        return m_track;
    }

    //  track number 1-based, 0=not found, -1=master track (read-only, returns the int directly)
    static int get_track_number(MediaTrack* track) {
        // debug("looking for the track number of track {:p}", (void*)track);
        return GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
    }

    shared_ptr<audio_pixel_mipmap_t> mipmap() { 
        return m_mipmap; 
    }

private:
    // asks the mipmap for an interpolated block of pixels
    // use this to update the internal active block
    audio_pixel_block_t calculate_pixels() {
        // debug("getting pixels for track {:p}", (void*)m_track);
        if (!m_mipmap) {
            // debug("no mipmap for track {:p}", (void*)m_track);
            return audio_pixel_block_t();
        }
        double pix_per_s = GetHZoomLevel();

        return m_mipmap->get_pixels(std::nullopt, std::nullopt, pix_per_s);
    }

    // updates the active block, if necessary
    void update_active_block() {
        double pix_per_s = GetHZoomLevel();

        if (pix_per_s != m_active_block.get_pps()) {
            m_active_block = this->calculate_pixels();
        }
    }

private:
    int m_active_channel {0};

    MediaTrack* m_track {nullptr};
    audio_pixel_block_t m_active_block;
    shared_ptr<audio_pixel_mipmap_t> m_mipmap {nullptr};
    shared_ptr<audio_accessor_t> m_accessor {nullptr};
};

// a map to hold one haptic track per MediaTrack
class haptic_track_map_t {
public:

    haptic_track_map_t() {
        // start with the master track
        MediaTrack* master_track = GetMasterTrack(project);
    }

    shared_ptr<haptic_track_t> active() { 
        // TODO: check that the track we're returning actually 
        // still exists
        // debug("returning active haptic track with index: {}", active_track);
        if (tracks.size() == 0) {
            return nullptr;
        } else {
            return tracks.at(active_track); 
        }
    };

    void add(MediaTrack* track) {
        // debug("adding haptic track with address {:p}", (void*)track);
        int tracknum = haptic_track_t::get_track_number(track);
        if (tracknum == 0) 
            track = nullptr;
            
            // only add if it's new
            if (tracks.find(tracknum) == tracks.end()) {
            debug("track is new. adding!");
            tracks[tracknum] = std::make_shared<haptic_track_t>(track);

            active_track = tracknum;
        } else {
            debug("track is already in the map. not adding!");
        }
    }

    void active(MediaTrack* track) {
        int tracknum = haptic_track_t::get_track_number(track);
        if (!(tracks.find(tracknum) == tracks.end())) {
            active_track = tracknum;
        } else {
            debug("track not found in track map");
        }
    }

private:
    unordered_map<int, shared_ptr<haptic_track_t>> tracks;
    int active_track {-1}; // master
};
