#include "tt_app_fileselection.h"
#include <Tactility/app/fileselection/FileSelection.h>

#include <cstring>
#include <string>

extern "C" {

AppInstanceId tt_app_fileselection_start_for_existing_file(AppInstanceId app_id) {
    return tt::app::fileselection::startForExistingFile(app_id);
}

AppInstanceId tt_app_fileselection_start_for_existing_or_new_file(AppInstanceId app_id) {
    return tt::app::fileselection::startForExistingOrNewFile(app_id);
}

bool tt_app_fileselection_get_result_path(char* buffer, uint32_t bufferSize) {
    const std::string path = tt::app::fileselection::getLastPath();
    if (path.length() + 1 > bufferSize) {
        return false;
    }
    std::strcpy(buffer, path.c_str());
    return true;
}

}
