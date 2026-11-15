#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include <localisation/localisation.h>

#include <tactility/log.h>

#include <cstring>
#include <strings.h>

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

/** Tags that are accepted as another language, e.g. a settings file or a device that uses nl-BE. Saving writes the canonical tag. */
constexpr struct { const char* tag; Language language; } ALIASES[] = {
    { "en-AU", Language::en_GB },
    { "en-IE", Language::en_GB },
    { "en-IN", Language::en_GB },
    { "en-NZ", Language::en_GB },
    { "en-ZA", Language::en_GB },
};

/** The language that is used for a tag without a match of its own: any region of it, or the language alone */
constexpr struct { const char* language; Language fallback; } LANGUAGE_DEFAULTS[] = {
    { "en", Language::en_US },
    { "fr", Language::fr_FR },
    { "nl", Language::nl_NL },
    { "es", Language::es_ES },
    { "de", Language::de_DE },
};

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
    // Tags are written with a dash, but an underscore (nl_BE) and any letter case are accepted
    std::string tag = text;
    for (auto& c : tag) {
        c = c == '_' ? '-' : c;
    }

    for (const auto& info : LANGUAGES) {
        if (strcasecmp(tag.c_str(), info.tag) == 0) {
            language = info.language;
            return true;
        }
    }
    for (const auto& alias : ALIASES) {
        if (strcasecmp(tag.c_str(), alias.tag) == 0) {
            language = alias.language;
            return true;
        }
    }
    const auto language_length = tag.find('-');
    const auto language_part = tag.substr(0, language_length);
    for (const auto& entry : LANGUAGE_DEFAULTS) {
        if (strcasecmp(language_part.c_str(), entry.language) == 0) {
            language = entry.fallback;
            return true;
        }
    }
    return false;
}

const char* getDisplayName(Language language) {
    return getInfo(language).displayName;
}

}
