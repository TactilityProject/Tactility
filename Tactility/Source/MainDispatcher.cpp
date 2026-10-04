#include <Tactility/Tactility.h>
#include <Tactility/TactilityPrivate.h>

namespace tt {

static DispatcherHandle_t mainDispatcherHandle = dispatcher_alloc();

namespace {

void mainDispatcherTrampoline(void* context) {
    auto* function = static_cast<MainDispatcher::Function*>(context);
    (*function)();
    delete function;
}

} // namespace

bool MainDispatcher::dispatch(Function function, TickType_t timeout) const {
    auto* boxed = new Function(std::move(function));
    if (dispatcher_dispatch_timed(handle, boxed, mainDispatcherTrampoline, timeout) != ERROR_NONE) {
        delete boxed;
        return false;
    }
    return true;
}

MainDispatcher getMainDispatcher() {
    return MainDispatcher(mainDispatcherHandle);
}

DispatcherHandle_t getMainDispatcherHandle() {
    return mainDispatcherHandle;
}

} // namespace
