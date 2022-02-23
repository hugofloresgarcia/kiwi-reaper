#include "audio_pixel.h"
#define project nullptr

// audio pixel functions 

double linear_interp(double x, double x1, double x2, double y1, double y2) {
    // interpolation in time domain
    return (((x2 - x) / (x2 - x1)) * y1) + ((x - x1) / (x2 - x1)) * y2;
}

double closest_val(double val1, double val2, double target) {
    return abs(target - val1) <= abs(target - val2) ? val1 : val2;
}

// ************* audio_pixel_block_t ****************

void audio_pixel_block_t::update(std::vector<double>& sample_buffer, int num_channels, int sample_rate) {
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

// ************* audio_pixel_mipmap_t ****************

const std::vector<std::vector<audio_pixel_t>>& audio_pixel_mipmap_t::get_pixels(double t0, double t1, double pix_per_s) {
    // start by grabbing pixels from the nearest two pixel blocks TODO: this will be a vector<double>
    double nearest_pps = get_nearest_pps(pix_per_s);

    if (nearest_pps == pix_per_s){ // last edge case, when hte given pps is already a block
        audio_pixel_block_t matching_block = m_blocks[nearest_pps];
        return matching_block.get_pixels(t0, t1);
    }

    // perform interpolation
    audio_pixel_block_t interpolated_block = create_interpolated_block(t0, t1, pix_per_s, nearest_pps);
    return interpolated_block.get_pixels(t0, t1);
};

double audio_pixel_mipmap_t::get_nearest_pps(double pix_per_s) {
    // TODO: make this a vector of doubles, return the two nearest pps
    // returning the nearest to first get bilinear interpolation working
    // 2 edge cases
    int n_blocks = m_block_pps.size();

    if (pix_per_s <= m_block_pps[0]) // given pps smaller than our smallest rep
        return m_block_pps[0];
    else if (pix_per_s >= m_block_pps[n_blocks - 1]) // given pps larger than our largest rep
        return m_block_pps[n_blocks - 1];
    // normal case
    return get_nearest_pps_helper(pix_per_s);
}

audio_pixel_block_t audio_pixel_mipmap_t::create_interpolated_block(double src_pps, double new_pps, double t0, double t1) {
    // interpolate from the block with nearest-pps to create a new block at new_pps
    // the tricky part here is keeping track of the which samples from the nearest block are towards us
    audio_pixel_block_t nearest_block = m_block_pps[src_pps];
    const std::vector<std::vector<audio_pixel_t>> nearest_channel_pixels = nearest_block.get_pixels(t0, t1);
    int new_number_pixels = (int)round((t1 - t0) * new_pps);

    double new_t_unit = 1/new_pps, nearest_t_unit = 1/src_pps; // new/nearest block's time unit (the time between pixels)
    double nearest_t0 = 0, nearest_t1 = nearest_t_unit; // nearest block's time range and index (position in time of nearest samples)
    audio_pixel_t nearest_pixel0, nearest_pixel1;
    int nearest_pixel_idx0 = 0, nearest_pixel_idx1 = 1; 
    double curr_t = 0; // new block's time 

    std::vector<std::vector<audio_pixel_t>> new_block;

    for (auto& nearest_pixels : nearest_channel_pixels) {
        // nearest pixels of the specfic channel 
        audio_pixel_t curr_audio_pixel;
        std::vector<audio_pixel_t> curr_channel_pixels;

        for (int i = 0; i < new_number_pixels; i++) {
            curr_t = i * new_t_unit;
            curr_audio_pixel = audio_pixel_t();
            
            // grab the nearest audio pixels and their times
            nearest_pixel_idx0 = floor(src_pps * curr_t);
            nearest_pixel_idx1 = ceil(src_pps * curr_t);
            nearest_t0 = nearest_pixel_idx0 * nearest_t_unit;
            nearest_t1 = nearest_t0 + nearest_t_unit;
            nearest_pixel0 = nearest_pixels[nearest_pixel_idx0];
            nearest_pixel1 = nearest_pixels[nearest_pixel_idx0];

            // perform liner interpolation for each field of the audio pixel
            curr_audio_pixel.linear_interpolation(curr_t, nearest_t0, nearest_t1, nearest_pixel0, nearest_pixel1);

            curr_channel_pixels.push_back(curr_audio_pixel);
        }
        new_block.push_back(curr_channel_pixels);
    }
    return audio_pixel_block_t(new_pps, new_block);
}

 double audio_pixel_mipmap_t::get_nearest_pps_helper(double pix_per_s) {
    // returns the nearest blocks for a given pixels per seconds using a binary search
    // assumes that we can get m_block_ppps in sorted form after construction of the map
    if (m_block_pps.size() == 0)
        return -1;

    double prev_block_val = m_block_pps[0];

    for (double& block_pps : m_block_pps) {
        if (pix_per_s < block_pps)
            break;
        prev_block_val = block_pps;
    }
    return prev_block_val;
}

void audio_pixel_mipmap_t::update() {
    // update all blocks
    // MUST BE CALLED BY MAIN THREAD
    // due to reaper api calls: AudioAccessorUpdate
    fill_blocks();
};

void audio_pixel_mipmap_t::fill_blocks() {
    AudioAccessor* safe_accessor = m_accessor.get();

    if (AudioAccessorStateChanged(safe_accessor)) {
        // updates the audio acessors contents
        AudioAccessorUpdate(safe_accessor);

        std::vector<double> sample_buffer;
        double accessor_start_time = GetAudioAccessorStartTime(safe_accessor);
        double accessor_end_time = GetAudioAccessorEndTime(safe_accessor);

        MediaItem* item = GetSelectedMediaItem(project, 0);
        if (!item) {return;} // TODO: LOG ME
        MediaItem_Take* take = GetActiveTake(item);
        if (!take) {return;} // TODO: LOG ME

        // retrive the takes sample rate and num channels from PCM source
        PCM_source* source = GetMediaItemTake_Source(take);
        if (!source) { return; } // TODO: LOG ME
        int sample_rate = GetMediaSourceSampleRate(source);
        int num_channels = GetMediaSourceNumChannels(source);

        // calculate the number of samples we want to collect per channel
        // TODO CEILING ROUNDING
        int samples_per_channel = sample_rate * (accessor_end_time - accessor_start_time);

        // samples stored in sample_buffer, operation status is returned
        if (!GetAudioAccessorSamples(safe_accessor, sample_rate, num_channels, accessor_start_time, samples_per_channel, sample_buffer.data())) {
            return;
        } // TODO: LOG ME

        // pass samples to update audio pixel blocks
        for (auto& it : m_blocks) {
            it.second.update(sample_buffer, num_channels, sample_rate);
        };
    }
}

