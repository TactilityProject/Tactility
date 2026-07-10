#pragma once

#include <Tactility/SystemEvents.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/rtctime/RtcTime.h>

#include <memory>

struct Device;

namespace tt::service::rtctime {

class RtcTimeService final : public Service {

    kernel::SystemEventSubscription timeEventSubscription = 0;
    Device* rtcDevice = nullptr;

    Device* findRtcDevice();
    void onTimeChanged(kernel::SystemEvent event);

public:

    bool onStart(ServiceContext& serviceContext) override;
    void onStop(ServiceContext& serviceContext) override;

    bool isAvailable() const;
};

std::shared_ptr<RtcTimeService> findRtcTimeService();

} // namespace tt::service::rtctime
