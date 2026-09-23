#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <Tactility/app/shell/ShellFs.h>

#include <tactility/filesystem/file_system.h>

#include <cstdio>

int cmdDf(int, char**) {
    // Only mount points are listed: ESP-IDF's VFS has no statvfs and Tactility's FileSystem API
    // exposes paths but not capacity, so there is no honest way to report free space here.
    printf("Mounted filesystems:\n");
    file_system_for_each_mounted(nullptr, printMount);
    return 0;
}
