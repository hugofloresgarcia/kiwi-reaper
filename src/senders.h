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
            m_worker = std::thread(&pixel_sender_t::do_send, this);
        } else {
            do_send();
        }
    }

    void abort () {
        m_abort = true;
    }

    ~pixel_sender_t() {
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

protected:
    virtual void do_send() = 0;

    haptic_track_t& m_track;
    shared_ptr<osc_manager_t> m_manager;
    std::atomic<bool> m_abort;
    std::thread m_worker;
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

        for (int start_idx = 0; start_idx < pixels.size() ;
             start_idx = start_idx + m_block_size) {
            
            if (m_abort)
            {
                debug("pixel send aborted, exiting");
                return;
            }

            const vec<audio_pixel_t>& sub_block = get_view(pixels, start_idx, start_idx + m_block_size);
            vec<audio_haptic_pixel_t> haptic_block(sub_block.size());
            for (int i = 0 ; i < sub_block.size() ; i++) {
                haptic_block.at(i) = audio_haptic_pixel_t(i, sub_block.at(i));
            }

            oscpkt::Message msg("/pixels");
            json j;
            j["startIdx"] = start_idx;
            j["pixels"] = haptic_block;

            msg.pushStr(j.dump());

            // send the message
            m_manager->send(msg);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    };

    size_t m_block_size;
};