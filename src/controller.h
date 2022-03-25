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
  };

  // cancels any currently sending pixel stream 
  // and sends a new one
  void send_pixel_update() {
    info("sending pixel update to remote");

    // cancel any pixels we're currently sending
    if (m_sender) {
      info("found another pixel sender already running. aborting the current sender");
      m_sender->abort();
    }

    m_sender.reset();
    debug("creating new pixel sender");
    m_sender = std::make_unique<block_pixel_sender_t>(
        m_tracks.active(), // TODO: maybe this should be a shared ptr
        m_manager, 
        128
    );
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
    // handle any packets
    m_manager->handle_receive(false);

    // TODO: active track do any necessary updates here
  }

private:
  shared_ptr<osc_manager_t> m_manager {nullptr};
  haptic_track_map_t m_tracks;
  unique_ptr<block_pixel_sender_t> m_sender;
};
