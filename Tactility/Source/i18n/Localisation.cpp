#include <Tactility/TactilityPrivate.h>
#include <Tactility/i18n/Localisation.h>
#include <Tactility/settings/Language.h>

#include <generated_catalogs.h>

#include <localisation/localisation.h>

#include <tactility/log.h>

#include <cstring>

namespace tt {

constexpr auto* TAG = "Localisation";
constexpr auto* SYSTEM_DOMAIN = "tactility";

void initLocalisation() {
    for (size_t i = 0; i < generated_catalogs_count; i++) {
        const auto& catalog = generated_catalogs[i];
        if (strcmp(catalog.domain, SYSTEM_DOMAIN) != 0) {
            continue;
        }
        if (localisation_add_system_catalog(catalog.locale, catalog.data, catalog.size) != ERROR_NONE) {
            LOG_E(TAG, "Failed to add catalog %s", catalog.locale);
        }
    }
    const auto locale = settings::toString(settings::getLanguage());
    if (localisation_set_locale(locale.c_str()) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set locale %s", locale.c_str());
    }
}

} // namespace

namespace tt::i18n {

L10nDomain* openDomain(const char* name) {
    L10nDomain* domain = nullptr;
    if (localisation_domain_open(nullptr, &domain) != ERROR_NONE) {
        return nullptr;
    }
    for (size_t i = 0; i < generated_catalogs_count; i++) {
        const auto& catalog = generated_catalogs[i];
        if (strcmp(catalog.domain, name) == 0 && localisation_domain_add_catalog(domain, catalog.locale, catalog.data, catalog.size) != ERROR_NONE) {
            LOG_E(TAG, "Failed to add catalog %s of %s", catalog.locale, name);
        }
    }
    return domain;
}

std::string format(const char* text, std::initializer_list<std::string_view> arguments) {
    std::string result;
    auto argument = arguments.begin();
    for (const char* c = text; *c != '\0'; c++) {
        if (c[0] == '{' && c[1] == '}' && argument != arguments.end()) {
            result += *argument++;
            c++;
        } else {
            result += *c;
        }
    }
    return result;
}

}
