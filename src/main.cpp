#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#define REAPERAPI_IMPLEMENT
// automatically check if registered api call is valid 
#define BAIL_IF_NO_REG(x) if (!x) { std::cerr<<"failed to register function "<<x<<std::endl; return 0; }
#define REG_FUNC(x,y) (*(void **)&x) = y->GetFunc(#x) ; BAIL_IF_NO_REG(x) ;

#include "reaper_plugin_functions.h"
#include <cstdio>
#include <string>
#include <utility>

#include "controller.h"

static bool testAction(int commandId, int flat){
  ShowConsoleMsg("hello world!\n");
  return true;
}

static std::string ADDRESS = "127.0.0.1";
static int SEND_PORT = 8000;
static int RECV_PORT = 8001;


extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  if(!rec)
    return 0;

  if(rec->caller_version != REAPER_PLUGIN_VERSION)
    return 0;

  // register api functions that we want
  REG_FUNC(AudioAccessorStateChanged, rec);
  REG_FUNC(AudioAccessorUpdate, rec);
  REG_FUNC(AudioAccessorValidateState, rec);
  REG_FUNC(CreateTrackAudioAccessor, rec);
  REG_FUNC(CSurf_OnZoom, rec);
  REG_FUNC(DestroyAudioAccessor, rec);
  REG_FUNC(GetAudioAccessorEndTime, rec);
  REG_FUNC(GetAudioAccessorStartTime, rec);
  REG_FUNC(GetAudioAccessorSamples, rec);
  REG_FUNC(GetCursorPosition, rec);
  REG_FUNC(GetHZoomLevel, rec);
  REG_FUNC(MoveEditCursor, rec);
  REG_FUNC(ShowConsoleMsg, rec);
  REG_FUNC(SetEditCurPos, rec);

  // includes the master track
  REG_FUNC(GetSelectedTrack2, rec);
  REG_FUNC(CountSelectedTracks2, rec);

  REG_FUNC(GetMediaSourceNumChannels, rec);
  REG_FUNC(GetMediaSourceSampleRate, rec);
  REG_FUNC(GetMediaTrackInfo_Value, rec);
  REG_FUNC(GetSetMediaItemInfo, rec);
  REG_FUNC(GetMediaItemInfo_Value, rec);

  REG_FUNC(GetSelectedMediaItem, rec);
  REG_FUNC(GetActiveTake, rec);
  REG_FUNC(GetMediaItemTake_Peaks, rec);
  REG_FUNC(GetMediaItemTake_Source, rec);
  REG_FUNC(PCM_Source_GetPeaks, rec);
  REG_FUNC(SetMediaItemTakeInfo_Value, rec);
  REG_FUNC(SetMediaItemInfo_Value, rec);

  // create controller
  OSCController *controller = new OSCController(ADDRESS, SEND_PORT, RECV_PORT);
  if (!controller->init()) {
    ShowConsoleMsg("KIWI: failed to initialize OSC controller\n");
    return 0;
  }

  // register action hooks
  if (!rec->Register("csurf_inst", (void *)controller))
    return 0;

  // initialization code here

  return 1;
}


