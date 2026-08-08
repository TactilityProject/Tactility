#pragma once

#include "./View.h"
#include "./State.h"

#include <Tactility/PubSub.h>
#include <Tactility/Mutex.h>
#include <Tactility/service/wifi/Wifi.h>

// Context (the app's actual runtime state) is defined inside WifiManage.cpp's own anonymous
// namespace - View.cpp doesn't need it (Bindings*/State* pointers and a raw appInstanceId are
// threaded through explicitly instead, see View.h/View.cpp). This header now only bundles
// View.h/State.h for convenience, same as before.
