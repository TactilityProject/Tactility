#include "../Include/Tactility/Platform.h"

namespace tt::kernel {

Platform getPlatform() {
#ifdef ESP_PLATFORM
    return PlatformEsp;
#else
    return PlatformSimulator;
#endif
}

} // namespace
