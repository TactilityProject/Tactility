#pragma once

#include <Tactility/service/Service.h>

#include <string>

namespace tt::service {

// Forward declarations
class ServiceContext;

using CreateService = ::ServiceCreate;

/** A ledger that describes the main parts of a service. */
struct ServiceManifest {
    /** The identifier by which the app is launched by the system and other apps. */
    std::string id {};

    /** Create the instance of the app */
    CreateService createService = nullptr;
};

} // namespace
