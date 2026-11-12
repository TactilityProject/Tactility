#pragma once

namespace tt::app::boot {

/**
 * Runs the .sh scripts of the "boot" folder in the user data path, one after the other in alphabetical order.
 * Each script runs in the shell, and its output is written to the log.
 * Returns when the last script has ended. Does nothing when the folder doesn't exist.
 */
void runBootScripts();

}
