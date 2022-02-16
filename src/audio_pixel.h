#define JSON_USE_IMPLICIT_CONVERSIONS 0
#define NOMINMAX

#include <iostream>
#include <iterator>

#include "include/json/json.hpp"
#include "reaper_plugin_functions.h"

using json = nlohmann::json;

class safe_audio_accessor_t {
public:
    // must check that accessor was created with is_valid()
    safe_audio_accessor_t(MediaItem_Take* take) :m_accessor(CreateTrackAudioAcessor(take)) {};
    ~safe_audio_accessor_t() { if(m_accessor) { DestroyTrackAudioAccessor(m_accessor); } };
    bool is_valid() { return m_accessor; };
    AudioAccessor* get() { return m_accessor; };

private:
    AudioAccessor* m_accessor {nullptr};
};

// one sample of audio pixel data, which stores max, min, and rms values
// avoid making these when the pixel-to-sample ratio is 1:1, since we can 
// just store the raw sample value in that case
class audio_pixel_t {
public: 
    audio_pixel_t(double max, double min, double rms) : m_max(max), m_min(min), m_rms(rms) {};
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
    audio_pixel_block_t(double pix_per_s)
        :m_pix_per_s(pix_per_s) {}
    ~audio_pixel_block_t() {}

    void update(std::vector<double>& sample_buffer, int num_channels, int sample_rate) {
        // constructs an audio pixel block using interleaved sample buffer and num channels
        // clear the current set of audio pixels 
        m_channel_pixels.clear();

        // initalize the m_channel_pixels to have empty audio pixel vectors
        for (int i = 0; i < num_channels; i++) {
            std::vector<audio_pixel_t> empty_pixel_vec;
            m_channel_pixels.push_back(empty_pixel_vec);
        }

        //calcualte the samples per audio pixel and initalize an audio pixel
        int samples_per_pixel = m_pix_per_s * sample_rate;
        audio_pixel_t curr_pixel = audio_pixel_t(0, -1, 0);

        for (int channel= 0; channel < num_channels; channel++) {
            int collected_samples = 0;

            for (int i = 0; i < sample_buffer.size(); i += num_channels) { 
                double curr_sample = sample_buffer[i];

                // increment collected samples after creating new audio pixel 
                collected_samples ++;
        
                // update all fields of the current pixeld based on the current sample
                curr_pixel.m_max = std::max(curr_pixel.m_max, curr_sample);
                curr_pixel.m_min = std::min(curr_pixel.m_min, curr_sample);
                curr_pixel.m_rms += (curr_sample * curr_sample);

                if ((collected_samples % samples_per_pixel) == 0) {
                    // complete the RMS calculation, this method accounts of edge cases (total sample % samples per pixel != 0)
                    curr_pixel.m_rms = curr_pixel.m_rms / collected_samples;

                    // add the curr pixel to its channel in m_channel_pixels, clear curr_pixel
                    m_channel_pixels[channel].push_back(curr_pixel);

                    // set max to equal reaper min, and min to equal reaper max
                    curr_pixel = audio_pixel_t(DBL_MIN, DBL_MAX, 0);
                }
            }
        }
    }
    bool flush() const { /*TODO*/ }; // flush contents to file

    const std::vector<std::vector<audio_pixel_t>>& get_pixels() const { return m_channel_pixels; }
    const std::vector<audio_pixel_t>& get_pixels(double t0, double t1) const { /* TODO */}

    // serialization and deserialization
    void to_json(json& j) const;
    void from_json(const json& j);

private: 
    double m_pix_per_s {1.0}; // pixels per second
    std::vector<std::vector<audio_pixel_t>> m_channel_pixels; // maps a channel to its corresponding representation 
    std::string m_guid; // unique id that maps back to a MediaItemTake
                        // use GetSetMediaItemTakeInfo_String to fill it up
};

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
class audio_pixel_mipmap_t {
    audio_pixel_mipmap_t(MediaItem_Take* take)
        :m_accessor(safe_audio_accessor_t(take)),
        m_take(take)
    {
        if (!m_accessor.is_valid()) { std::cerr << "Invalid audio accessor used for audio block." << std::endl; }
    };

    void update() { // update all blocks
        // MUST BE CALLED BY MAIN THREAD
        // due to reaper api calls: AudioAccessorUpdate
        AudioAccessor* safe_accessor = m_accessor.get();

        if (AudioAcessorStateChange(safe_accessor)) {
            // updates the audio acessors contents
            AudioAccessorUpdate(safe_accessor);

            std::vector<double> sample_buffer;
            double accessor_start_time = GetAudioAccessorStartTime(safe_accessor);
            double accessor_end_time = GetAudioAccessorEndTime(safe_accessor);

            // retrive the takes sample rate and num channels from PCM source
            PCM_source* source = GetMediaItemTake_Source(m_take);
            if (!source) { return; } // TODO: LOG ME
            int sample_rate = GetMediaSourceSampleRate(source);
            int num_channels = GetMediaSourceNumChannels(source);

            // calculate the number of samples we want to collect per channel
            // TODO CEILING ROUNDING
            int samples_per_channel = sample_rate * (accessor_end_time - accessor_start_time);

            // samples stored in sample_buffer, operation status is returned
            if (!GetAudioAcessorSamples(safe_accessor, sample_rate, num_channels, accessor_start_time, samples_per_channel, sample_buffer.data())) { 
                return;  
            } // TODO: LOG ME

            // pass samples to update audio pixel blocks
            for (auto& it: m_blocks) {
                it.second.update(sample_buffer, num_channels, sample_rate);
            };
        }
    }
    bool flush() const; // flush all blocks to disk

    // mm, how do we pass these pixels fast enough?
    std::vector<audio_pixel_t> get_pixels(double t0, double t1, double pix_per_s) const;

private:
    MediaItemTake* m_take {nullptr};
    safe_audio_accessor_t m_accessor; // to get samples from the MediaItem_Take
    std::map<int, audio_pixel_block_t> m_blocks;  
};
