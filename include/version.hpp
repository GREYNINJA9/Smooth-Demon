#pragma once

//SmoothDemon version constants.

#define SMOOTHDEMON_VERSION_MAJOR 0
#define SMOOTHDEMON_VERSION_MINOR 1
#define SMOOTHDEMON_VERSION_PATCH 0

#define SMOOTHDEMON_VERSION_STRING "0.1.0"

namespace smoothdemon {
    inline constexpr int version_major = SMOOTHDEMON_VERSION_MAJOR;
    inline constexpr int version_minor = SMOOTHDEMON_VERSION_MINOR;
    inline constexpr int version_patch = SMOOTHDEMON_VERSION_PATCH;

    inline constexpr const char* version_string = SMOOTHDEMON_VERSION_STRING;
}
