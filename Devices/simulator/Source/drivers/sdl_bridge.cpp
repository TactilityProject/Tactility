// SPDX-License-Identifier: Apache-2.0
#include "sdl_bridge.h"
#include "sdl_input.h"

#include <tactility/error.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

struct PresentJob {
    Device* device;
    void* internal;
    int32_t x_start;
    int32_t y_start;
    int32_t x_end;
    int32_t y_end;
    const void* color_data;
    error_t result;
};

std::mutex job_mutex;
std::condition_variable job_ready_cv;
std::condition_variable job_done_cv;
bool job_pending = false;
bool job_done = false;
PresentJob pending_job;

}

error_t sdl_bridge_present(Device* device, void* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    std::unique_lock<std::mutex> lock(job_mutex);

    pending_job = { device, internal, x_start, y_start, x_end, y_end, color_data, ERROR_NONE };
    job_pending = true;
    job_done = false;
    job_ready_cv.notify_one();

    job_done_cv.wait(lock, [] { return job_done; });
    return pending_job.result;
}

void sdl_bridge_run_main_loop() {
    while (true) {
        sdl_input_pump();

        std::unique_lock<std::mutex> lock(job_mutex);
        if (job_ready_cv.wait_for(lock, std::chrono::milliseconds(1), [] { return job_pending; })) {
            PresentJob job = pending_job;
            lock.unlock();

            job.result = sdl_display_execute_draw_bitmap(job.device, job.internal, job.x_start, job.y_start, job.x_end, job.y_end, job.color_data);

            lock.lock();
            pending_job.result = job.result;
            job_pending = false;
            job_done = true;
            lock.unlock();
            job_done_cv.notify_one();
        }
    }
}
