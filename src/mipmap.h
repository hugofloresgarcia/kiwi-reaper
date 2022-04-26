#pragma once

#include "pixel_block.h"
#include "accessor.h"
#include <shared_mutex>

#include "include/ThreadPool/ThreadPool.h"

#include <fstream>

#define project nullptr

// hold audio_pixel_block_t at different resolutions
// and is able to interpolate between them to 
// get audio pixels at any resolution inbetween
// thread safe (will block if another thread is updating)

class audio_pixel_mipmap_t;
using mipmap_update_closure_t = std::function<void(audio_pixel_mipmap_t& map)>;

class audio_pixel_mipmap_t {
public:
    audio_pixel_mipmap_t(shared_ptr<audio_accessor_t> accessor, vec<double> resolutions)
    :m_accessor(accessor) {
        info("creating audio pixel mipmap");

        for (double res : resolutions) {
            if (res > 0)
                m_blocks[res] = audio_pixel_block_t(res);
        }
        m_block_pps = resolutions;
        std::sort(m_block_pps.begin(), m_block_pps.end());
    }

    // returns a copy of the block at the specified resolution
    audio_pixel_block_t get_pixels(opt<double> t0, opt<double> t1, 
                                  double pix_per_s){
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

    // flush contents to file
    bool flush(){
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

    void to_json(json& j) {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto& map_entry : m_blocks) {
            j[std::to_string(map_entry.first)] = map_entry.second.get_pixels();
        }
    }
    

    // update contents of the mipmap 
    // (if the audio accessor state has changed)
    bool update(mipmap_update_closure_t on_update, bool force = false){
        if (m_accessor->state_changed() || force) {
            info("mipmap: accessor state changed");

            m_pool = std::make_unique<ThreadPool>(std::clamp(
                std::thread::hardware_concurrency(), 2u, 16u
            ));

            auto buffer = std::make_shared<vec<double>>();
            m_accessor->get_samples(*buffer);

            m_pool->enqueue([this, buffer, on_update](){
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    
                    debug("mipmap: updating mipmap in worker thread");
                    // pass samples to update audio pixel blocks
                    for (auto& it : m_blocks) {
                        debug("updating block {}", it.first);
                        it.second.update(*buffer, m_accessor->num_channels(), 
                                                m_accessor->sample_rate());
                        it.second.transform();

                        if (m_abort) {
                            info("mipmap: abort requested");
                            return;
                        }
                    };
                    debug("mipmap: finished updating mipmap in worker thread");
                }
                // make sure the lock is released before we call this
                on_update(*this);
            });

            return true;
        } else {
            return false;
        }
    }

private:
    // finds the lower bound of cached resolutions
    double get_nearest_pps(double pix_per_s){
        auto const it = std::lower_bound(m_block_pps.begin(), m_block_pps.end(), pix_per_s);
        if (it == m_block_pps.end())
            return m_block_pps.back();
        
        return *it;
    }

    // create a new interpolated block from a source block
    audio_pixel_block_t create_interpolated_block(double src_pps, double new_pps,
                                                 opt<double> t0, opt<double> t1);
    
    // where we get the samples from
    shared_ptr<audio_accessor_t> m_accessor {nullptr}; 

    // the lo-res pixel blocks
    // TODO: these shouldn't be doubles
    std::map<double, audio_pixel_block_t, std::greater<double>> m_blocks;

    // sorted list of the blocks pps
    vec<double> m_block_pps; 

    std::unique_ptr<ThreadPool> m_pool {
        std::make_unique<ThreadPool>(
            std::clamp(
                std::thread::hardware_concurrency(), 2u, 16u
            )
        )
    };

    std::mutex m_mutex;
    std::atomic<bool> m_abort {false};
};