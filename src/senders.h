#pragma once

#include "audio_pixel.h"
#include "haptic_track.h"
#include "osc.h"
#include "log.h"
#include "include/ThreadPool/ThreadPool.h"


// interface class for pixel block sender
// pixel senders send blocks of pixels to the OSC client
class pixel_sender_t {
public:
    // a range of indices to send
    using range = std::pair<size_t, size_t>;

    pixel_sender_t(haptic_track_t& track, 
                   shared_ptr<osc_manager_t> manager, 
                   size_t block_size)
        : m_track(track), m_manager(manager), m_block_size(block_size) {}

    // send, optionally blocking until the block is sent
    void send(bool send_cursor = false, bool block = false) {
        if (!block) {
            debug("initing worker thread for pixel send");
            m_work_pool.enqueue([this, send_cursor]() {
                prepare_and_send(send_cursor);
            });
        } else {
            prepare_and_send(send_cursor);
        }
    }

    // sends an abort message to all active workers
    void abort () {
        m_abort = true;
    }

    // make sure to call abort() before destroying, 
    // unless you want to wait for all threads to finish
    // sending
    ~pixel_sender_t() {
    }

    // get the track associated with the sender
    const haptic_track_t& track() {
        return m_track;
    }

protected:
    // implement me!
    virtual void do_send(haptic_pixel_block_t& block) = 0;

    // our parent track
    haptic_track_t& m_track;

    // our osc manager
    shared_ptr<osc_manager_t> m_manager;

    // abort flag
    std::atomic<bool> m_abort {false};
    bool m_done {false};

    // the size of the block to send
    size_t m_block_size;

private:
    // prepares a haptic pixel block for sending
    void prepare_and_send(bool send_cursor) {
        debug("inside worker thread, sending pixels");

        // if our block is initially empty
        // try to query the track to fill it up
        if (m_block.get_pixels().empty()) {
            {
                // unique lock to modify the block!
                std::unique_lock<std::shared_mutex> lock(m_block_mutex);
                m_block = m_track.get_pixels();
                // unlocks here
            }
        }

        // shared lock to read from the block
        std::shared_lock<std::shared_mutex> lock(m_block_mutex);

        // check again, but this time return if they're empty
        if (m_block.get_pixels().empty()) {
            debug("no pixels to send");
            m_done = true;
            return;
        }

        const vec<audio_pixel_t>& pixels = m_block.get_pixels()
                                                .at(m_track.get_active_channel());

        // calculate the range to send
        // taking into account pixels that we have already sent
        range send_range = get_send_range(pixels);

        if ((send_range.second - send_range.first) == 0) {
            debug("no pixels to send");
            m_done = true;
            return;
        }

        // get a temporary view of the sub block we're sending
        const vec<audio_pixel_t>& sub_block = get_view(pixels, send_range.first, send_range.second);

        // convert audio pixels to haptic pixels
        haptic_pixel_block_t haptic_block(sub_block.size());
        for (int i = 0 ; i < sub_block.size() ; i++) {
            // use the true index as the id for the haptic pixel
            haptic_block.at(i) = haptic_pixel_t(i + send_range.first, sub_block.at(i));
        }

        // unlock the block
        lock.unlock();

        // send!
        do_send(haptic_block);

        // document the range of pixels we sent
        std::unique_lock<std::shared_mutex> lock2(m_sent_mutex);
            m_sent_blocks.emplace_back(send_range);

        // // wait a bit, then set the cursor
        if (send_cursor) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            oscpkt::Message msg("/cursor");
            msg.pushStr(json(m_track.get_cursor_mip_map_idx()).dump());
            m_manager->send(msg);
        }
        m_done = true;
    }

    range get_send_range(const vec<audio_pixel_t>& pixels) {
        debug("calculating send range...");

        // find the current cursor position
        int center_idx = m_track.get_cursor_mip_map_idx();

        // find the start and end indices
        int start_idx = std::clamp(center_idx - (int)m_block_size / 2, 0, (int)pixels.size());
        int end_idx = std::clamp(center_idx + (int)m_block_size / 2 - 1, start_idx, (int)pixels.size());

        range trimmed_range = trim_block_if_sent(range(start_idx, end_idx));
        debug("send range is {} to {}", trimmed_range.first, trimmed_range.second);
        return trimmed_range;
    }

    // clamp around the edges of the block, if the edges of the block have already been sent
    // will return  (-1, -1) if the block needn't be sent at all (completely intersects)
    // a block that we have already sent
    std::pair<size_t, size_t> trim_block_if_sent(range inblock) {
        std::shared_lock<std::shared_mutex> lock(m_sent_mutex);
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

    // check if the ranges intersect
    // ranges must be in natural order (i think?)
    bool range_intersects_before(range a, range b) {
        return a.first < b.second && b.first < a.second;
    }

    // worker threads
    ThreadPool m_work_pool {
        std::clamp(
            std::thread::hardware_concurrency(), 1u, 3u
        )
    };

    // keep track of any blocks that have already been sent
    std::shared_mutex m_sent_mutex;
        std::vector<range> m_sent_blocks;

    // the block of audio pixels we're converting to haptic pixels
    std::shared_mutex m_block_mutex;
        audio_pixel_block_t m_block;
};


// send the pixels, one by one,
class single_pixel_sender_t : public pixel_sender_t {
public:
    using pixel_sender_t::pixel_sender_t;

protected:
    void do_send(haptic_pixel_block_t& block) override {
        for (const auto& pix : block) {
            // abort before we send if we we're asked to abort
            if (m_abort)
            {
                debug("pixel send aborted, exiting");
                return;
            }

            oscpkt::Message msg("/pixel");
            json j = pix;

            msg.pushStr(j.dump());

            // send the message
            m_manager->send(msg);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
};


// sends the pixels in single OSC message, packed into a JSON list 
class block_pixel_sender_t : public pixel_sender_t {
public:
    using pixel_sender_t::pixel_sender_t;

private:
    void do_send(haptic_pixel_block_t& block) override {
        // abort before we send if we we're asked to abort
        if (m_abort)
        {
            debug("pixel send aborted, exiting");
            return;
        }

        for (size_t i = 0; i < block.size(); i += m_chunk_size) {
            debug("sender, sending chunk {}", i);
            size_t last = std::min(block.size(), i + m_chunk_size);
            const vec<haptic_pixel_t>& chunk = get_view(block, i, last);
            debug("chunk range is {} to {}", i, last);

            // prep the message!
            oscpkt::Message msg("/pixels");
            json j = chunk;

            msg.pushStr(j.dump());

            // send the message
            m_manager->send(msg);
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    };

    int m_chunk_size {64};
};

