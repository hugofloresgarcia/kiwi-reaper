#define JSON_USE_IMPLICIT_CONVERSIONS 0

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include "include/json/json.hpp"
#include "reaper_plugin_functions.h"

#include <vector> 

using json = nlohmann::json;

template<typename T> 
using vec = std::vector<T>;

template<typename T> 
using opt = std::optional<T>; 

// forward declarations
double linear_interp(double x, double x1, double x2, double y1, double y2);

// wraps an audio accessor
class safe_audio_accessor_t {
public:
    safe_audio_accessor_t(MediaTrack* track) : m_accessor(CreateTrackAudioAccessor(track)){};
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
class audio_pixel_block_t {
    
public: 
    audio_pixel_block_t() {};
    audio_pixel_block_t(double pix_per_s)
        :m_pix_per_s(pix_per_s) {};
    ~audio_pixel_block_t() {};

    // returns the entire block of audio pixels
    const vec<vec<audio_pixel_t>>& get_pixels() const { return m_channel_pixels; };

    // returns a a block of pixels for the specified time range
    // NOTE: this produces a copy of the underlying data.
    // TODO: make me not produce a copy? 
    audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1);

    // creates a new block at a new resolution, via linear interpolation
    audio_pixel_block_t interpolate(double new_pps, opt<double> t0, opt<double> t1);

    // gets the resolution (in pixels per second)
    double get_pps() { return m_pix_per_s; };
    
    // number of pixels in a block (for a single channel)
    int get_num_pix_per_channel();

    // fill a json object with a block
    void to_json(json& j) const;

    // given a buffer with samples, update the block
    // sample buffer must be interleaved
    void update(vec<double>& sample_buffer, int num_channels, int sample_rate);

private: 
    // vector of audio_pixels for each channel 
    vec<vec<audio_pixel_t>> m_channel_pixels; 

    // pixels per second
    double m_pix_per_s {1.0};
};

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
class audio_pixel_mipmap_t {
public:
    audio_pixel_mipmap_t(MediaTrack* track, vec<int> resolutions)
        :m_accessor(safe_audio_accessor_t(track)),
        m_track(track)
    {
        if (!m_accessor.is_valid()) { return; }
        for (double res : resolutions) {
            m_blocks[res] = audio_pixel_block_t(res);
        }
        m_block_pps = resolutions;
        std::sort(m_block_pps.begin(), m_block_pps.end());
        fill_blocks();
    };

    // mm, how do we pass these pixels fast enough? lower resolutions will work well for fast inromation exchange 
    audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1, int pix_per_s);
    
    // serialization and deserialization
    bool flush() const; // flush contents to file
    void to_json(json& j) const;
    void from_json(const json& j);

    void update();

private:
    int closest_val(double val1, double val2, double target);

    // fills all cached blocks with audio pixel data
    void fill_blocks();

    // finds the lower bound of cached resolutions
    int get_nearest_pps(int pix_per_s);

    // create a new interpolated block from a source block
    audio_pixel_block_t create_interpolated_block(int src_pps, int new_pps, opt<double> t0, opt<double> t1);
    
    // to get samples from the MediaItem_track
    safe_audio_accessor_t m_accessor; 
    std::map<int, audio_pixel_block_t, std::greater<int>> m_blocks;  
    vec<int> m_block_pps; // sorted list of the blocks pps
    MediaTrack* m_track {nullptr};
};
