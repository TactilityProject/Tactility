#pragma once

#include <localisation/localisation.h>

#include <initializer_list>
#include <string>
#include <string_view>

namespace tt::i18n {

/**
 * Opens the domain of an app of Tactility, with the catalogs of Translations/<name> that are part of the firmware.
 * Close it with localisation_domain_close().
 * @return the domain, or nullptr when it could not be created
 */
struct L10nDomain* openDomain(const char* name);

/**
 * Replaces every "{}" in a translated text by the next argument, in order.
 * Unlike std::format() a faulty translation can't make it fail: surplus placeholders stay and surplus arguments are ignored.
 */
std::string format(const char* text, std::initializer_list<std::string_view> arguments);

}
