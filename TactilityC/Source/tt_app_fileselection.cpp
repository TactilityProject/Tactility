#include "tt_app_fileselection.h"
#include <Tactility/app/App.h>
#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/Bundle.h>

#include <cstring>
#include <string>

extern "C" {

AppLaunchId tt_app_fileselection_start_for_existing_file() {
    return tt::app::fileselection::startForExistingFile();
}

AppLaunchId tt_app_fileselection_start_for_existing_or_new_file() {
    return tt::app::fileselection::startForExistingOrNewFile();
}

bool tt_app_fileselection_get_result_path(BundleHandle handle, char* buffer, uint32_t bufferSize) {
    auto path = tt::app::fileselection::getResultPath(*(tt::Bundle*)handle);
    if (path.empty() || bufferSize == 0) {
        return false;
    }
    strncpy(buffer, path.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

}
