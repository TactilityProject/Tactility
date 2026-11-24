#include "doctest.h"

#include <Tactility/settings/DisplaySettings.h>

#include <tactility/delay.h>
#include <tactility/file_ops.h>
#include <tactility/paths.h>

#include <atomic>

using namespace tt::settings::display;

namespace {

struct DataPathOverride {
    DataPathOverride() { paths_set_root_path_override("data"); }
    ~DataPathOverride() { paths_set_root_path_override(nullptr); }
};

struct FileOpsBlocker {
    TaskEventGroup eventGroup {};
    FileOpsSubscription subscription {};
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };
    std::atomic<bool> finished { false };
    std::atomic<bool> markerFinished { false };
    bool subscribed = false;
    bool queued = false;
    bool markerQueued = false;

    FileOpsBlocker() {
        task_event_group_construct(&eventGroup);
        subscribed = file_ops_subscribe(&subscription, &eventGroup) == ERROR_NONE;
    }

    ~FileOpsBlocker() {
        release.store(true);
        if (queued) {
            const auto& finishedFlag = markerQueued ? markerFinished : finished;
            for (int i = 0; i < 1000 && !finishedFlag.load(); ++i) {
                delay_millis(5);
            }
        }
        if (subscribed) {
            file_ops_unsubscribe(&subscription);
        }
        task_event_group_destruct(&eventGroup);
    }

    bool start() {
        if (!subscribed || file_ops_queue(block, this, nullptr) != ERROR_NONE) {
            return false;
        }
        queued = true;
        for (int i = 0; i < 1000 && !entered.load(); ++i) {
            delay_millis(5);
        }
        return entered.load();
    }

    bool waitUntilIdle() {
        markerFinished.store(false);
        if (!subscribed || file_ops_queue(markFinished, this, &subscription) != ERROR_NONE) {
            return false;
        }
        markerQueued = true;
        return file_ops_await(&subscription, pdMS_TO_TICKS(5000)) == ERROR_NONE && markerFinished.load();
    }

    static void block(void* context) {
        auto* blocker = static_cast<FileOpsBlocker*>(context);
        blocker->entered.store(true);
        while (!blocker->release.load()) {
            delay_millis(1);
        }
        blocker->finished.store(true);
    }

    static void markFinished(void* context) {
        static_cast<FileOpsBlocker*>(context)->markerFinished.store(true);
    }
};

}

TEST_CASE("display brightness saves coalesce and preserve orientation settings") {
    DataPathOverride dataPathOverride;
    DisplaySettings initial {
        .orientation = Orientation::Landscape,
        .gammaCurve = 3,
        .backlightDuty = 200,
        .autoRotateEnabled = false
    };
    REQUIRE(save(initial));

    FileOpsBlocker blocker;
    REQUIRE(blocker.start());
    REQUIRE(queueSaveBacklightDuty(64));
    REQUIRE(queueSaveBacklightDuty(50));

    REQUIRE(saveOrientationAndAutoRotate(Orientation::Portrait, true));
    blocker.release.store(true);
    REQUIRE(blocker.waitUntilIdle());

    DisplaySettings loaded;
    REQUIRE(load(loaded));
    CHECK_EQ(loaded.orientation, Orientation::Portrait);
    CHECK(loaded.autoRotateEnabled);
    CHECK_EQ(loaded.backlightDuty, 50);
    CHECK_EQ(loaded.gammaCurve, 3);

    REQUIRE(saveOrientationAndAutoRotate(Orientation::LandscapeFlipped, false));
    REQUIRE(queueSaveBacklightDuty(128));
    REQUIRE(blocker.waitUntilIdle());

    REQUIRE(load(loaded));
    CHECK_EQ(loaded.orientation, Orientation::LandscapeFlipped);
    CHECK_FALSE(loaded.autoRotateEnabled);
    CHECK_EQ(loaded.backlightDuty, 128);
    CHECK_EQ(loaded.gammaCurve, 3);
}