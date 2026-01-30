#include "tt_app_alertdialog.h"
#include <Tactility/app/alertdialog/AlertDialog.h>

extern "C" {

AppLaunchId tt_app_alertdialog_start(const char* title, const char* message, const char* buttonLabels[], uint32_t buttonLabelCount) {
    std::vector<std::string> list;
    for (int i = 0; i < buttonLabelCount; i++) {
        list.emplace_back(buttonLabels[i]);
    }
    return tt::app::alertdialog::start(title, message, list);
}

int32_t tt_app_alertdialog_get_result_index(BundleHandle handle) {
    return tt::app::alertdialog::getResultIndex(*(tt::Bundle*)handle);
}

}
