cmake_minimum_required(VERSION 3.14)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# set project name and version
project(kiwi VERSION 0.0.1)

# need FindWDL
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# find the config dir
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(USER_CONFIG_DIR "$ENV{APPDATA}")
  elseif(APPLE)
    set(USER_CONFIG_DIR "~/Library/Application Support")
  else()
    set(USER_CONFIG_DIR "~/.config")
  endif()

  set(CMAKE_INSTALL_PREFIX "${USER_CONFIG_DIR}/REAPER" CACHE PATH
    "REAPER resource path where to install SWS" FORCE)
endif()


# get an architecture name
if(CMAKE_OSX_ARCHITECTURES)
  list(JOIN CMAKE_OSX_ARCHITECTURES "-" ARCH_NAME)
elseif(MSVC_CXX_ARCHITECTURE_ID)
  set(ARCH_NAME ${MSVC_CXX_ARCHITECTURE_ID})
else()
  set(ARCH_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

# lower architecture name
STRING(TOLOWER "${ARCH_NAME}" ARCH_NAME)

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME kiwi) # avoid an empty "Unspecified" component


add_library(kiwi SHARED
    include/oscpkt/oscpkt.hh
    include/oscpkt/udp.hh
    src/osc.h
    src/main.cpp
)
  
target_include_directories(kiwi PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/reaper-sdk/sdk
  ${CMAKE_CURRENT_BINARY_DIR}
)

# add WDL
# list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/wdl)
find_package(WDL REQUIRED)
target_link_libraries(kiwi WDL::WDL)
set_target_properties(kiwi PROPERTIES
  PREFIX "" # disable the "lib" prefix
  OUTPUT_NAME "reaper_kiwi-${ARCH_NAME}"
)

set(REAPER_USER_PLUGINS "UserPlugins")

if(NO_INSTALL_PREFIX)
  # Used for .dmg generation to avoid getting directories
  # Maybe there's a better way?
  set(REAPER_USER_PLUGINS ".")
  set(REAPER_SCRIPTS      ".")
endif()

install(TARGETS kiwi
  COMPONENT kiwi
  RUNTIME DESTINATION "${REAPER_USER_PLUGINS}" # Windows .dll
  LIBRARY DESTINATION "${REAPER_USER_PLUGINS}" # macOS .dylib/Linux .so
)
