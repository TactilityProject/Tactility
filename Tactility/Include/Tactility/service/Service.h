#pragma once

#include <tactility/service/service_instance.h>

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

template<typename T>
void* create(const ::ServiceManifest* /*manifest*/) { return new std::shared_ptr<Service>(std::shared_ptr<T>(new T)); }

}