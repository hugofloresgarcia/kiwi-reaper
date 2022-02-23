#define JSON_USE_IMPLICIT_CONVERSIONS 0

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include <iostream>
#include <iterator>
#include <math.h>

#include "include/json/json.hpp"

#include "reaper_plugin_functions.h"
#include "reaper_plugin.h"


using json = nlohmann::json;

class safe_audio_accessor_t {
public:
    // must check that accessor was created with is_valid()
    safe_audio_accessor_t(MediaTrack* take) : m_accessor(CreateTrackAudioAccessor(take)) {};
    ~safe_audio_accessor_t() { if(m_accessor) { DestroyAudioAccessor(m_accessor); } };

    AudioAccessor* get() { return m_accessor; };
    bool is_valid() { return m_accessor; };
    
private:
    AudioAccessor* m_accessor {nullptr};
};

// one sample of audio pixel data, which stores max, min, and rms values
// avoid making these when the pixel-to-sample ratio is 1:1, since we can 
// just store the raw sample value in that case
class audio_pixel_t {
public: 
    audio_pixel_t(double max, double min, double rms) : m_max(max), m_min(min), m_rms(rms) {};
    audio_pixel_t() {};
    double m_max{ DBL_MIN };
    double m_min { DBL_MAX };
    double m_rms {0};

public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(audio_pixel_t, m_max, m_min, m_rms);
};

// stores one block of mipmapped audio data, at a particular sample rate
// should be able to update when the samples are updated
class audio_pixel_block_t {

public: 
    audio_pixel_block_t() {};
    audio_pixel_block_t(double pix_per_s)
        :m_pix_per_s(pix_per_s) {};
    audio_pixel_block_t(double pix_per_s, std::vector<std::vector<audio_pixel_t>> channel_pixels)
        :m_pix_per_s(pix_per_s), m_channel_pixels(channel_pixels) {};
    ~audio_pixel_block_t() {};

    bool flush() const { /*TODO*/ }; // flush contents to file
    // serialization and deserialization
    void to_json(json& j) const;
    void from_json(const json& j);

    const int get_mono_pixel_count() {
        if (m_channel_pixels.size() > 0)
            return m_channel_pixels[0].size();
        return 0;
    }
    const int get_stero_pixel_count() {
        if (m_channel_pixels.size() > 0)
            return m_channel_pixels[0].size() * m_channel_pixels.size();
        return 0;
    }
    const std::vector<std::vector<audio_pixel_t>>& get_pixels() const { return m_channel_pixels; }
    const std::vector<std::vector<audio_pixel_t>>& get_pixels(double t0, double t1) const { return m_channel_pixels; } //TODO RETURN FOR REGIONS
    double get_pps() { return m_pix_per_s; }


    void update(std::vector<double>& sample_buffer, int num_channels, int sample_rate);



private: 
    std::vector<std::vector<audio_pixel_t>> m_channel_pixels; // vector of audio_pixels for each channel 
    std::string m_guid; // unique-id mapping back to a MediaItemTake, fill w/ GetSetMediaItemTakeInfo_String 
    double m_pix_per_s {1.0}; // pixels per second

};

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
class audio_pixel_mipmap_t {
    audio_pixel_mipmap_t(MediaTrack* track)
        :m_accessor(safe_audio_accessor_t(track)),
        m_track(track)
    {
        if (!m_accessor.is_valid()) { std::cerr << "Invalid audio accessor used for audio block." << std::endl; }
    };

    bool flush() const; // flush all blocks to disk

    // mm, how do we pass these pixels fast enough?
    audio_pixel_block_t get_pixels(double t0, double t1, double pix_per_s);

    void update();

private:
    double closet_val(double val1, double val2, double target);
    double get_nearest_block_pps(double pix_per_s);
    double get_nearest_pps(double pix_per_s);
    audio_pixel_block_t interpolate_block(double t0, double t1, double new_pps, double nearest_pps);
    double linear_interp(double x, double x1, double x2, double y1, double y2);
    
    MediaTrack* m_track {nullptr};
    safe_audio_accessor_t m_accessor; // to get samples from the MediaItem_track
    std::map<double, audio_pixel_block_t> m_blocks;  
    std::vector<double> m_block_pps; // sorted list of the blocks pps, TODO fill this up after construction
};

struct cmp_blocks_pps {
    bool operator()(const double& a, const double& b) {
        return a < b;
    }
};
