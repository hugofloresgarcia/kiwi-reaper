#pragma once

#include "audio_pixel.h"
#include "haptic_track.h"
#include "osc.h"
#include "log.h"

#include <thread>

// this is the format that the iOS client expects
class audio_haptic_pixel_t {
public: 
    audio_haptic_pixel_t() {};
    audio_haptic_pixel_t(int idx, audio_pixel_t pixel) : 
        id(idx) 
    {
        value = abs(pixel.m_max) + abs(pixel.m_min); 
    };

    int id { 0 };
    double value { 0 };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(audio_haptic_pixel_t, id, value);
};

// interface class for pixel block sender
class pixel_sender_t {
public:
    pixel_sender_t(haptic_track_t& track, shared_ptr<osc_manager_t> manager)
        : m_track(track), m_manager(manager) {}

    void send(bool block = false) {
        if (!block) {
            debug("initing worker thread for pixel send");
            m_workers.emplace_back(
                std::thread(&pixel_sender_t::do_send, this)
            );
        } else {
            do_send();
        }
    }

    void abort () {
        m_abort = true;
    }

    ~pixel_sender_t() {
        for (auto& worker: m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    const haptic_track_t& track() {
        return m_track;
    }

protected:
    // implement me!
    virtual void do_send() = 0;

    haptic_track_t& m_track;
    shared_ptr<osc_manager_t> m_manager;
    std::atomic<bool> m_abort;
    std::deque<std::thread> m_workers;
};


// send the pixels, one by one,
class single_pixel_sender_t : public pixel_sender_t {
public:
    using pixel_sender_t::pixel_sender_t;

private:
    
    void do_send() override {
        // wait for mip map to be ready
        debug("inside worker thread, sending pixels");

        audio_pixel_block_t block = m_track.get_pixels();

        if (block.get_pixels().empty()) {
            debug("no pixels to send");
            return;
        }

        const vec<audio_pixel_t>& pixels = block.get_pixels()
                                                .at(m_track.get_active_channel());
        for (int i = 0 ; i < pixels.size() ; i++) {
            if (m_abort)
            {
                debug("pixel send aborted, exiting");
                return;
            }

            oscpkt::Message msg("/pixel");
            json j = audio_haptic_pixel_t(i, pixels.at(i));

            msg.pushStr(j.dump());
            
            // send the message
            m_manager->send(msg);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
};


// sends the pixels as a block
// sent as a JSON dictionary, like this
/*
{
    "pixels": [
        {
            "id": 0,
            "value": 0.5
        },
        {
            "id": 1,
            "value": 0.5
        }
    ],
    "startIdx": 0,
}
*/
class block_pixel_sender_t : public pixel_sender_t {
public:
    block_pixel_sender_t(haptic_track_t& track, 
                         shared_ptr<osc_manager_t> manager,
                         size_t block_size)
        : pixel_sender_t(track, manager), m_block_size(block_size)
    {}

private:
    using range = std::pair<size_t, size_t>;

    range get_send_range(const vec<audio_pixel_t>& pixels) {
        // find the current cursor position
        int center_idx = m_track.get_cursor_mip_map_idx();

        // find the start and end indices
        int start_idx = std::clamp(center_idx - (int)m_block_size / 2, 0, (int)pixels.size());
        int end_idx = std::clamp(center_idx + (int)m_block_size / 2 - 1, start_idx, (int)pixels.size());

        range trimmed_range = trim_block_if_sent(range(start_idx, end_idx));
        return trimmed_range;
    }

    void do_send() override {
        // wait for mip map to be ready
        debug("inside worker thread, sending pixels");

        if (m_block.get_pixels().empty()) {
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_block = m_track.get_pixels();
            }
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (m_block.get_pixels().empty()) {
            debug("no pixels to send");
            return;
        }

        const vec<audio_pixel_t>& pixels = m_block.get_pixels()
                                                .at(m_track.get_active_channel());

        range send_range = get_send_range(pixels);

        if ((send_range.second - send_range.first) == 0) {
            debug("no pixels to send");
            return;
        }

        m_sent_blocks.emplace_back(send_range);

        // get a temporary view of the sub block we're sending
        const vec<audio_pixel_t>& sub_block = get_view(pixels, send_range.first, send_range.second);

        // convert audio pixels to haptic pixels
        vec<audio_haptic_pixel_t> haptic_block(sub_block.size());
        for (int i = 0 ; i < sub_block.size() ; i++) {
            haptic_block.at(i) = audio_haptic_pixel_t(i, sub_block.at(i));
        }

        // abort before we send if we we're asked to abort
        if (m_abort)
        {
            debug("pixel send aborted, exiting");
            return;
        }

        // prep the message!
        oscpkt::Message msg("/pixels");
        json j;
        j["startIdx"] = send_range.first;
        j["pixels"] = haptic_block;

        msg.pushStr(j.dump());

        // send the message
        m_manager->send(msg);

    };

    // clamp around the edges of the block, if the edges of the block have already been sent
    // will return  (-1, -1) if the block needn't be sent at all (completely intersects)
    // a block that we have already sent
    std::pair<size_t, size_t> trim_block_if_sent(range inblock) {
        for (const auto& block : m_sent_blocks) {
            if (range_intersects_before(inblock, block)) {
                inblock.first = block.second;
            }
            if (range_intersects_before(block, inblock)) {
                inblock.second = block.first;
            }
            if (inblock.first >= inblock.second) {
                return {0, 0};
            }
        }
        return inblock;
    }

    bool range_intersects_before(range a, range b) {
        return a.first < b.second && b.first < a.second;
    }
    
    size_t m_block_size;
    std::vector<range> m_sent_blocks;

    std::shared_mutex m_mutex;
    audio_pixel_block_t m_block;
};

