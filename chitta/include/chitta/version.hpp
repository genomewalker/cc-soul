#pragma once

#define CHITTA_VERSION "2.58.1"
#define CHITTA_PROTOCOL_VERSION_MAJOR 1
#define CHITTA_PROTOCOL_VERSION_MINOR 0

namespace chitta {
namespace version {

inline bool protocol_compatible(int major, int minor) {
    return major == CHITTA_PROTOCOL_VERSION_MAJOR &&
           minor >= CHITTA_PROTOCOL_VERSION_MINOR;
}

} // namespace version
} // namespace chitta
