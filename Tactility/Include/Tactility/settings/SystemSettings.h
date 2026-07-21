#pragma once

#include "Language.h"

namespace tt::settings {

struct SystemSettings {
    Language language = Language::en_US;
    bool timeFormat24h = true;
    std::string dateFormat = std::string("DD/MM/YYYY"); // MM/DD/YYYY, DD/MM/YYYY, YYYY-MM-DD, YYYY/MM/DD
};

bool loadSystemSettings(SystemSettings& properties);

bool saveSystemSettings(const SystemSettings& properties);

}
