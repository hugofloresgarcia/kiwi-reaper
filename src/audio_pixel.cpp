#include "audio_pixel.h"
#define project nullptr

#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <math.h>
#include <memory>
#include <string>
#include <algorithm>

// audio pixel functions 

double linear_interp(double x, double x1, double x2, double y1, double y2) {
    // edge cases 
    if ((x2-x1) == 0)
        return x1;
    // interpolation in time domain
    return (((x2 - x) / (x2 - x1)) * y1) + ((x - x1) / (x2 - x1)) * y2;
}

template<typename T>
const vec<T> get_view(vec<T> const &parent, int start, int end)
{
    // todo: make sure start and  end indices are valid
    auto startptr = parent.cbegin() + start;
    auto endptr = parent.cbegin() + end;
 
    vec<T> vec(startptr, endptr);
    return vec;
}

// ************* audio_pixel_block_t ****************

void audio_pixel_block_t::to_json(json& j) const {
    j = m_channel_pixels;
}

void audio_pixel_block_t::update(vec<double>& sample_buffer, int num_channels, int sample_rate) {
    // constructs an audio pixel block using interleaved sample buffer and num channels
    // clear the current set of audio pixels 
    m_channel_pixels.clear();
    int pixels_per_channel = 0;

    // initalize the m_channel_pixels to have empty audio pixel vectors
    for (int i = 0; i < num_channels; i++) {
        vec<audio_pixel_t> empty_pixel_vec;
        m_channel_pixels.push_back(empty_pixel_vec);
    }

    //calcualte the samples per audio pixel and initalize an audio pixel
    int samples_per_pixel = sample_rate / m_pix_per_s;

    for (int channel= 0; channel < num_channels; channel++) {
        audio_pixel_t curr_pixel = audio_pixel_t();
        pixels_per_channel = 0;

        for (int i = 0; i < sample_buffer.size(); i += num_channels) { 
            double curr_sample = sample_buffer[i];
    
            // update all fields of the current pixeld based on the current sample
            curr_pixel.m_max = std::max(curr_pixel.m_max, curr_sample);
            curr_pixel.m_min = std::min(curr_pixel.m_min, curr_sample);
            curr_pixel.m_rms += (curr_sample * curr_sample);

            if (((i % samples_per_pixel) == 0) || i == sample_buffer.size() - 1) {
                // complete the RMS calculation, this method accounts of edge cases (total sample % samples per pixel != 0)
                curr_pixel.m_rms = sqrt(curr_pixel.m_rms / (samples_per_pixel - (i % samples_per_pixel)));

                // add the curr pixel to its channel in m_channel_pixels, clear curr_pixel
                m_channel_pixels[channel].push_back(curr_pixel);

                // set max to equal reaper min, and min to equal reaper max
                curr_pixel = audio_pixel_t();
                pixels_per_channel++;
            }
        }
    }
}

audio_pixel_block_t audio_pixel_block_t::get_pixels(opt<double> t0, opt<double> t1) {
    int block_size = get_num_pix_per_channel();

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

    audio_pixel_block_t output_block(m_pix_per_s);

    for (int channel = 0; channel < m_channel_pixels.size(); channel++){
        output_block.m_channel_pixels.push_back(
            get_view(m_channel_pixels[channel], start_idx, end_idx)
        );
    }

    return output_block;
}

audio_pixel_block_t audio_pixel_block_t::interpolate(double new_pps, opt<double> t0, opt<double> t1) {
    // TODO: handle optionals here
    int new_num_pix = ceil(((*t1) - (*t0)) * new_pps);

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
    for (auto& pix_channel : m_channel_pixels) {
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
        new_block.m_channel_pixels.push_back(curr_pix_channel);
    }
    return new_block;
}

// returns the number of pixels per each channel
int audio_pixel_block_t::get_num_pix_per_channel() { 
    return m_channel_pixels.empty() ? 0 : m_channel_pixels.front().size(); 
}

// ************* audio_pixel_mipmap_t ****************

audio_pixel_block_t audio_pixel_mipmap_t::get_pixels(opt<double> t0, opt<double> t1, int pix_per_s) {
    int nearest_pps = get_nearest_pps(pix_per_s);
    //std::unique_ptr<audio_pixel_block_t> nearest_block = std::make_unique<audio_pixel_block_t>(map_entry->second);
    if (nearest_pps == pix_per_s){ // last edge case, when the given pps is already a block (other cases in get nearest pps)
        std::unique_ptr<audio_pixel_block_t> matching_block = std::make_unique<audio_pixel_block_t>(m_blocks[nearest_pps]);
        return matching_block->get_pixels(t0, t1);
    }

    // perform interpolation
    audio_pixel_block_t interpolated_block = create_interpolated_block(nearest_pps, pix_per_s, t0, t1);
    return interpolated_block.get_pixels(t0, t1);
};

int audio_pixel_mipmap_t::get_nearest_pps(int pix_per_s) {
    auto const it = std::lower_bound(m_block_pps.begin(), m_block_pps.end(), pix_per_s);
    if (it == m_block_pps.end())
        return m_block_pps.back();
    
    return *it;
}

audio_pixel_block_t audio_pixel_mipmap_t::create_interpolated_block(int src_pps, int new_pps, opt<double> t0 = 0, opt<double> t1 = -1) {
    // interpolate from the block with nearest-pps to create a new block at new_pps
    auto map_entry = m_blocks.find(src_pps);
    std::unique_ptr<audio_pixel_block_t> nearest_block = std::make_unique<audio_pixel_block_t>(map_entry->second);
    if (t1 == -1)
        t1 = nearest_block->get_num_pix_per_channel() / nearest_block->get_pps();
    
    // FIXME: this is copying
    audio_pixel_block_t nearest_channel_pixels = nearest_block->get_pixels(t0, t1);

    return nearest_block->interpolate(new_pps, t0, t1);
}



bool audio_pixel_mipmap_t::flush() const {
    json j;
    to_json(j);
    std::string resource_path = GetResourcePath();
    std::ofstream ofs;
    ofs.open(resource_path + "/kiwi-mipmap.json");

    if (!ofs.is_open()){
        return false;
    }
    
    ofs << std::setw(4) << j << std::endl;
    return true;
}

void audio_pixel_mipmap_t::to_json(json& j) const {
    for (auto& map_entry : m_blocks) {
        j[std::to_string(map_entry.first)] = map_entry.second.get_pixels();
    }
}

void audio_pixel_mipmap_t::update() {
    // update all blocks
    // MUST BE CALLED BY MAIN THREAD
    // due to reaper api calls: AudioAccessorUpdate
    fill_blocks();
};

void audio_pixel_mipmap_t::fill_blocks() {
    // we do need to check the last updated time somehow
    AudioAccessor* safe_accessor = m_accessor.get();

    double accessor_start_time = GetAudioAccessorStartTime(safe_accessor);
    double accessor_end_time = GetAudioAccessorEndTime(safe_accessor);

    MediaItem* item = GetSelectedMediaItem(project, 0);
    if (!item) {return;} // TODO: LOG ME
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) {return;} // TODO: LOG ME

    // retrive the takes sample rate and num channels from PCM source
    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) { return; } // TODO: LOG ME

    GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true);
    int sample_rate = GetSetProjectInfo(0, "PROJECT_SRATE", 0, true);
    int num_channels = (int)GetMediaTrackInfo_Value(m_track, "I_NCHAN");

    // calculate the number of samples we want to collect per channel
    int samples_per_channel = sample_rate * (accessor_end_time - accessor_start_time);
    vec<double> sample_buffer(samples_per_channel*num_channels);
    int sample_status = GetAudioAccessorSamples(safe_accessor, sample_rate, num_channels, accessor_start_time, samples_per_channel, sample_buffer.data());

    // samples stored in sample_buffer, operation status is returned
    if (!sample_status) {
        return;
    } // TODO: LOG ME

    // pass samples to update audio pixel blocks
    for (auto& it : m_blocks) {
        std::cout << std::to_string(it.first) << std::endl;
        it.second.update(sample_buffer, num_channels, sample_rate);
    };

}
