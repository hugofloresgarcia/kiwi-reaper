#include "audio_pixel.h"
#define project nullptr

// audio pixel functions 

double linear_interp(double x, double x1, double x2, double y1, double y2) {
    // edge cases 
    if ((x2-x1) == 0)
        return x1;
    // interpolation in time domain
    return (((x2 - x) / (x2 - x1)) * y1) + ((x - x1) / (x2 - x1)) * y2;
}

double closest_val(double val1, double val2, double target) {
    return abs(target - val1) <= abs(target - val2) ? val1 : val2;
}

template<typename T>
const std::vector<T> get_view(std::vector<T> const &parent, int start, int end)
{
    // todo: make sure start and  end indices are valid
    auto startptr = v.cbegin() + start;
    auto endptr = v.cbegin() + end;
 
    std::vector<T> vec(startptr, endptr);
    return vec;
}

// ************* audio_pixel_block_t ****************

bool audio_pixel_block_t::flush() const {
    json j;
    to_json(j);
    std::string resource_path = GetResourcePath();
    std::ofstream ofs;
    ofs.open(resource_path + "/kiwi-block.json");

    if (!ofs.is_open()){
        return false;
    }
    
    ofs << std::setw(4) << j << std::endl;
    return true;
}

void audio_pixel_block_t::to_json(json& j) const {
    j[std::to_string(m_pix_per_s)] = m_channel_pixels;
}

void audio_pixel_block_t::update(std::vector<double>& sample_buffer, int num_channels, int sample_rate) {
    // constructs an audio pixel block using interleaved sample buffer and num channels
    // clear the current set of audio pixels 
    m_channel_pixels.clear();
    int pixels_per_channel = 0;

    // initalize the m_channel_pixels to have empty audio pixel vectors
    for (int i = 0; i < num_channels; i++) {
        std::vector<audio_pixel_t> empty_pixel_vec;
        m_channel_pixels.push_back(empty_pixel_vec);
    }

    //calcualte the samples per audio pixel and initalize an audio pixel
    int samples_per_pixel = sample_rate / m_pix_per_s;
    m_block_size = ceil((sample_buffer.size()/num_channels)/m_pix_per_s);

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

std::vector<std::vector<audio_pixel_t>> audio_pixel_block_t::get_pixels(double t0, double t1) {
    std::vector<std::vector<audio_pixel_t>> output_block;
    int starting_idx =  std::max(0, (int)(t0 * m_pix_per_s));
    int ending_idx = std::min((int)(t1 * m_pix_per_s), m_block_size);

    for (int channel = 0; channel < m_channel_pixels.size(); channel++){
        std::vector<audio_pixel_t> channel_block = m_channel_pixels[channel];
        std::vector<audio_pixel_t> subset_block;
        for (int i = starting_idx; i < ending_idx && i < channel_block.size(); i++) {
            subset_block.push_back(channel_block[i]);
        }
        output_block.push_back(subset_block);
    }

    return output_block;
}

// ************* audio_pixel_mipmap_t ****************

audio_pixel_block_t audio_pixel_mipmap_t::get_pixels(double t0, double t1, int pix_per_s) {
    int nearest_pps = get_nearest_pps(pix_per_s);

    if (nearest_pps == pix_per_s){ // last edge case, when the given pps is already a block (other cases in get nearest pps)
        audio_pixel_block_t matching_block = m_blocks[nearest_pps];
        audio_pixel_block_t output_block(pix_per_s, matching_block.get_pixels(t0, t1));
        return output_block;
    }

    // perform interpolation
    audio_pixel_block_t interpolated_block = create_interpolated_block(nearest_pps, pix_per_s, t0, t1);
    return interpolated_block;
};

audio_pixel_block_t audio_pixel_mipmap_t::get_block(int pix_per_s){
    int nearest_pps = get_nearest_pps(pix_per_s);

    if (nearest_pps == pix_per_s){
        audio_pixel_block_t matching_block = m_blocks[nearest_pps];
        return matching_block;
    }

    // perform interpolation
    audio_pixel_block_t interpolated_block = create_interpolated_block(nearest_pps, pix_per_s, -1, -1);
    return interpolated_block;
}

int  audio_pixel_mipmap_t::get_nearest_pps(int pix_per_s) {
    int n_blocks = m_block_pps.size();

    if (pix_per_s <= m_block_pps[0]) // given pps smaller than our smallest rep
        return m_block_pps[0];
    else if (pix_per_s >= m_block_pps[n_blocks - 1]) // given pps larger than our largest rep
        return m_block_pps[n_blocks - 1];

    return get_nearest_pps_helper(pix_per_s);
}

audio_pixel_block_t audio_pixel_mipmap_t::create_interpolated_block(int src_pps, int new_pps, double t0, double t1) {
    if (t0 == -1)
        t0 = GetAudioAccessorStartTime(m_accessor.get());
    if (t1 == -1)
        t1 = GetAudioAccessorEndTime(m_accessor.get());

    // interpolate from the block with nearest-pps to create a new block at new_pps
    auto map_entry = m_blocks.find(src_pps);
    audio_pixel_block_t nearest_block = map_entry->second;
    const std::vector<std::vector<audio_pixel_t>> nearest_channel_pixels = nearest_block.get_pixels(t0, t1);
    int new_number_pixels = ceil((t1 - t0) * new_pps);

    double new_t_unit = 1.0/new_pps, nearest_t_unit = 1.0/src_pps; // new/nearest block's time unit (the time between pixels)
    double nearest_t0 = 0.0, nearest_t1 = nearest_t_unit; // nearest block's time range and index (position in time of nearest samples)
    audio_pixel_t nearest_pixel0, nearest_pixel1;
    int nearest_pixel_idx0 = 0, nearest_pixel_idx1 = 1; 
    double curr_t = 0.0; // new block's time 

    std::vector<std::vector<audio_pixel_t>> new_block;

    for (auto& nearest_pixels : nearest_channel_pixels) {
        // nearest pixels of the specfic channel 
        audio_pixel_t curr_audio_pixel;
        std::vector<audio_pixel_t> curr_channel_pixels;

        for (int i = 0; i < new_number_pixels; i++) {
            curr_t = i * new_t_unit;
            curr_audio_pixel = audio_pixel_t();
            
            // grab the nearest audio pixels and their times
            nearest_pixel_idx0 = std::min((int)floor(src_pps * curr_t), (int)nearest_pixels.size() - 1);
            nearest_pixel_idx1 = std::min((int)ceil(src_pps * (curr_t + nearest_t_unit)), (int)nearest_pixels.size() - 2);
            nearest_t0 = nearest_pixel_idx0 * nearest_t_unit;
            nearest_t1 = nearest_t0 + nearest_t_unit;
            nearest_pixel0 = nearest_pixels[nearest_pixel_idx0];
            nearest_pixel1 = nearest_pixels[nearest_pixel_idx1];

            // perform liner interpolation for each field of the audio pixel
            curr_audio_pixel.linear_interpolation(curr_t, nearest_t0, nearest_t1, nearest_pixel0, nearest_pixel1);

            curr_channel_pixels.push_back(curr_audio_pixel);
        }
        new_block.push_back(curr_channel_pixels);
    }
    return audio_pixel_block_t(new_pps, new_block);
}

 int audio_pixel_mipmap_t::get_nearest_pps_helper(int pix_per_s) {
    // assumes that we can get m_block_ppps in sorted form after construction of the map
    if (m_block_pps.size() == 0)
        return -1;

    int prev_block_val = m_block_pps[0];

    for (int& block_pps : m_block_pps) {
        if (pix_per_s < block_pps)
            break;
        prev_block_val = block_pps;
    }
    return prev_block_val;
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
    std::vector<double> sample_buffer(samples_per_channel*num_channels);
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
