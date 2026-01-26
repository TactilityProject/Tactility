#include <Tactility/app/apphub/AppHub.h>

#include <format>

namespace tt::app::apphub {

constexpr auto* BASE_URL = "https://cdn.tactilityproject.org/apps";

static std::string getVersionWithoutPostfix() {
    std::string version(TT_VERSION);
    auto index = version.find_first_of('-');
    if (index == std::string::npos) {
        return version;
    } else {
        return version.substr(0, index);
    }
}

std::string getAppsJsonUrl() {
    return std::format("{}/{}/apps.json", BASE_URL, getVersionWithoutPostfix());
}

std::string getDownloadUrl(const std::string& relativePath) {
    return std::format("{}/{}/{}", BASE_URL, getVersionWithoutPostfix(), relativePath);
}

}
