#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include "include/json/json.hpp"

#include "reaper_plugin_functions.h"

using json = nlohmann::json;

class safe_audio_accessor_t {
public:
    // must check that accessor was created with is_valid()
    safe_audio_accessor_t(MediaItem_Take* take) :m_accessor(CreateTrackAudioAcessor(take)) {};
    ~safe_audio_accessor_t() { if(m_accessor) { DestroyTrackAudioAccessor(m_accessor); } };
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

private:
    double m_max;
    double m_min;
    double m_rms;

public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(audio_pixel_t, m_max, m_min, m_rms);
};

// stores one block of mipmapped audio data, at a particular sample rate
// should be able to update when the samples are updated
class audio_pixel_block_t {

public: 
    // TODO: handle the accessor not being valid
    audio_pixel_block_t(MediaItem_Take* take, double pix_per_s) 
        : m_take(take), m_pix_per_s(pix_per_s), 
          m_accessor(safe_audio_accessor_t(take)) 
          { /* TODO: */}
    ~audio_pixel_block_t() {}

    void update() { /* TODO: see AudioAccessorStateChanged */}
    bool flush() const { /*TODO*/ }; // flush contents to file

    const std::vector<audio_pixel_t>& get_pixels() const { return m_pixels; }
    const std::vector<audio_pixel_t>& get_pixels(double t0, double t1) const { /* TODO */}

    // serialization and deserialization
    void to_json(json& j) const;
    void from_json(const json& j);

private: 
    MediaItem_Take *m_take {nullptr}; // parent take
    double m_pix_per_s {1.0}; // pixels per second
    std::vector<audio_pixel_t> m_pixels;  // stores pixel data
    safe_audio_accessor_t m_accessor; // to get samples from the MediaItem_Take
    std::string m_guid; // unique id that maps back to a MediaItemTake
                        // use GetSetMediaItemTakeInfo_String to fill it up
};

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
class audio_pixel_mipmap_t {
    audio_pixel_mipmap_t(MediaItem_Take* take);

    void update(); // update all blocks
    bool flush() const; // flush all blocks to disk

    // mm, how do we pass these pixels fast enough?
    std::vector<audio_pixel_t> get_pixels(double t0, double t1, double pix_per_s) const;

private:
    MediaItemTake* m_take {nullptr};
    std::map<int, audio_pixel_block_t> m_blocks;
};