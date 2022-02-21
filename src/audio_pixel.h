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
        :m_pix_per_s(pix_per_s) {};
    audio_pixel_block_t(double pix_per_s, std::vector<std::vector<audio_pixel_t>> channel_pixels;)
        :m_pix_per_s(pix_per_s), m_channel_pixels(channel_pixels) {};
    ~audio_pixel_block_t() {};

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
    const std::vector<std::vector<audio_pixel_t>>& get_pixels(double t0, double t1) const { /* TODO */}

    const int get_mono_pixel_count(){
        if (m_channel_pixels.size() > 0)
            return m_channel_pixels[0].size();
        return 0;
    }

    const int get_stero_pixel_count() {
        if (m_channel_pixels.size() > 0)
            return m_channel_pixels[0].size() * m_channel_pixels.size();
        return 0;
    }

    const int get_pps() {return m_pix_per_s;}

    const int num_samp

    // serialization and deserialization
    void to_json(json& j) const;
    void from_json(const json& j);

private: 
    double m_pix_per_s {1.0}; // pixels per second
    std::vector<std::vector<audio_pixel_t>> m_channel_pixels; // vector of audio_pixels for each channel 
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
    };

    bool flush() const; // flush all blocks to disk

    // mm, how do we pass these pixels fast enough?
    std::vector<audio_pixel_t> get_pixels(double t0, double t1, double pix_per_s) {
        // start by grabbing pixels from the nearest two pixel blocks
        nearest_pps = get_two_nearest_pps(pix_per_s);

        if (nearest_pps == pix_per_s) // last edge case, when hte given pps is already a block
            return m_blocks[nearest_pps];

        // perform interpolation
        audio_pixel_block_t interpolated_block = interpolate_block(t0, t1, pix_per_s, nearest_pps);
    };

    double get_two_nearest_pps(double pix_per_s) {
        // TODO: make this a vector of doubles, return the two nearest pps
        // returning the nearest to first get bilinear interpolation working
        // 2 edge cases
        if (pix_per_s <= m_block_pps[0]) // given pps smaller than our smallest rep
            return m_block_pps[0];
        else if (pix_per_s >= m_block_pps[n_blocks - 1]) // given pps larger than our largest rep
            return m_block_pps[n_blocks - 1];

        return get_nearest_block_pps(pix_per_s);
    }

    double get_nearest_block_pps(double pix_per_s) {
        // returns the nearest blocks for a given pixels per seconds
        // also notifies caller if the given nearest block is an edge case (first/last element)
        int n_blocks = m_block_pps.size()

        // binary search the list 
        int low = 0, high = n_blocks, mid = 0;

        while (low < high) {
            // update mid
            mid = (low + high)/2;
            mid_val = m_block_pps[mid];

            // edge case, a block with the given pps is already saved
            if (mid_val == pix_per_s)
                mid_val;
            
            // the given pps is less than the current mid
            if (pix_per_s < mid_val) {
                if (mid > 0 && target > m_block_pps[mid - 1])
                    return closet_val(m_block_pps[mid - 1], mid_val, pix_per_s);
                high = mid;
            }

            else {
                if (mid < n - 1 && pix_per_s < m_block_pps[mid + 1])
                    return closet_val(mid_val, m_block_pps[mid + 1], pix_per_s);
                low = mid + 1;
            }
        }

        return m_block_pps[mid];
    }

    double closet_val(val1, val2, target) {
        // val 1 must be greater than val 2
        if (target - val1 >= val2 - target) {
            return val2;
        else 
            return val1
        }
    }

    audio_pixel_block_t interpolate_block(double t0, double t1, double new_pps, double nearest_pps) {
        // interpolate from the block with nearest-pps to create a new block at new_pps
        // the tricky part here is keeping track of the which samples from the nearest block are towards us
        audio_pixel_block_t nearest_block = m_block_pps[nearest_pps];
        std::vector<std::vector<audio_pixel_t>> nearest_channel_pixels = nearest_block.get_pixels(t0, t1);
        int new_number_pixels = (int)round((t1 - t0) * new_pps);

        double new_t_unit = 1/new_pps, nearest_t_unit = 1/nearest_pps; // new/nearest block's time unit (the time between pixels)
        double nearest_t1 = 0, nearest_t2 = nearest_t_unit, nearest_t_idx1 = 0, nearest_t_idx2 = 1; // nearest block's time range and index (position in time of nearest samples)
        double curr_new_t = 0; // new block's time 

        for (auto& nearest_pixels : nearest_channel_pixels) {
            // nearest pixels of the specfic channel 
            for (int i = 0; i < new_number_pixels; i++) {
                curr_new_t = i * new_t_unit;

                if (closet_val(nearest_t2, nearest_t2 + nearest_t_unit, curr_new_t)) {
                    nearest_t1 += nearest_t_unit;
                    nearest_t2 += nearest_t_unit;
                    nearest_t_idx1++;
                    nearest_t_idx2++;
                }
            }
        }
    }

private:
    MediaItemTake* m_take {nullptr};
    safe_audio_accessor_t m_accessor; // to get samples from the MediaItem_Take
    std::map<double, audio_pixel_block_t, cmp_blocks_pps> m_blocks;  
    std::vector<double> m_block_pps; // sorted list of the blocks pps, TODO fill this up after construction
};

struct cmp_blocks_pps {
    bool operator()(const double& a, const double& b) {
        return a < b;
    }
}