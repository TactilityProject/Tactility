#pragma once

#include "Main.h"
#include "drivers/sdl_bridge.h"

#include <csignal>
#include <pthread.h>
#include <thread>

namespace simulator {
    /** Set the function pointer of the real app_main() */
    void setMain(MainFunction mainFunction);
    /** The actual main task */
    void freertosMain();
}

extern "C" {
void app_main(); // ESP-IDF's main function, implemented in the application
}

int main() {
    // The FreeRTOS POSIX port arms a process-wide SIGALRM timer for its tick and expects every one
    // of its task pthreads to have all signals but SIGINT blocked.
    // (see prvSetupSignalsAndSchedulerPolicy() in FreeRTOS-Kernel's Posix port.c)
    // A signal-generated SIGALRM can land on any thread in the process that doesn't block it.
    // This thread stays a plain OS thread (running the SDL loop below, never a FreeRTOS task),
    // so without this it's eligible to catch a tick SIGALRM and freeze inside the scheduler's handler.
    // Block the same set here, before anything else, so it never can.
    sigset_t all_signals_except_sigint;
    sigfillset(&all_signals_except_sigint);
    sigdelset(&all_signals_except_sigint, SIGINT);
    pthread_sigmask(SIG_SETMASK, &all_signals_except_sigint, nullptr);

    // FreeRTOS and app_main() run on a separate thread: macOS requires SDL/Cocoa window creation,
    // event pumping and rendering to happen on the real OS main thread, which sdl_bridge_run_main_loop()
    // below takes over. freertosMain() never returns, so this thread is detached rather than joined.
    simulator::setMain(app_main);
    std::thread(simulator::freertosMain).detach();
    sdl_bridge_run_main_loop();
    return 0;
}
