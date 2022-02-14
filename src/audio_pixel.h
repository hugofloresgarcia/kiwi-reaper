#define JSON_USE_IMPLICIT_CONVERSIONS 0

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
    audio_pixel_block_t(double pix_per_s)
        :m_pix_per_s(pix_per_s) {}
    ~audio_pixel_block_t() {}

    void update(std::vector<double>& sample_buffer, int num_channels, int sample_rate) {
        // constructs an audio pixel block using interleaved sample buffer and num channels
        // clear the current set of audio pixels 
        m_pixels.clear();
        
        // vectors to store each channels audio pixel data
        std::vector<double> channel_max(num_channels);
        std::vector<double> channel_min(num_channels);
        std::vector<double> channel_rms(num_channels);

        // counter to help parse interleaved labels
        int curr_channel = 0;

        //calcualte the samples per audio pixel
        int samples_per_pixel = (m_pix_per_s * sample_rate) * num_channels;
        int current_sample_count = 1;

        for (int i = 0; i < sample_buffer.size(); i++) {
            double curr_sample = sample_buffer[i];

            // update each channels audio pixel data information
            if (channel_max[curr_channel] < curr_sample)
                channel_max[curr_channel] = curr_sample;
            if (channel_min[curr_channel] > curr_sample)
                channel_min[curr_channel] = curr_sample;
            channel_rms[curr_channel] += curr_sample * curr_sample;

            curr_channel = (curr_channel + 1) % num_channels;
            if ((current_sample_count % samples_per_pixel) == 0) {
                // create audio pixels for each channel
                for (int i = 0; i < num_channels; i++) {
                    m_pixels.push_back(audio_pixel_t(channel_max[i], channel_min[i], channel_rms[i] / sample_rate));
                }
                // zero out the vectors used to collect information about audio pixels for channels
                std::fill(channel_max.begin(), channel_max.end(), 0);
                std::fill(channel_min.begin(), channel_min.end(), 0);
                std::fill(channel_rms.begin(), channel_rms.end(), 0);
            }
        }
    }
    bool flush() const { /*TODO*/ }; // flush contents to file

    const std::vector<audio_pixel_t>& get_pixels() const { return m_pixels; }
    const std::vector<audio_pixel_t>& get_pixels(double t0, double t1) const { /* TODO */}

    // serialization and deserialization
    void to_json(json& j) const;
    void from_json(const json& j);

private:
    double m_pix_per_s {1.0}; // pixels per second
    std::vector<audio_pixel_t> m_pixels;  // stores pixel data
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
    {if (!m_accessor.is_valid()) { std::cerr << "Invalid audio accessor used for audio block." << std::endl; }};

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
            int get_samples_status = GetAudioAcessorSamples(safe_accessor, sample_rate, num_channels, 
                accessor_start_time, samples_per_channel, sample_buffer.data());
            if (get_samples_status != 1) { return; } // TODO: LOG ME

            // pass samples to update audio pixel blocks
            std::map<int, audio_pixel_block_t>::iterator it;
            for (it = m_blocks.begin(); it != m_blocks.end(); it++) {
                it->second.update(sample_buffer, num_channels, sample_rate);
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