#pragma once

#include "audio_pixel.h"
#include "haptic_track.h"
#include "osc.h"
#include "log.h"

#include <thread>
#include <queue>

#define project nullptr


using std::unique_ptr;

class pixel_block_sender_t {
public:
  pixel_block_sender_t(haptic_track_t& track,
                       shared_ptr<osc_manager_t> manager) 
      : m_track(track), m_manager(manager) { }


  void send(bool block = false) {
    if (!block) {
      debug("initing worker thread for pixel send");
      m_worker = std::thread(&pixel_block_sender_t::do_send, this);
    } else {
      do_send();
    }
  }

  void abort () {
    m_abort = true;
  }

  ~pixel_block_sender_t() {
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

private:
  // send the pixels, one by one,
  // along w/ an index
  void do_send() {
    // wait for mip map to be ready
    debug("inside worker thread, sending pixels");

    audio_pixel_block_t block = m_track.get_pixels();
    const vec<audio_pixel_t>& pixels = block.get_pixels()
                                        .at(m_track.get_active_channel());
    for (int i = 0 ; i < pixels.size() ; i++) {
      if (m_abort)
      {
        debug("pixel send aborted, exiting");
        return;
      }

      oscpkt::Message msg("/pixel");
      json j;
      j["id"] = i;
      j["value"] = abs(pixels.at(i).m_max);

      msg.pushStr(j.dump());
      
      // send the message
      m_manager->send(msg);
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }

  haptic_track_t& m_track;
  shared_ptr<osc_manager_t> m_manager;
  std::atomic<bool> m_abort;
  std::thread m_worker;
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
    m_sender = std::make_unique<pixel_block_sender_t>(
        m_tracks.active(), // TODO: maybe this should be a shared ptr
        m_manager
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
  unique_ptr<pixel_block_sender_t> m_sender;
};
