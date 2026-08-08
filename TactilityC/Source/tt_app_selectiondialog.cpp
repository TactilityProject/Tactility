#include "tt_app_selectiondialog.h"
#include <Tactility/app/selectiondialog/SelectionDialog.h>

extern "C" {

AppInstanceId tt_app_selectiondialog_start(AppInstanceId parent_id, const char* title, int argc, const char* argv[]) {
    std::vector<std::string> list;
    for (int i = 0; i < argc; i++) {
        list.emplace_back(argv[i]);
    }
    return tt::app::selectiondialog::start(parent_id, title, list);
}

}
