#include "doctest.h"

#include <tactility/delay.h>
#include <tactility/file_ops.h>

#include <atomic>

namespace {

struct Recorder {
    std::atomic<int> order[8];
    std::atomic<int> count { 0 };
    /** Set after the last write of the work, so a test can wait for the work to finish with the recorder */
    std::atomic<int> done { 0 };
};

struct Job {
    Recorder* recorder;
    int id;
};

void record(void* context) {
    auto* job = static_cast<Job*>(context);
    const int index = job->recorder->count.fetch_add(1);
    job->recorder->order[index] = job->id;
    job->recorder->done.fetch_add(1);
}

void block_briefly(void* context) {
    delay_ticks(5);
    record(context);
}

}

TEST_CASE("file_ops_await returns when the queued work has completed") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    Recorder recorder;
    Job job { &recorder, 1 };
    CHECK_EQ(file_ops_queue(record, &job, &sub), ERROR_NONE);
    CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(2000)), ERROR_NONE);
    CHECK_EQ(recorder.count.load(), 1);
    CHECK_EQ(file_ops_poll(&sub), ERROR_TIMEOUT);

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}

TEST_CASE("file_ops runs work in the order it was queued, with one completion for each") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    Recorder recorder;
    Job jobs[3] = { { &recorder, 1 }, { &recorder, 2 }, { &recorder, 3 } };
    for (auto& job : jobs) {
        CHECK_EQ(file_ops_queue(block_briefly, &job, &sub), ERROR_NONE);
    }
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(2000)), ERROR_NONE);
    }
    CHECK_EQ(recorder.order[0].load(), 1);
    CHECK_EQ(recorder.order[1].load(), 2);
    CHECK_EQ(recorder.order[2].load(), 3);
    CHECK_EQ(file_ops_poll(&sub), ERROR_TIMEOUT);

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}

TEST_CASE("file_ops starts a new task after the queue was emptied") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    Recorder recorder;
    for (int i = 0; i < 3; i++) {
        Job job { &recorder, i };
        CHECK_EQ(file_ops_queue(record, &job, &sub), ERROR_NONE);
        CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(2000)), ERROR_NONE);
        delay_ticks(5);
    }
    CHECK_EQ(recorder.count.load(), 3);

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}

TEST_CASE("file_ops runs work without a subscription") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    Recorder recorder;
    Job first { &recorder, 1 };
    Job last { &recorder, 2 };
    CHECK_EQ(file_ops_queue(block_briefly, &first, nullptr), ERROR_NONE);
    CHECK_EQ(file_ops_queue(record, &last, &sub), ERROR_NONE);
    CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(2000)), ERROR_NONE);
    CHECK_EQ(recorder.count.load(), 2);

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}

TEST_CASE("file_ops_unsubscribe is safe while work is still queued") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    Recorder recorder;
    Job job { &recorder, 1 };
    CHECK_EQ(file_ops_queue(block_briefly, &job, &sub), ERROR_NONE);
    CHECK_EQ(file_ops_unsubscribe(&sub), ERROR_NONE);
    CHECK_EQ(file_ops_unsubscribe(&sub), ERROR_NOT_FOUND);

    // The bit is free again immediately, and the work still runs
    uint32_t bit;
    CHECK_EQ(task_event_group_claim_bit(&group, &bit), ERROR_NONE);
    task_event_group_release_bit(&group, bit);
    while (recorder.done.load() == 0) {
        delay_ticks(1);
    }

    task_event_group_destruct(&group);
}

TEST_CASE("file_ops_queue rejects a subscription that isn't subscribed") {
    FileOpsSubscription sub {};
    Recorder recorder;
    Job job { &recorder, 1 };
    CHECK_EQ(file_ops_queue(record, &job, &sub), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("file_ops_await times out when nothing was queued") {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(20)), ERROR_TIMEOUT);

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}

namespace {

struct WorkerProbe {
    std::atomic<int> insideWorker { -1 };
};

void probe_worker(void* context) {
    static_cast<WorkerProbe*>(context)->insideWorker = file_ops_is_worker() ? 1 : 0;
}

}

TEST_CASE("file_ops_is_worker is only true inside queued work") {
    CHECK_FALSE(file_ops_is_worker());

    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription sub {};
    REQUIRE_EQ(file_ops_subscribe(&sub, &group), ERROR_NONE);

    WorkerProbe probe;
    CHECK_EQ(file_ops_queue(probe_worker, &probe, &sub), ERROR_NONE);
    CHECK_EQ(file_ops_await(&sub, pdMS_TO_TICKS(2000)), ERROR_NONE);
    CHECK_EQ(probe.insideWorker.load(), 1);
    CHECK_FALSE(file_ops_is_worker());

    file_ops_unsubscribe(&sub);
    task_event_group_destruct(&group);
}
