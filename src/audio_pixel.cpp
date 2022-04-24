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

int time_to_pixel_idx(double time, double pix_per_s) {
    return floor((int)(time * pix_per_s));
}

double pixel_idx_to_time(int pixel_idx, double pix_per_s) {
    return (double)pixel_idx / pix_per_s;
}

int pps_to_samples_per_pix(double pix_per_s, int sample_rate) {
    return ceil((double)sample_rate / pix_per_s);
}

double samples_per_pix_to_pps(int samples_per_pix, int sample_rate) {
    return sample_rate / samples_per_pix;
}

double linear_interp(double x, double x1, double x2, double y1, double y2) {
    // edge cases 
    if ((x2-x1) == 0)
        return x1;
    // interpolation in time domain
    return (((x2 - x) / (x2 - x1)) * y1) + ((x - x1) / (x2 - x1)) * y2;
}

// ************* audio_pixel_block_t ****************

void audio_pixel_block_t::to_json(json& j) const {
    j = *m_channel_pixels;
}

void audio_pixel_block_t::update(vec<double>& sample_buffer, int num_channels, 
                                 int sample_rate) {
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

const audio_pixel_block_t audio_pixel_block_t::get_pixels(opt<double> t0, opt<double> t1) const {
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

audio_pixel_block_t audio_pixel_block_t::interpolate(double new_pps) const {
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

// returns the number of pixels per each channel
int audio_pixel_block_t::get_num_pix_per_channel() const { 
    return m_channel_pixels->empty() ? 0 : m_channel_pixels->front().size(); 
}

void audio_pixel_block_t::transform() {
    m_transform->normalize(m_channel_pixels);
}

// ************* audio_pixel_mipmap_t ****************

audio_pixel_mipmap_t::audio_pixel_mipmap_t(MediaTrack* track, vec<double> resolutions)
    :m_accessor(safe_audio_accessor_t(track)), m_track(track) {
    
    info("creating audio pixel mipmap for track {:x}", 
          (void*)m_track);

    for (double res : resolutions) {
        m_blocks[res] = audio_pixel_block_t(res);
    }
    m_block_pps = resolutions;
    std::sort(m_block_pps.begin(), m_block_pps.end());
    fill_map();
}

audio_pixel_block_t audio_pixel_mipmap_t::get_pixels(opt<double> t0, opt<double> t1, double pix_per_s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    debug("mipmap: getting pixels for range {} to {} with resolution {}", t0.value_or(0), t1.value_or(-1), pix_per_s);

    int nearest_pps = get_nearest_pps(pix_per_s);

     // bail early if we already have the given pps
    if (nearest_pps == pix_per_s){
        return m_blocks.at(nearest_pps).get_pixels(t0, t1).clone();
    }

    // perform interpolation
    return m_blocks.at(nearest_pps).get_pixels(t0, t1).interpolate(pix_per_s);
}

// not thread-safe (needs lock)
double audio_pixel_mipmap_t::get_nearest_pps(double pix_per_s) {
    auto const it = std::lower_bound(m_block_pps.begin(), m_block_pps.end(), pix_per_s);
    if (it == m_block_pps.end())
        return m_block_pps.back();
    
    return *it;
}

// thread safe
bool audio_pixel_mipmap_t::flush(){

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

// thread safe
void audio_pixel_mipmap_t::to_json(json& j) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& map_entry : m_blocks) {
        j[std::to_string(map_entry.first)] = map_entry.second.get_pixels();
    }
}

void audio_pixel_mipmap_t::fill_map() {
    debug("starting mipmap fill");
    // m_accessor = safe_audio_accessor_t(m_track);

    if (!m_accessor.is_valid()) {
        info("got invalid audio accessor for track {:x}", (void*)m_track);
        return; 
    }
    // m_accessor.get();

    double accessor_start_time = GetAudioAccessorStartTime(m_accessor.get());
    double accessor_end_time = GetAudioAccessorEndTime(m_accessor.get());
    debug("mipmap: accessor start time: {}; end time: {};", accessor_start_time, accessor_end_time);
    if (accessor_end_time <= accessor_start_time) {
        info("mipmap: accessor end time is less than or equal to start time");
        return;
    }
    
    // calculate the number of samples we want to collect per channel
    int samples_per_channel = sample_rate() * (accessor_end_time - accessor_start_time);
    shared_ptr<vec<double>> sample_buffer = 
        std::make_shared<vec<double>>(samples_per_channel*num_channels());

    // TODO: can we do this from a worker thread? 
    // if not, is it too slow and will we have a problem?
    int sample_status = GetAudioAccessorSamples(m_accessor.get(), sample_rate(), num_channels(), 
                                                accessor_start_time, samples_per_channel, 
                                                sample_buffer->data());


    // samples stored in sample_buffer, operation status is returned
    if (sample_status < 1) {
        debug("mipmap: failed to get samples from accessor: error: {}", sample_status);
        return;
    } 
    
    m_pool->enqueue([this, sample_buffer](){
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            debug("mipmap: updating mipmap in worker thread");
            m_ready = false;
            // pass samples to update audio pixel blocks
            for (auto& it : m_blocks) {
                debug("updating block {}", it.first);
                it.second.update(*sample_buffer, num_channels(), sample_rate());
                it.second.transform();

                if (m_abort) {
                    debug("mipmap: aborting mipmap fill");
                    return;
                }
            };
            m_ready = true;
            debug("mipmap: finished updating mipmap in worker thread");
            // DestroyAudioAccessor(m_accessor.get());
        }
    });
}

// update all blocks
// MUST BE CALLED BY MAIN THREAD
// due to reaper api calls: AudioAccessorUpdate
bool audio_pixel_mipmap_t::update() {
    // debug("mipmap: updating mipmap");

    if (m_accessor.state_changed()) {
        info("mipmap: accessor state changed");
        AudioAccessorValidateState(m_accessor.get());
        AudioAccessorUpdate(m_accessor.get());

        int ret = AudioAccessorValidateState(m_accessor.get());

        m_abort = true;
        m_pool = std::make_unique<ThreadPool>(std::clamp(
            std::thread::hardware_concurrency(), 2u, 16u
        ));

        fill_map();
        return true;
    } else {
        return false;
    }
};

