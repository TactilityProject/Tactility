#pragma once

#include <string>

#include <tactility/filesystem/file_system.h>


namespace tt {

bool findFirstMountedSdCardPath(std::string& path);

FileSystem* findSdcardFileSystem(bool mustBeMounted);

std::string getSystemRootPath();

std::string getTempPath();

std::string getAppInstallPath();

std::string getAppInstallPath(const std::string& appId);

std::string getUserPath();

std::string getAppUserPath(const std::string& appId);

}
