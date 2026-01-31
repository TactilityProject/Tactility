#include "Tactility/MountPoints.h"

#include "Tactility/TactilityConfig.h"
#include <tactility/hal/Device.h>
#include "Tactility/hal/sdcard/SdCardDevice.h"

#include <Tactility/file/File.h>

#include <cstring>
#include <vector>
#include <dirent.h>

namespace tt::file {

std::vector<dirent> getMountPoints() {
    std::vector<dirent> dir_entries;
    dir_entries.clear();

    // Data partition
    auto data_dirent = dirent{
        .d_ino = 1,
        .d_type = TT_DT_DIR,
        .d_name = { 0 }
    };
    strcpy(data_dirent.d_name, DATA_PARTITION_NAME);
    dir_entries.push_back(data_dirent);

    // SD card partitions
    auto sdcards = tt::hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (auto& sdcard : sdcards) {
        auto state = sdcard->getState();
        if (state == hal::sdcard::SdCardDevice::State::Mounted) {
            auto mount_name = sdcard->getMountPath().substr(1);
            auto dir_entry = dirent {
                .d_ino = 2,
                .d_type = TT_DT_DIR,
                .d_name = { 0 }
            };
            assert(mount_name.length() < sizeof(dirent::d_name));
            strcpy(dir_entry.d_name, mount_name.c_str());
            dir_entries.push_back(dir_entry);
        }
    }

    if (config::SHOW_SYSTEM_PARTITION) {
        // System partition
        auto system_dirent = dirent{
            .d_ino = 0,
            .d_type = TT_DT_DIR,
            .d_name = { 0 }
        };
        strcpy(system_dirent.d_name, SYSTEM_PARTITION_NAME);
        dir_entries.push_back(system_dirent);
    }

    return dir_entries;
}

}
