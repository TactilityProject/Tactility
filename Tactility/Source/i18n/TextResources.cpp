#include <Tactility/i18n/TextResources.h>
#include <Tactility/file/FileLock.h>

#include <Tactility/file/File.h>
#include <Tactility/settings/Language.h>

#include <tactility/log.h>

#include <format>
#include <utility>

namespace tt::i18n {

constexpr auto* TAG = "I18n";

static std::string getFallbackLocale() {
    return "en-US";
}

static std::string getDesiredLocale() {
    switch (settings::getLanguage()) {
        case settings::Language::en_GB:
            return "en-GB";
        case settings::Language::en_US:
            return "en-US";
        case settings::Language::fr_FR:
            return "fr-FR";
        case settings::Language::nl_BE:
            return "nl-BE";
        case settings::Language::nl_NL:
            return "nl-NL";
        default:
            return getFallbackLocale();
    }
}

static std::string getI18nDataFilePath(const std::string& path) {
    auto locale = getDesiredLocale();
    auto desired_file_path = std::format("{}/{}.i18n", path, locale);
    if (file::isFile(desired_file_path)) {
        return desired_file_path;
    } else {
        LOG_W(TAG, "Translations not found for %s at %s", locale.c_str(), desired_file_path.c_str());
    }

    auto fallback_locale = getFallbackLocale();
    auto fallback_file_path = std::format("{}/{}.i18n", path, getFallbackLocale());
    if (file::isFile(fallback_file_path)) {
        return fallback_file_path;
    } else {
        LOG_W(TAG, "Fallback translations not found for %s at %s", fallback_locale.c_str(), fallback_file_path.c_str());
        return "";
    }
}

std::string TextResources::ERROR_RESULT = "TXT_RES_ERROR";

bool TextResources::load() {
    std::vector<std::string> new_data;

    // Resolve the language file that we need (depends on system language selection)
    auto file_path = getI18nDataFilePath(path);
    if (file_path.empty()) {
        LOG_E(TAG, "Couldn't find i18n data for %s", path.c_str());
        return false;
    }

    file::readLines(file_path, true, [&new_data](const char* line) {
        new_data.push_back(line);
    });

    if (new_data.empty()) {
        LOG_E(TAG, "Couldn't find i18n data for %s", path.c_str());
        return false;
    }

    data = std::move(new_data);
    return true;
}

}
