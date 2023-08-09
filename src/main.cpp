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
#include <condition_variable>
#include <cstdio>
#include <string>
#include <utility>

#include "controller.h"

#include "ip.h"

static int SEND_PORT = 8000;
static int RECV_PORT = 8001;

// moved these out for the action to have access to them
int CONNECTION_ACTION_ID;
osc_controller_t *controller;

static bool kiwi_connection_status(int commandId, int flag)
{
	if (commandId == CONNECTION_ACTION_ID) {
    bool connection_status = controller->get_connection_status();

    if (connection_status)  
      ShowConsoleMsg("Kiwi Haptic Interface is connected.");
    else 
      ShowConsoleMsg("Kiwi Haptic Interface is not connected.");
      
		return true;
	}
	return false;
}

std::string trim(const std::string& str,
                 const std::string& whitespace = " ")
{
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

std::string get_ip_address(const std::string& resource_path) {
  size_t addrsize = 16;
  std::string addr(addrsize, ' ');
  // load cached ip address (if it exists)
  std::string ip_address_cache_file = resource_path + "/kiwi-ip.json";
  std::ifstream ip_cache_ifs(ip_address_cache_file);
  if (ip_cache_ifs.is_open()) {
    info("found cached ip address");
    json j;
    ip_cache_ifs >> j;
    addr = j["ip"].get<std::string>();
    addr.resize(addrsize, ' ');
  }

  

  if (!GetUserInputs("kiwi setup", 1, "Enter iPhone IP address: ", addr.data(), addr.size())) {
    ShowConsoleMsg("kiwi: failed to get IP address");
    return "";
  }

  // // tell the user what the ip address is
  // std::string msg = std::string("kiwi: REAPER IP address ") + get_local_IP();
  // ShowConsoleMsg(msg.c_str());

  // clean the IP address string
  addr = std::string(addr.c_str()); // this is needed bc reaper adds a null terminator before the string actually ends
  // remove whitespace
  addr = trim(addr, " ");
  if (!validateIP(addr)) {
    ShowConsoleMsg("kiwi: invalid IP address");
    return "";
  }

  // cache the IP address if we validated successfully
  std::ofstream ip_cache_ofs(ip_address_cache_file);
  if (ip_cache_ofs.is_open()) {
    info("caching ip address");
    json j;
    j["ip"] = addr;
    ip_cache_ofs << j;
  }

  return addr;
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  if(!rec)
    return 0;

  if(rec->caller_version != REAPER_PLUGIN_VERSION)
    return 0;

  // register api functions that we want
  REG_FUNC(adjustZoom, rec);
  REG_FUNC(AudioAccessorStateChanged, rec);
  REG_FUNC(AudioAccessorUpdate, rec);
  REG_FUNC(AudioAccessorValidateState, rec);
  REG_FUNC(AudioAccessorUpdate, rec);
  REG_FUNC(CreateTrackAudioAccessor, rec);
  REG_FUNC(CSurf_OnZoom, rec);
  REG_FUNC(DestroyAudioAccessor, rec);
  REG_FUNC(EnumProjects, rec);
  REG_FUNC(GetAudioAccessorEndTime, rec);
  REG_FUNC(GetAudioAccessorStartTime, rec);
  REG_FUNC(GetAudioAccessorSamples, rec);
  REG_FUNC(GetCursorPosition, rec);
  REG_FUNC(GetHZoomLevel, rec);
  REG_FUNC(GetResourcePath, rec);
  REG_FUNC(MoveEditCursor, rec);
  REG_FUNC(ShowConsoleMsg, rec);
  REG_FUNC(SetEditCurPos, rec);
  
  // includes the master track
  REG_FUNC(GetSelectedTrack2, rec);
  REG_FUNC(CountSelectedTracks2, rec);
  REG_FUNC(CountSelectedMediaItems, rec);
  REG_FUNC(GetSetProjectInfo, rec);

  REG_FUNC(GetMediaSourceNumChannels, rec);
  REG_FUNC(GetMediaSourceSampleRate, rec);
  REG_FUNC(GetMediaTrackInfo_Value, rec);
  REG_FUNC(GetSetMediaItemInfo, rec);
  REG_FUNC(GetMediaItemInfo_Value, rec);

  REG_FUNC(GetSelectedMediaItem, rec);
  REG_FUNC(GetActiveTake, rec);
  REG_FUNC(GetMediaItemTake_Peaks, rec);
  REG_FUNC(GetMediaItemTake_Source, rec);
  REG_FUNC(GetMediaItem_Track, rec);
  REG_FUNC(PCM_Source_GetPeaks, rec);
  REG_FUNC(SetMediaItemTakeInfo_Value, rec);
  REG_FUNC(SetMediaItemInfo_Value, rec);
  REG_FUNC(GetTrack, rec);
  REG_FUNC(GetMasterTrack, rec);
  REG_FUNC(GetAudioAccessorHash, rec);
  REG_FUNC(AudioAccessorValidateState, rec);

  REG_FUNC(ValidatePtr2, rec);
  REG_FUNC(GetUserInputs, rec);
  REG_FUNC(Track_GetPeakInfo, rec);

  // create log file
  std::string resource_path = GetResourcePath();
  std::string log_path = resource_path + "/kiwi-log.txt";
  kiwi_logger_init(log_path);

  // print our log path and resource path
  info("kiwi: log path: %s", log_path.c_str());
  info("kiwi: resource path: %s", resource_path.c_str());


  // check if we have permission to write to the resource path
  // try opening a new file in the resource path

  std::string test_file_path = resource_path + "/kiwi-test.txt";
  std::ofstream test_file(test_file_path);
  if (!test_file.is_open()) {
    ShowConsoleMsg("kiwi: failed to open test file in resource path. Please check permissions");
  }


  

  std::string ADDRESS = get_ip_address(resource_path);

  // register kiwi connection action
  CONNECTION_ACTION_ID = rec->Register("command_id", (void*)"KiwiConnectionStatus");
  gaccel_register_t accelerator;
  accelerator.accel.fVirt = 0;
  accelerator.accel.key = 0;
  accelerator.accel.cmd = CONNECTION_ACTION_ID;
  accelerator.desc = "Check connection status of kiwi haptic interface";
  if (!rec->Register("gaccel", &accelerator)) 
    return 0;

  if (!rec->Register("hookcommand", (void *)&kiwi_connection_status)) 
    return 0;

  // create controller
  controller = new osc_controller_t(ADDRESS, SEND_PORT, RECV_PORT);
  if (!controller->init()) {
    ShowConsoleMsg("kiwi: failed to initialize OSC controller. OSC Connection failed\n");
    return 0;
  }
  // register action hooks
  if (!rec->Register("csurf_inst", (void *)controller))
    return 0;

  // initialization code here

  return 1;
}


