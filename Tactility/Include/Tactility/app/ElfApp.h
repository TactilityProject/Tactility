#pragma once

#include "AppManifest.h"

#ifdef ESP_PLATFORM

namespace tt::app {

typedef void* (*CreateData)();
typedef void (*DestroyData)(void* data);
/** data is nullable */
typedef void (*OnCreate)(void* appContext, void* data);
/** data is nullable */
typedef void (*OnDestroy)(void* appContext, void* data);
/** data is nullable */
typedef void (*OnShow)(void* appContext, void* data, lv_obj_t* parent);
/** data is nullable */
typedef void (*OnHide)(void* appContext, void* data);
/** data is nullable, resultData is nullable. */
typedef void (*OnResult)(void* appContext, void* data, LaunchId launchId, Result result, Bundle* resultData);

/** All fields are nullable */
void setElfAppParameters(
    CreateData createData,
    DestroyData destroyData,
    OnCreate onCreate,
    OnDestroy onDestroy,
    OnShow onShow,
    OnHide onHide,
    OnResult onResult
);

std::shared_ptr<App> createElfApp(const std::shared_ptr<AppManifest>& manifest);

}
#endif // ESP_PLATFORM
