#include <iostream>

#define REAPERAPI_IMPLEMENT
// automatically check if registered api call is valid 
#define BAIL_IF_NO_REG(x) if (!x) { std::cerr<<"failed to register function "<<x<<std::endl; return 0; }
#define REG_FUNC(x,y) (*(void **)&x) = y->GetFunc(#x) ; BAIL_IF_NO_REG(x) ;

#include "reaper_plugin_functions.h"
#include <cstdio>
#include <string>

static bool testAction(int commandId, int flat){
  ShowConsoleMsg("hello world!\n");
  return true;
}

int register_action_hook(std::string desc, std::string action_name, void* function_address, reaper_plugin_info_t *rec) {
    int new_action_id = plugin_register("command_id", (void*)"kiwiTestAction");

    if (!rec->Register("hookcustommenu", function_address)) {
        std::cerr << "Failed to register actioin:  " << action_name << std::endl;
    }
    return new_action_id;
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  if(!rec) {
    // cleanup code here
    return 0;
  }

  if(rec->caller_version != REAPER_PLUGIN_VERSION)
    return 0;

  // register api functions that we want
  REG_FUNC(CreateTrackAudioAccessor, rec);
  REG_FUNC(GetAudioAccessorEndTime, rec);
  REG_FUNC(GetAudioAccessorStartTime, rec);
  REG_FUNC(GetAudioAccessorSamples, rec);
  REG_FUNC(AudioAccessorStateChanged, rec);
  REG_FUNC(AudioAccessorUpdate, rec);
  REG_FUNC(AudioAccessorValidateState, rec);
  REG_FUNC(DestroyAudioAccessor, rec);

  // see also https://gist.github.com/cfillion/350356a62c61a1a2640024f8dc6c6770
  ShowConsoleMsg = (decltype(ShowConsoleMsg))rec->GetFunc("ShowConsoleMsg");
  
  if(!ShowConsoleMsg) {
    fprintf(stderr, "[reaper_barebone] Unable to import ShowConsoleMsg\n");
    return 0;
  }

  int kiwi_action_id = register_action_hook("the first test action B)", "kiwi test action", &testAction, rec);

  // initialization code here

  return 1;
}


