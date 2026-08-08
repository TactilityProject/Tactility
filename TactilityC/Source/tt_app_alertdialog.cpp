#include "tt_app_alertdialog.h"

#include <Tactility/app/alertdialog/AlertDialog.h>

extern "C" {

AppInstanceId tt_app_alertdialog_start(AppInstanceId parent_id, const char* title, const char* message, const char* buttonLabels[], uint32_t buttonLabelCount) {
    std::vector<std::string> list;
    for (int i = 0; i < buttonLabelCount; i++) {
        list.emplace_back(buttonLabels[i]);
    }
    // TODO: Get caller app instance id from task context?
    return tt::app::alertdialog::start(parent_id, title, message, list);
}

}
