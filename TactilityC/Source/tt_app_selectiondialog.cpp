#include "tt_app_selectiondialog.h"
#include <Tactility/app/selectiondialog/SelectionDialog.h>

extern "C" {

AppLaunchId tt_app_selectiondialog_start(const char* title, int argc, const char* argv[]) {
    std::vector<std::string> list;
    for (int i = 0; i < argc; i++) {
        list.emplace_back(argv[i]);
    }
    return tt::app::selectiondialog::start(title, list);
}

int32_t tt_app_selectiondialog_get_result_index(BundleHandle handle) {
    return tt::app::selectiondialog::getResultIndex(*(tt::Bundle*)handle);
}

}
