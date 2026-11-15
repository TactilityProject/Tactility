#pragma once

#include <string>

namespace tt::settings {

enum class Language {
    en_GB,
    en_US,
    fr_FR,
    nl_NL,
    es_ES,
    de_DE,
    count
};

/** Saves the language and makes it the active locale. Neither changes when the settings can't be saved. @return true on success */
bool setLanguage(Language language);

Language getLanguage();

std::string toString(Language language);

bool fromString(const std::string& text, Language& language);

/** The name of the language in that language, for showing in a list */
const char* getDisplayName(Language language);

}
