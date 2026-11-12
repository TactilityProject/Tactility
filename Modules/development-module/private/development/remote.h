// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <development/session.h>

#include <app/event.h>

#include <tactility/concurrent/task_event_group.h>

/**
 * Streams the display to the client and applies its input until it disconnects or the app is
 * asked to close. Sends a DEVELOPMENT_FRAME_FAIL with the reason when remote access isn't possible.
 * @param[in,out] close_requested set when an APP_EVENT_CLOSE arrives
 */
void development_remote_run(DevelopmentSession* session, TaskEventGroup* event_group, AppEventSubscription* subscription, bool* close_requested);

