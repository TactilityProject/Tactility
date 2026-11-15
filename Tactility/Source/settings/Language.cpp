#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <localisation/localisation.h>

#include <tactility/log.h>

#include <cstring>

namespace tt::settings {

constexpr auto* TAG = "Language";

namespace {

struct LanguageInfo {
    Language language;
    const char* tag;
    const char* displayName;
};

constexpr LanguageInfo LANGUAGES[] = {
    { Language::en_GB, "en-GB", "English (UK)" },
    { Language::en_US, "en-US", "English (US)" },
    { Language::fr_FR, "fr-FR", "Français" },
    { Language::nl_NL, "nl-NL", "Nederlands" },
    { Language::es_ES, "es-ES", "Español" },
    { Language::de_DE, "de-DE", "Deutsch" },
};

static_assert(sizeof(LANGUAGES) / sizeof(LANGUAGES[0]) == static_cast<size_t>(Language::count));

const LanguageInfo& getInfo(Language language) {
    for (const auto& info : LANGUAGES) {
        if (info.language == language) {
            return info;
        }
    }
    LOG_E(TAG, "Missing info for language %d", static_cast<int>(language));
    return LANGUAGES[1];
}

} // namespace

void setLanguage(Language newLanguage) {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return;
    }

    properties.language = newLanguage;
    saveSystemSettings(properties);
    localisation_set_locale(getInfo(newLanguage).tag);
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
    return getInfo(language).tag;
}

bool fromString(const std::string& text, Language& language) {
    for (const auto& info : LANGUAGES) {
        if (text == info.tag) {
            language = info.language;
            return true;
        }
    }
    return false;
}

const char* getDisplayName(Language language) {
    return getInfo(language).displayName;
}

}
