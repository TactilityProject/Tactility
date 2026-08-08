#pragma once

#include "app/instance.h"


#include <cstdint>
#include <string>
#include <vector>

/**
 * Show a dialog with a title and a list of selectable items.
 */
namespace tt::app::selectiondialog {

/**
 * Show a selection dialog with the provided title and items, as a modal child of
 * @a callerAppInstanceId (a new-model app - see app/manager.h). The caller receives the
 * result as an APP_EVENT_RESULT in its own event loop: result is the selected item's index
 * (>= 0), -1 if 0 items were provided (an error - the dialog auto-closes without showing
 * anything), or a value not matching any item (currently always 1) if the dialog was
 * dismissed without a selection. No result_bundle. If exactly 1 item is provided, the dialog
 * auto-closes with result index 0 without showing anything. The caller is responsible for
 * calling app_manager_stop() on the returned instance id once it has handled the result.
 * @return the new dialog's app instance id
 */
AppInstanceId start(AppInstanceId callerAppInstanceId, const std::string& title, const std::vector<std::string>& items);

}
