#pragma once

#include "haptic_track.h"
#include "mipmap.h"
#include "osc.h"
#include "log.h"

#include <iostream>
#include <chrono>
#include <thread>

#define project nullptr

using std::unique_ptr;

int TIMEOUT = 2000;

enum class controller_mode {
    mipmap, 
    meter
};

class osc_controller_t : IReaperControlSurface {

    osc_controller_t();

public: 
    osc_controller_t(std::string& addr, int send_port, int recv_port)
        : m_manager(std::make_unique<osc_manager_t>(addr, send_port, recv_port)) {}

    virtual const char* GetTypeString() override { return "HapticScrubber"; }
    virtual const char* GetDescString() override { return "A tool for scrubbing audio using haptic feedback."; }
    virtual const char* GetConfigString() override { return ""; }

    bool init () {
        bool success = m_manager->init();
        // TODO: we should have a pointer to an
        // active track object 
        add_callbacks();
        return success;
    }

    virtual ~osc_controller_t() {}

    void OnTrackSelection(MediaTrack *trackid) override {
        // add the track to our map if we gotta
        m_tracks.add(trackid);
        m_tracks.active(trackid);
        info("set {} as the active track", (void*)trackid);
    };

    void SetSurfaceSelected(MediaTrack* trackid, bool selected) override {
        if (!selected) {
            return;
        }

        // add the track to our map if we gotta
        m_tracks.add(trackid);
        m_tracks.active(trackid);
        info("setsurface: set {} as the active track", (void*)trackid);
    }

    void send_pixel(int mipmap_idx) {
        shared_ptr<haptic_track_t> active_track = m_tracks.active();

        if (active_track) {
            m_pool.enqueue([this, active_track, mipmap_idx]() {
                info("getting pixel at {}", mipmap_idx);
                audio_pixel_t audio_pix = active_track->get_pixel(mipmap_idx);
                haptic_pixel_t haptic_pix(mipmap_idx, audio_pix);

                oscpkt::Message msg("/pixel");
                json j = haptic_pix;

                msg.pushStr(j.dump());
                m_manager->send(msg);
            });
        }
    }

    void send_pixels(int start, int end) {
        if ((end - start) < 1) {
            info("range is empty");
            return;
        }

        shared_ptr<haptic_track_t> active_track = m_tracks.active();

        if (active_track) {
            m_pool.enqueue([this, active_track, start, end]() {
                info("inside worker thread, getting pixels from {} to {}", start, end);
                audio_pixel_block_t& audiopix_block = active_track->get_pixels();

                auto haptic_block = from(audiopix_block.get_pixels()
                                            .at(active_track->get_active_channel()), 
                                                start, end);

                size_t chunk_size = 128;
                for (size_t i = 0; i < haptic_block.size(); i+= chunk_size) {
                    size_t last = std::min(i + chunk_size, haptic_block.size());

                    const haptic_pixel_block_t& chunk = get_view(haptic_block, i, last);

                    oscpkt::Message msg("/pixels");
                    json j = chunk;
                    msg.pushStr(j.dump());
                    m_manager->send(msg);

                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }

                debug("pixel block sent");
            });
        } else {
            info("no active track, can't send pixels");
        }
    }

    void send_cursor() {
        info("sending cursor message to remote");

        // TODO: make sure that the cursor is within the bounds of the mipmap 
        // before we send

        oscpkt::Message msg("/cursor");
        msg.pushStr(json(m_tracks.active()->get_cursor_mip_map_idx()).dump());
        m_manager->send(msg);
    }
    
    void set_mode(const std::string mode) {
        info("setting mode to {}", mode);
        if (mode == "mipmap") {
            m_mode = controller_mode::mipmap;
        } else if (mode == "meter") {
            m_mode = controller_mode::meter;
        } else {
            warn("invalid controller mode given: {}", mode);
        }
    }

    // TODO: un-
    void send_peaks() {
        if (!m_tracks.active()) {
            info("no active track, can't send peaks");
            return;
        }

        info("querying peak level from track {} and channel {}", 
                (void*)m_tracks.active()->get_track(), 
                m_tracks.active()->get_active_channel()
        );
        double level = Track_GetPeakInfo(m_tracks.active()->get_track(), 
                                m_tracks.active()->get_active_channel());
        info("sending peak level: {}", level);

        oscpkt::Message msg("/peak");
        json j = level;
        msg.pushStr(j.dump());
        m_manager->send(msg);
    }

    bool get_connection_status() {
        // resets the connection status
        m_connection_status = false;

        oscpkt::Message msg("/ping");
        m_manager->send(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT));

        return m_connection_status;
    }

    // use this to register all callbacks with the osc manager
    void add_callbacks() {
        using Msg = oscpkt::Message;

        // add a callback to listen to
        m_manager->add_callback("/set_cursor",
        [this](Msg& msg){
            int index;
            if (msg.arg().popInt32(index)
                        .isOkNoMoreArgs()){
                info("received /set_cursor to {}", index);
                shared_ptr<haptic_track_t> active_track = m_tracks.active();
                if (active_track)
                    active_track->set_cursor(index);
            }
        });

        // send a single pixel, given a mip map idx
        m_manager->add_callback("/pixel",
        [this](Msg& msg){
            int index;
            if (msg.arg().popInt32(index)
                        .isOkNoMoreArgs()){
                send_pixel(index);
            }
        });

        // send a block of pixels, given a range of indices
        m_manager->add_callback("/pixels",
        [this](Msg& msg){
            std::string json_str;
            if (msg.arg().popStr(json_str)
                        .isOkNoMoreArgs()){
                info("range received: {}", json_str);
                auto range = json::parse(json_str);
                int start = range.at(0).get<int>();
                int end = range.at(1).get<int>();

                send_pixels(start, end);
            }
        });

        m_manager->add_callback("/zoom",
        [this](Msg& msg){
            float amt;
            if (msg.arg().popFloat(amt)
                        .isOkNoMoreArgs()){
                info("received /zoom {} from remote controller", amt);
                shared_ptr<haptic_track_t> active_track = m_tracks.active();
                if (active_track)
                    active_track->zoom((double)amt);
            }
        });

        m_manager->add_callback("/flush_map", 
        [this](Msg& msg){
            info("received /flush_map from remote controller");
            shared_ptr<haptic_track_t> active_track = m_tracks.active();
            json j;
            if (active_track)
                active_track->mipmap()->flush();

        });

        m_manager->add_callback("/sync",
        [this](Msg& msg){
            info("received /sync from remote controller");
            shared_ptr<haptic_track_t> active_track = m_tracks.active();
            if (active_track) {
                send_cursor();
            }
        });

        m_manager->add_callback("/set_mode",
        [this](Msg& msg){
            std::string mode;
            if (msg.arg().popStr(mode)
                        .isOkNoMoreArgs()){
                set_mode(mode);
            }
        });
        
        m_manager->add_callback("/ping_ack", 
        [this](Msg& msg){
            m_connection_status = true;
        });

        m_manager->add_callback("/ping", 
        [this](Msg& msg){
            info("received /ping from remote controller");
            oscpkt::Message ackmsg("/ping_ack");
            m_manager->send(ackmsg);
        });
    }

    // this runs about 30x per second. do all OSC polling here
    virtual void Run() override {
        // handle any packets
        m_manager->handle_receive(false);

        auto active_track  = m_tracks.active();
        if (!active_track) { return; }
        switch (m_mode) {
            case controller_mode::mipmap:
                // check for updates
                if (active_track->mipmap()) {
                    active_track->mipmap()->update(mipmap_update_closure_t(), false);
                }
                break;

            case controller_mode::meter:
                send_peaks();
                break;
        }
    }

private:
    controller_mode m_mode {controller_mode::mipmap};
    shared_ptr<osc_manager_t> m_manager {nullptr};
    haptic_track_map_t m_tracks;
    ThreadPool m_pool { 4 };
    std::atomic<bool> m_connection_status;
};