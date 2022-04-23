#pragma once

#include "audio_pixel.h"
#include "haptic_track.h"
#include "senders.h"
#include "osc.h"
#include "log.h"

#define project nullptr

using std::unique_ptr;

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
    };

    void make_new_pixel_sender() {
        debug("creating new pixel sender");
        m_senders.push(std::make_unique<block_pixel_sender_t>(
            *m_tracks.active(), // TODO: maybe this should be a shared ptr
            m_manager, 
            2048 * 8
        ));
    }

    // cancels any currently sending pixel stream 
    // and sends a new one
    void send_pixel_update(bool needs_resend = false) {
        info("sending pixel update to remote");
        info("senders size: {}", m_senders.size());

        if (needs_resend){
            info("whoops");
        }

        // check if the active track has changed
        bool active_track_has_changed = m_senders.size() ? m_tracks.active()->get_track_number() != 
                                         m_senders.back()->track().get_track_number() : true;
        if (m_senders.size() == 0) {
            make_new_pixel_sender();
        } else if (active_track_has_changed || needs_resend) {
            // cancel any pixels we're currently sending
            if (m_senders.back()) {
                // info("aborting the current sender");
                m_senders.back()->abort();
            }

            send_clear();
            make_new_pixel_sender();
        }

        m_senders.back()->send();
        // debug("pixel sender sent!");
    }

    void send_clear() {
        info("sending clear message to remote");

        oscpkt::Message msg("/pixels/clear");
        m_manager->send(msg);
    }

    void send_cursor() {
        info("sending cursor message to remote");

        oscpkt::Message msg("/cursor");
        msg.pushStr(json(m_tracks.active()->get_cursor_mip_map_idx()).dump());
        m_manager->send(msg);
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
                send_pixel_update(false);
                this->m_last_cursor_pos = index;
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

                // clear first, then update pixels
                send_pixel_update(true);
                this->m_last_zoom = GetHZoomLevel();
            }
        });

        m_manager->add_callback("/init",
        [this](Msg& msg){
            info("received /init from remote controller");
            m_tracks.add(GetTrack(project, 0));
            // set cursor to 0
            send_pixel_update(true);
        });

        m_manager->add_callback("/flush_map", 
        [this](Msg& msg){
            info("received /flush_map from remote controller");
            shared_ptr<haptic_track_t> active_track = m_tracks.active();
            json j;
            if (active_track)
                active_track->mipmap().flush();

        });
    };

    // this runs about 30x per second. do all OSC polling here
    virtual void Run() override {
        // handle any packets
        m_manager->handle_receive(false);

        shared_ptr<haptic_track_t> active_track = m_tracks.active();
        if (active_track) {
            int cursor_pos = active_track->get_cursor_mip_map_idx();
            if (cursor_pos != m_last_cursor_pos) {
                m_last_cursor_pos = cursor_pos;
                // send_cursor();
            }

            if (m_last_zoom != GetHZoomLevel()) {
                m_last_zoom = GetHZoomLevel();
                send_pixel_update(true);
            }

            if (active_track->update())
                send_pixel_update(true);
        }
        
    }

private:
    int m_last_cursor_pos {0};
    double m_last_zoom {0};

    shared_ptr<osc_manager_t> m_manager {nullptr};
    haptic_track_map_t m_tracks;
    std::queue<unique_ptr<block_pixel_sender_t>> m_senders;
};