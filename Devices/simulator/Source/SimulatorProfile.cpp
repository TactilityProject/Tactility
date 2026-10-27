#include "SimulatorProfile.h"

#include "lv_conf.h"

#include <tactility/log.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace simulator {

constexpr auto* TAG = "SimulatorProfile";
constexpr auto* PROFILE_DIRECTORY = "data/tactility";
constexpr auto* PROFILE_PATH = "data/tactility/simulator_profile.txt";
constexpr auto* DISPLAY_SETTINGS_DIRECTORY = "data/tactility/user/app/tactility.display";
constexpr auto* DISPLAY_SETTINGS_PATH = "data/tactility/user/app/tactility.display/display.properties";
constexpr auto* ORIENTATION_KEY = "orientation=";

static const Profile PROFILES[PROFILE_COUNT] = {
    { "Default", 640, 320, LV_DPI_DEF, false, false, false, true, true },
    { "CL-32", 384, 168, 139, false, true, true, true, false },
    { "LilyGO T-Deck", 320, 240, 143, false, false, false, true, true },
    { "LilyGO T-Lora Pager", 480, 222,  150, false, false, false, true, false },
    { "M5Stack Cardputer", 240, 135, 139, false, true, false, true, false },
    { "M5Stack Tab5",  1280, 720, 250, true, false, false, true, true },
};

static size_t activeProfileIndex = 0;
static OrientationOverride activeOrientation = OrientationOverride::Default;
static char** processArguments = nullptr;
/** Absolute, because argv[0] can be a bare name that was found through PATH. Empty when it can't be resolved. */
static std::string executablePath;

const Profile& getProfile(size_t index) {
    return PROFILES[index < PROFILE_COUNT ? index : 0];
}

static const char* toString(OrientationOverride orientation) {
    switch (orientation) {
        case OrientationOverride::Landscape:
            return "landscape";
        case OrientationOverride::Portrait:
            return "portrait";
        default:
            return "default";
    }
}

/** The first line has the profile name, the optional second line the orientation */
static void loadSelection(size_t& profileIndex, OrientationOverride& orientation) {
    profileIndex = 0;
    orientation = OrientationOverride::Default;
    FILE* file = fopen(PROFILE_PATH, "r");
    if (file == nullptr) {
        return;
    }
    char name[64] = {};
    char orientationText[16] = {};
    const bool read = fgets(name, sizeof(name), file) != nullptr;
    if (read) {
        fgets(orientationText, sizeof(orientationText), file);
    }
    fclose(file);
    if (!read) {
        return;
    }
    name[strcspn(name, "\r\n")] = '\0';
    orientationText[strcspn(orientationText, "\r\n")] = '\0';
    for (size_t i = 0; i < PROFILE_COUNT; i++) {
        if (strcmp(PROFILES[i].name, name) == 0) {
            profileIndex = i;
        }
    }
    for (auto candidate : { OrientationOverride::Landscape, OrientationOverride::Portrait }) {
        if (strcmp(toString(candidate), orientationText) == 0) {
            orientation = candidate;
        }
    }
}

size_t loadSelectedProfileIndex() {
    size_t profileIndex;
    OrientationOverride orientation;
    loadSelection(profileIndex, orientation);
    return profileIndex;
}

OrientationOverride loadSelectedOrientation() {
    size_t profileIndex;
    OrientationOverride orientation;
    loadSelection(profileIndex, orientation);
    return orientation;
}

static const char* toDisplaySettingsValue(OrientationOverride orientation) {
    return orientation == OrientationOverride::Portrait ? "Portrait" : "Landscape";
}

/** Creates the folders of a path, like "mkdir -p" */
static void makeDirectories(const std::string& path) {
    for (size_t position = path.find('/'); position != std::string::npos; position = path.find('/', position + 1)) {
        mkdir(path.substr(0, position).c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

/**
 * The Display settings apply their saved orientation to the display. Writes the override there,
 * or removes the saved orientation so that the display's own orientation applies.
 */
static void applyOrientationToDisplaySettings(OrientationOverride orientation) {
    std::string kept;
    if (FILE* file = fopen(DISPLAY_SETTINGS_PATH, "r"); file != nullptr) {
        char line[256];
        while (fgets(line, sizeof(line), file) != nullptr) {
            if (strncmp(line, ORIENTATION_KEY, strlen(ORIENTATION_KEY)) != 0) {
                kept += line;
            }
        }
        fclose(file);
    }
    if (orientation != OrientationOverride::Default) {
        kept += std::string(ORIENTATION_KEY) + toDisplaySettingsValue(orientation) + "\n";
    } else if (kept.empty() && access(DISPLAY_SETTINGS_PATH, F_OK) != 0) {
        return;
    }

    makeDirectories(DISPLAY_SETTINGS_DIRECTORY);
    FILE* file = fopen(DISPLAY_SETTINGS_PATH, "w");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to write %s", DISPLAY_SETTINGS_PATH);
        return;
    }
    fputs(kept.c_str(), file);
    fclose(file);
}

bool saveSelection(size_t profileIndex, OrientationOverride orientation) {
    size_t savedProfileIndex;
    OrientationOverride savedOrientation;
    loadSelection(savedProfileIndex, savedOrientation);
    const bool changed = profileIndex != savedProfileIndex || orientation != savedOrientation;
    mkdir(PROFILE_DIRECTORY, 0755);
    FILE* file = fopen(PROFILE_PATH, "w");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to write %s", PROFILE_PATH);
        return false;
    }
    const bool written = fprintf(file, "%s\n%s\n", getProfile(profileIndex).name, toString(orientation)) > 0;
    if (fclose(file) != 0 || !written) {
        return false;
    }
    if (changed) {
        applyOrientationToDisplaySettings(orientation);
    }
    return true;
}

void initActiveProfile() {
    loadSelection(activeProfileIndex, activeOrientation);
    LOG_I(TAG, "Profile: %s, orientation: %s", getProfile(activeProfileIndex).name, toString(activeOrientation));
}

size_t getActiveProfileIndex() {
    return activeProfileIndex;
}

OrientationOverride getActiveOrientation() {
    return activeOrientation;
}

static std::string resolveExecutablePath(const char* argv0) {
    char path[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof(path);
    char resolved[PATH_MAX];
    if (_NSGetExecutablePath(path, &size) == 0 && realpath(path, resolved) != nullptr) {
        return resolved;
    }
#else
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length > 0) {
        path[length] = '\0';
        return path;
    }
#endif
    // Only a path with a slash can be resolved without searching PATH
    if (strchr(argv0, '/') != nullptr && realpath(argv0, path) != nullptr) {
        return path;
    }
    return "";
}

void setProcessArguments(char** argv) {
    processArguments = argv;
    executablePath = resolveExecutablePath(argv[0]);
}

void reboot() {
    if (processArguments == nullptr) {
        LOG_E(TAG, "Cannot reboot without process arguments");
        return;
    }
    // The FreeRTOS tick timer would outlive exec() and kill the new image with SIGALRM
    itimerval previousTimer = {};
    itimerval stopTimer = {};
    setitimer(ITIMER_REAL, &stopTimer, &previousTimer);
    if (!executablePath.empty()) {
        execv(executablePath.c_str(), processArguments);
    } else {
        execvp(processArguments[0], processArguments);
    }
    LOG_E(TAG, "Failed to reboot: %s", strerror(errno));
    // This process keeps running, so it needs its ticks again
    setitimer(ITIMER_REAL, &previousTimer, nullptr);
}

}
