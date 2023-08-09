#pragma once

#include "reaper_plugin_functions.h"
#include "log.h"


using std::pair; 

// wraps an audio accessor
class audio_accessor_t {
public:
    audio_accessor_t(MediaTrack* track) 
      : m_accessor(CreateTrackAudioAccessor(track)), m_track(track) { };

    ~audio_accessor_t() { 
        if(m_accessor) { 
            // https://github.com/reaper-oss/sws/blob/bcc8fbc96f30a943bd04fb8030b4a03ea1ff7557/Breeder/BR_Loudness.cpp#L246
            if (EnumProjects(0, NULL, 0))
                DestroyAudioAccessor(m_accessor); 
        } 
    };

    void reset() {
        if (m_accessor) { DestroyAudioAccessor(m_accessor); }
        m_accessor = CreateTrackAudioAccessor(m_track);
    }

    bool get_samples(std::vector<double>& buffer) {
        // TODO: if audio is longer than 28800000 samples (arbitrary, 
        // then ask for the samples in chunks. 
        if (!this->is_valid()) {
            // info("got invalid audio accessor for track {:x}", (void*)m_track);
            return false;
        }

        double t_start = GetAudioAccessorStartTime(m_accessor);
        double t_end = GetAudioAccessorEndTime(m_accessor);
        m_time_bounds = std::make_pair(t_start, t_end);

        debug("mipmap: accessor start time: {}; end time: {};", t_start, t_end);
        if (t_end <= t_start) {
            info("mipmap: accessor end time is less than or equal to start time");
            return false;
        }
        
        // calculate the number of samples we want to collect per channel
        int samples_per_channel = sample_rate() * (t_end - t_start);
        buffer.resize(samples_per_channel * num_channels());

        int result = GetAudioAccessorSamples(m_accessor, sample_rate(), num_channels(), 
                                                    t_start, samples_per_channel, 
                                                    buffer.data());

        if (result == 0){
            info("no audio");
        } else if (result < 1) {
            info("failed to get samples from accessor: error: {}", result);
            return false;
        } else {
            debug("got result {} from accessor", result);
        }

        return true;
    }

    pair<double, double> get_time_bounds() { return m_time_bounds; }

    int num_channels() const { 
        return (int)GetMediaTrackInfo_Value(m_track, "I_NCHAN"); 
    }

    static int sample_rate() {
        GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true);
        return (int)GetSetProjectInfo(0, "PROJECT_SRATE", 0, true); 
    }

    bool state_changed() {
        return AudioAccessorStateChanged(m_accessor);
    }

    void update() {
        AudioAccessorUpdate(m_accessor);
        AudioAccessorValidateState(m_accessor);
    }

    AudioAccessor* get() { return m_accessor; };
    bool is_valid() { 
        return m_accessor; 
    };
    
private:
    pair<double, double> m_time_bounds;
    AudioAccessor* m_accessor {nullptr};
    MediaTrack* m_track {nullptr};
};