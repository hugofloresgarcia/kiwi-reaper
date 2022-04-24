#pragma once

#define JSON_USE_IMPLICIT_CONVERSIONS 0

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include "include/json/json.hpp"
#include "include/ThreadPool/ThreadPool.h"
#include <vector> 
#include <shared_mutex>

#include "reaper_plugin_functions.h"

#include "log.h"

using json = nlohmann::json;

template<typename T> 
using vec = std::vector<T>;

template<typename T> 
using opt = std::optional<T>; 

using std::shared_ptr;

class audio_pixel_transform_t;

// helpers!
int time_to_pixel_idx(double time, double pix_per_s);
double pixel_idx_to_time(int pixel_idx, double pix_per_s);
int pps_to_samples_per_pix(double pix_per_s, int sample_rate);
double samples_per_pix_to_pps(int samples_per_pix, int sample_rate);
double linear_interp(double x, double x1, double x2, double y1, double y2);

template<typename T>
const vec<T> get_view(const vec<T>& parent, int start, int end)
{
    end = std::min(end, (int)parent.size()); 
    start = std::max(start, 0);   
    // todo: make sure start and  end indices are valid
    auto startptr = parent.cbegin() + start;
    auto endptr = parent.cbegin() + end;
 
    vec<T> vec(startptr, endptr);
    return vec;
}

// wraps an audio accessor
class safe_audio_accessor_t {
public:
    safe_audio_accessor_t(MediaTrack* track) 
      : m_accessor(CreateTrackAudioAccessor(track)), m_track(track) {
          
        // state_changed();
     };

    ~safe_audio_accessor_t() { 
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

    bool state_changed() {
        bool changed = AudioAccessorStateChanged(m_accessor);
        return changed;
    }

    AudioAccessor* get() { return m_accessor; };
    bool is_valid() { 
        return m_accessor; 
    };
    
private:
    AudioAccessor* m_accessor {nullptr};
    MediaTrack* m_track {nullptr};
};

// one sample of audio pixel data, which stores max, min, and rms values
// avoid making these when the pixel-to-sample ratio is 1:1, since we can 
// just store the raw sample value in that case
class audio_pixel_t {
public: 
    audio_pixel_t() {};
    audio_pixel_t(double max, double min, double rms) : m_max(max), m_min(min), m_rms(rms) {};

    // linearly interpolate between two pixels
    static audio_pixel_t linear_interpolation(double t, double t0, double t1, audio_pixel_t p0, audio_pixel_t p1){
        audio_pixel_t p;
        p.m_max = linear_interp(t, t0, t1, p0.m_max, p1.m_max);
        p.m_min = linear_interp(t, t0, t1, p0.m_min, p1.m_min);
        p.m_rms = linear_interp(t, t0, t1, p0.m_rms, p1.m_rms);
        return p;
    }

    double m_max{ std::numeric_limits<double>::lowest() };
    double m_min { std::numeric_limits<double>::max() };
    double m_rms { 0 };

public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(audio_pixel_t, m_max, m_min, m_rms);
};

// stores one block of mipmapped audio data, at a particular sample rate
// should be able to update when the samples are updated
// not thread safe by itself
class audio_pixel_block_t {
public: 
    audio_pixel_block_t() {};
    audio_pixel_block_t(double pix_per_s)
        :m_pix_per_s(pix_per_s) {};
    ~audio_pixel_block_t() {};

    audio_pixel_block_t clone() const {
        audio_pixel_block_t block(*this);
        block.m_channel_pixels = std::make_shared<
                                    vec<vec<audio_pixel_t>>
                                >(*m_channel_pixels);
        return block;
    }

    // returns a ref to the entire block of audio pixels
    const vec<vec<audio_pixel_t>>& get_pixels() const { return *m_channel_pixels; };

    // returns a VIEW (not a copy) of pixels for the specified time range
    const audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1) const;

    // creates a new block at a new resolution, via linear interpolation
    audio_pixel_block_t interpolate(double new_pps) const;

    // gets the resolution (in pixels per second)
    double get_pps() const { return m_pix_per_s; };
    
    // number of pixels in a block (for a single channel)
    int get_num_pix_per_channel() const;

    // fill a json object with a block
    void to_json(json& j) const;

    // wrapper to apply transformations from transform object
    void transform();

    // given a buffer with samples, update the block
    // sample buffer must be interleaved
    void update(vec<double>& sample_buffer, int num_channels, int sample_rate);

private: 
    // vector of audio_pixels for each channel 
    shared_ptr<vec<vec<audio_pixel_t>>> m_channel_pixels {
        std::make_shared<vec<vec<audio_pixel_t>>>()
    }; 

    // pixels per second
    double m_pix_per_s {1.0};
    shared_ptr<audio_pixel_transform_t> m_transform {
        std::make_shared<audio_pixel_transform_t>()
    };
};

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
// thread safe (will block if another thread is updating)
class audio_pixel_mipmap_t {
public:
    audio_pixel_mipmap_t(MediaTrack* track, vec<double> resolutions);

    // returns a copy of the block at the specified resolution
    audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1, 
                                  double pix_per_s);

    // serialization and deserialization
    bool flush(); // flush contents to file
    void to_json(json& j) ;
    void from_json(const json& j); // TODO: implement me 

    // update contents of the mipmap 
    // (if the audio accessor state has changed)
    bool update();
    void fill_map(); // fill map contents
    bool ready() { return m_ready; };

    int num_channels() const { 
        return (int)GetMediaTrackInfo_Value(m_track, "I_NCHAN"); 
    }

    static int sample_rate() {
        GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true);
        return (int)GetSetProjectInfo(0, "PROJECT_SRATE", 0, true); 
    }

private:
    // finds the lower bound of cached resolutions
    double get_nearest_pps(double pix_per_s);

    // create a new interpolated block from a source block
    audio_pixel_block_t create_interpolated_block(double src_pps, double new_pps,
                                                 opt<double> t0, opt<double> t1);
    
    // to get samples from the MediaItem_track
    safe_audio_accessor_t m_accessor; 

    // the lo-res pixel blocks
    // TODO: these shouldn't be doubles
    std::map<double, audio_pixel_block_t, std::greater<double>> m_blocks;

    // sorted list of the blocks pps
    vec<double> m_block_pps; 

    // our parent media track
    MediaTrack* m_track {nullptr};

    std::unique_ptr<ThreadPool> m_pool {
        std::make_unique<ThreadPool>(
            std::clamp(
                std::thread::hardware_concurrency(), 2u, 16u
            )
        )
    };
    std::mutex m_mutex;
    std::atomic<bool> m_ready {false};
    std::atomic<bool> m_abort {false};
};

class audio_pixel_transform_t {
public:
    audio_pixel_transform_t() {};
    
    void normalize(std::shared_ptr<vec<vec<audio_pixel_t>>> block){
        std::vector<double> max_max_field;
        std::vector<double> min_min_field;
        std::vector<double> max_rms_field;

        for (vec<audio_pixel_t>& curr_channel : (*block)) {
            double channel_max_max = std::numeric_limits<double>::lowest();
            double channel_max_min = std::numeric_limits<double>::max();
            double channel_max_rms = std::numeric_limits<double>::lowest();

            for (audio_pixel_t& curr_pixel : curr_channel) {
                channel_max_max = std::max(channel_max_max, curr_pixel.m_max);
                channel_max_min = std::min(channel_max_min, curr_pixel.m_min);
                channel_max_rms = std::max(channel_max_max, curr_pixel.m_rms);
            }

            max_max_field.push_back(channel_max_max);
            min_min_field.push_back(channel_max_min);
            max_rms_field.push_back(channel_max_rms);
        }

        for (int channel_idx = 0; channel_idx < block->size(); channel_idx++) {
            for (audio_pixel_t& curr_pixel : block->at(channel_idx)) {
                curr_pixel.m_max = curr_pixel.m_max/max_max_field.at(channel_idx);
                curr_pixel.m_min = curr_pixel.m_min/min_min_field.at(channel_idx);
                curr_pixel.m_rms = curr_pixel.m_rms/max_rms_field.at(channel_idx);
            } 
        }
    }
};
