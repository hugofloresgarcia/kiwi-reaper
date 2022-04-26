#pragma once

#include "pixel.h"
#include "log.h"
#include <vector> 

template<typename T> 
using vec = std::vector<T>;

template<typename T> 
using opt = std::optional<T>; 

using std::shared_ptr;

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
    const audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1) const {
        debug("audio pixel block: getting pixels");

        int block_size = get_num_pix_per_channel();
        debug("block size is {}", block_size);

        // set start idx
        int start_idx = std::clamp(
            (int)floor(t0.value_or(0.0) * m_pix_per_s), // value or default
            0,  // lo
            block_size -1 // hi
        );

        // set end idx
        int end_idx = std::clamp(
            (int)ceil(t1.value_or((block_size-1) / m_pix_per_s) * m_pix_per_s), // value or default (end of block)
            start_idx,  // lo
            block_size -1 // hi
        );

        debug("retrieving {} pixels from {} to {}", end_idx - start_idx, start_idx, end_idx);
        debug("current resolution is {} pixels per second", m_pix_per_s);

        const audio_pixel_block_t output_block(m_pix_per_s);

        for (int channel = 0; channel < m_channel_pixels->size(); channel++){
            output_block.m_channel_pixels->push_back(
                get_view(m_channel_pixels->at(channel), start_idx, end_idx)
            );
        }

        return output_block;
    }


    // creates a new block at a new resolution, via linear interpolation
    audio_pixel_block_t interpolate(double new_pps) const{
        debug("creating interpolated audio pixel block with resolution {}", new_pps);

        // TODO: handle optionals here
        int new_num_pix = ceil(((double)(get_num_pix_per_channel()) / m_pix_per_s) * new_pps);

        // our block's time unit (the time between pixels)
        double m_t_unit = 1.0 / m_pix_per_s; 

        // our own block's time range and index (position in time of nearest samples)
        double n_t0 =  0.0;
        double n_t1 = m_t_unit; 

        // new block's time 
        double curr_t = 0.0; 

        // our output block
        audio_pixel_block_t new_block(new_pps);

        // iterate through our pixels to create new ones
        for (auto& pix_channel : *m_channel_pixels) {
            // nearest pixels of the specfic channel 
            vec<audio_pixel_t> curr_pix_channel;

            for (int i = 0; i < new_num_pix; i++) {
                // the current point in time we're trying to interpolate for
                curr_t = i / new_pps;
                
                // grab the nearest audio pixels and their times
                int idx0 = std::min((int) floor(m_pix_per_s * curr_t), 
                                    (int) pix_channel.size() - 1);
                double nearest_t0 = idx0 * m_t_unit;

                int idx1 = std::min((int) ceil(m_pix_per_s * (curr_t + m_t_unit)),
                                    (int) pix_channel.size() - 2);
                // TODO: should this be nearest_t0 + m_t_unit; or idx1 * m_t_unit;??
                double nearest_t1 = nearest_t0 + m_t_unit;

                audio_pixel_t pix0 = pix_channel[idx0];
                audio_pixel_t pix1 = pix_channel[idx1];

                // perform linear interpolation for each field of the audio pixel
                audio_pixel_t curr_audio_pixel = audio_pixel_t::linear_interpolation(
                                                    curr_t, nearest_t0, nearest_t1, 
                                                    pix0, pix1);

                curr_pix_channel.push_back(curr_audio_pixel);
            }
            new_block.m_channel_pixels->push_back(curr_pix_channel);
        }
        return new_block;
    }

    // gets the resolution (in pixels per second)
    double get_pps() const { return m_pix_per_s; };
    
    // number of pixels in a block (for a single channel)
    int get_num_pix_per_channel() const { 
        return m_channel_pixels->empty() ? 0 : m_channel_pixels->front().size(); 
    }

    // fill a json object with a block
    void to_json(json& j) const {
        j = *m_channel_pixels;
    }

    // wrapper to apply transformations from transform object
    void transform() {
        m_transform->normalize(m_channel_pixels);
    }


    // given a buffer with samples, update the block
    // sample buffer must be interleaved
    void update(vec<double>& sample_buffer, int num_channels, int sample_rate){
        debug("updating audio pixel block with pps {}", m_pix_per_s);

        //calcualte the samples per audio pixel and initalize an audio pixel
        int samples_per_pixel = sample_rate / m_pix_per_s;
        int num_samples_per_channel = sample_buffer.size() / num_channels;
        int pixels_per_channel = ceil((float)num_samples_per_channel / (float)samples_per_pixel) + 1;

        // reallocate if we need to
        m_channel_pixels->resize(num_channels);
        for (auto& chan: *m_channel_pixels) {
            chan.resize(pixels_per_channel);
        }

        
        for (int channel = 0; channel < num_channels; channel++) {
            std::fill(m_channel_pixels->at(channel).begin(), 
                    m_channel_pixels->at(channel).end(), 
                    audio_pixel_t());     
            // debug("processing channel {}", channel);
            int pixel_idx = 0;
            for (int i = 0; i < num_samples_per_channel; i++) {
                // debug("processing sample {} ", i);
                double curr_sample = sample_buffer.at(i*num_channels + channel);

                if (pixel_idx >= (*m_channel_pixels).at(channel).size()) {
                    info("a fatal error occurred. pixel_idx {} is out of bounds", pixel_idx);
                    assert(false);
                }
                audio_pixel_t& curr_pixel = (*m_channel_pixels).at(channel).at(pixel_idx); 
        
                // update all fields of the current pixeld based on the current sample
                curr_pixel.m_max = std::max(curr_pixel.m_max, curr_sample);
                curr_pixel.m_min = std::min(curr_pixel.m_min, curr_sample);
                curr_pixel.m_rms += (curr_sample * curr_sample);

                int collected_samples = i % samples_per_pixel;
                if ((collected_samples == 0) || i == sample_buffer.size() - 1) {
                    // debug("pixel {} collected", pixel_idx);
                    // complete the RMS calculation, this method accounts 
                    // of edge cases (total sample % samples per pixel != 0)
                    curr_pixel.m_rms = sqrt(curr_pixel.m_rms / 
                                            (samples_per_pixel - collected_samples));
                    pixel_idx++;
                }
            }
        }
        debug("DONE updating audio pixel block with pps {}", m_pix_per_s);
    }

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