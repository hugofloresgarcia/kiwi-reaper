#include "reaper_plugin_functions.h"
#include "osc.h"

#include <cmath>

// only use the active project for now
#define project nullptr

double get_rms_for_frame(double t0, double t1) {
  // get the sample rate from the master track rn
  int sample_rate = 16000;
  int num_samples_per_channel = (int)((t1 - t0) * (double)sample_rate);
  int num_channels = (int)GetMediaTrackInfo_Value(track, "I_NCHAN");

  // allocate some memory
  double *samples = (double *)malloc(sizeof(double) *\
                        num_samples_per_channel * num_channels);
  // TODO: add logging in each of these
  if (!samples)
    return;

  // get the samples
  if (!GetAudioAccessorSamples(accessor, sample_rate, num_channels, 
                              t0, num_samples_per_channel, samples)) 
    return;

  // calculate the peak
  double peak = 0;
  double rms = 0;
  for (int c = 0; c < num_channels; c++) {
    // samples are interleaved
    for (int i = c; i < num_samples_per_channel; i += num_channels) {
      peak = std::max(peak, samples[i]);
      rms += samples[i] * samples[i];
    }
  }
  rms = std::sqrt((float)(rms / num_samples_per_channel));

  // free the accessor and memory
  // TODO: wrap the accessor in a RAII class
  DestroyAudioAccessor(accessor);
  free(samples);

  return rms
}

class OSCController : IReaperControlSurface {
  OSCController();
public: 
  OSCController(std::string& addr, int send_port, int recv_port)
    : m_manager(std::make_unique<OSCManager>(addr, send_port, recv_port)) {}

  virtual const char* GetTypeString() override { return "HapticScrubber"; }
  virtual const char* GetDescString() override { return "A tool for scrubbing audio using haptic feedback."; }
  virtual const char* GetConfigString() override { return ""; }

  bool init () {
    bool success = m_manager->init();
    add_callbacks();
    return success;
  }

  virtual ~OSCController() {}

  // TODO: should switch focus to last selected track
  void OnTrackSelection(MediaTrack *trackid) override {};
  
  // use this to register all callbacks with the osc manager
  void add_callbacks() {
    using Msg = oscpkt::Message;

    // add a callback to listen to
    m_manager->add_callback("/move_edit_cursor",
    [](Msg& msg){
      float move_amt;
      if (msg.arg().popFloat(move_amt).isOkNoMoreArgs()){
        SetEditCurPos(
          GetCursorPosition() + (double)move_amt, 
          true, true
        );
      }
    });

    m_manager->add_callback("/zoom",
    [](Msg& msg){
      int xdir;
      if (msg.arg().popInt32(xdir).isOkNoMoreArgs()){
        CSurf_OnZoom(xdir, 0);
      }
    });

    m_manager->add_callback("/get_amplitude_at_time", 
    [this](Msg& msg){
      float time;
      if (msg.arg().popFloat(time)){
        // this works but is super slow if the buffer is large bc lots of zoom is . what do then?
        // The entire block's samples fall within one pixel column.
         // Either it's a rare odd block at the end, or else,
         // we must be really zoomed out!
         // Omit the entire block's contents from min/max/rms
         // calculation, which is not correct, but correctness might not
         // be worth the compute time if this happens every pixel
         // column. -- PRL
        // TODO: abstract me away into a class

        // TODO: need to check if we're displaying individual samples
        // TODO: also what happens when there are too many samples in a single pixel? e.g the whole track is in a single pixel
        int num_sel_tracks = CountSelectedTracks2(project, true);
        MediaTrack *track = GetSelectedTrack2(project, num_sel_tracks-1, true);

        // create an accessor for the samples we're gonna gather
        AudioAccessor* accessor = CreateTrackAudioAccessor(track);
        if (!accessor) return;

        // see how many seconds we're gonna get to calculate the peak
        int num_pixels = 5;
        double seconds_per_pixel = num_pixels / GetHZoomLevel();

        double t0 = time - seconds_per_pixel / 2;
        double t1 = time + seconds_per_pixel / 2;

        double allowedt0 = GetAudioAccessorStartTime(accessor);
        double allowedt1 = GetAudioAccessorEndTime(accessor);

        t0 = std::max(t0, allowedt0);
        t1 = std::min(t1, allowedt1);

        double rms = get_rms_for_frame(t0, t1);
        // send the peak back to the client
        oscpkt::Message msg;
        msg.init("/rms").pushFloat(float(rms));
        m_manager->send(msg);
      }
    });
  };

  // this runs about 30x per second. do all OSC polling here
  virtual void Run() override {
    // handle any packets
    m_manager->handle_receive(false);
  }

private:
  std::unique_ptr<OSCManager> m_manager {nullptr};
};
