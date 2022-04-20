#pragma once

#include "audio_pixel.h"
#include "haptic_track.h"
#include "senders.h"
#include "osc.h"
#include "log.h"
#include "include/zeroconf/source/zeroconf/zeroconf.service.h"

#define project nullptr

using std::unique_ptr;

void controller_browse_replies(
                            DNSServiceRef                       sd_ref,
                            DNSServiceFlags                     flags,
                            uint32_t                            interface_idx,
                            DNSServiceErrorType                 error_code,
                            const char                          *service_name,
                            const char                          *regtype,
                            const char                          *reply_domain,
                            void                                *context){
    spdlog::info(service_name);
    return;
}

void bonjour_message_callback(
    DNSServiceRef                       sdRef,
    DNSServiceFlags                     flags,
    DNSServiceErrorType                 errorCode,
    const char                          *name,
    const char                          *regtype,
    const char                          *domain,
    void                                *context) {
        info("a bonjour message call back called.");
        info("name: {}", name);
        info("regtype: {}", regtype);
        info("domain: {}", domain);
        info("error code: {}", errorCode);
}

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

        m_service = std::make_unique<zeroconf_service>("", "_osc._udp", "kiwi-reaper-pc", RECV_PORT);

        // check that bonjour service was registered with no errors
        // if (service_reg_status != kDNSServiceErr_NoError)
        //     return 0;

        return success;
    }

    virtual ~osc_controller_t() {}

    void OnTrackSelection(MediaTrack *trackid) override {
        // add the track to our map if we gotta
        m_tracks.add(trackid);
    };

    void make_new_pixel_sender() {
        debug("creating new pixel sender");
        m_sender = std::make_unique<block_pixel_sender_t>(
            m_tracks.active(), // TODO: maybe this should be a shared ptr
            m_manager, 
            128
        );
    }

    // cancels any currently sending pixel stream 
    // and sends a new one
    void send_pixel_update() {
        info("sending pixel update to remote");

        // check if the active track has changed
        if (!m_sender) {
            make_new_pixel_sender();
        }
        if (m_tracks.active().get_track_number() !=  m_sender->track().get_track_number()) {
            // cancel any pixels we're currently sending
            if (m_sender) {
                info("aborting the current sender");
                m_sender->abort();
            }

            m_sender.reset();
            make_new_pixel_sender();
        }

        m_sender->send();
        debug("pixel sender sent!");
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
            m_tracks.active().set_cursor(index);
            send_pixel_update();
        }
        });

        m_manager->add_callback("/zoom",
        [this](Msg& msg){
        float amt;
        if (msg.arg().popFloat(amt)
                    .isOkNoMoreArgs()){
            info("received /zoom {} from remote controller", amt);
            m_tracks.active().zoom((double)amt);

            send_pixel_update();
        }
        });

        m_manager->add_callback("/init",
        [this](Msg& msg){
        info("received /init from remote controller");
        m_tracks.add(GetTrack(project, 0));
        // set cursor to 0
        m_tracks.active().set_cursor(0);
        send_pixel_update();
        });
    };

    // this runs about 30x per second. do all OSC polling here
    virtual void Run() override {
        m_manager->handle_receive(false);
    }

private:
    bool m_connection_status {false};
    shared_ptr<osc_manager_t> m_manager {nullptr};
    haptic_track_map_t m_tracks;
    unique_ptr<block_pixel_sender_t> m_sender;
    DNSServiceRef m_kiwi_service_ref;
    std::unique_ptr<zeroconf_service> m_service {nullptr};
};
