#pragma once

#include <service/instance.h>

#include <tactility/Allocator.hpp>
#include <tactility/Memory.hpp>

#include <memory>

namespace tt::service {

using State = ::ServiceState;

// Forward declaration
class ServiceContext;

class Service {

public:

    Service() = default;
    virtual ~Service() = default;

    virtual bool onStart(ServiceContext& serviceContext) { return true; }
    virtual void onStop(ServiceContext& serviceContext) {}
};

/** Creates a service for ServiceManifest::createService. The result is released with deleteObject(), see ServiceManifest. */
template<typename T>
void* create(const ::ServiceManifest* /*manifest*/) {
    return newObject<::std::shared_ptr<Service>>(::std::allocate_shared<T>(OptExternalAllocator<T>()));
}

}