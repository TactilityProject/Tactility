#pragma once

namespace tt::network::ntp {

bool isSynced();

/** Persist the current time as the minimum time after the next boot */
void storeTimeInNvs();

}