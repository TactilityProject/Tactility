#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <tactility/log.h>

#include <utility>

namespace tt::settings {

constexpr auto* TAG = "Language";

void setLanguage(Language newLanguage) {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return;
    }

    properties.language = newLanguage;
    saveSystemSettings(properties);
}

Language getLanguage() {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return Language::en_US;
    } else {
        return properties.language;
    }
}

std::string toString(Language language) {
    switch (language) {
        case Language::en_GB:
            return "en-GB";
        case Language::en_US:
            return "en-US";
        case Language::fr_FR:
            return "fr-FR";
        case Language::nl_BE:
            return "nl-BE";
        case Language::nl_NL:
            return "nl-NL";
        default:
            LOG_E(TAG, "Missing serialization for language %d", static_cast<int>(language));
            std::unreachable();
    }
}

bool fromString(const std::string& text, Language& language) {
    if (text == "en-GB") {
        language = Language::en_GB;
    } else if (text == "en-US") {
        language = Language::en_US;
    } else if (text == "fr-FR") {
        language = Language::fr_FR;
    } else if (text == "nl-BE") {
        language = Language::nl_BE;
    } else if (text == "nl-NL") {
        language = Language::nl_NL;
    } else {
        return false;
    }

    return true;
}

}
