#pragma once

#include "Language.h"

namespace tt::settings {

struct SystemSettings {
    Language language;
    bool timeFormat24h;
    std::string dateFormat;  // MM/DD/YYYY, DD/MM/YYYY, YYYY-MM-DD, YYYY/MM/DD
};

bool loadSystemSettings(SystemSettings& properties);

bool saveSystemSettings(const SystemSettings& properties);

}
