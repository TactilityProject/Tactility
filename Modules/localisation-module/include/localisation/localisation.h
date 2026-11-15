// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A set of translations that belongs to one app. Strings that the domain doesn't translate are
 * looked up in the system domain, so shared words like "OK" are translated once.
 */
struct L10nDomain;

/** Called after the locale changed. Runs on the task that changed the locale. */
typedef void (*L10nLocaleListener)(void* context);

/**
 * @brief Opens a domain and loads its catalog for the current locale.
 * Reads a file, so don't call it on the LVGL task. Call it once, before the app creates its widgets.
 * @param[in] directory folder with a catalog per locale, named "<locale>.tlc" (e.g. "nl-NL.tlc"), or NULL for a domain that only has catalogs added with localisation_domain_add_catalog().
 * When the exact locale has no file, the catalog of its language ("nl.tlc") is used. A missing catalog is not an error.
 * @param[out] out_domain the opened domain, to close with localisation_domain_close()
 * @retval ERROR_OUT_OF_MEMORY
 * @retval ERROR_NONE on success
 */
error_t localisation_domain_open(const char* directory, struct L10nDomain** out_domain);

/**
 * @brief Adds a catalog to a domain. The data must stay valid until the domain is closed, e.g. const data in flash.
 * A catalog of a directory has priority over an added one for the same locale.
 * @param[in] locale a language tag like "nl-NL" (copied)
 * @retval ERROR_INVALID_ARGUMENT the data is not a catalog
 * @retval ERROR_OUT_OF_MEMORY the domain is full
 * @retval ERROR_NONE on success
 */
error_t localisation_domain_add_catalog(struct L10nDomain* domain, const char* locale, const void* data, size_t size);

/** Closes a domain. Strings that were returned for it become invalid. */
void localisation_domain_close(struct L10nDomain* domain);

/**
 * @brief Translates a text.
 * @param[in] domain the domain of the app, or NULL to only use the system domain
 * @param[in] msgid the English text, which is also the fallback
 * @return the translation, or @a msgid when there is none. Never NULL. The result is valid until the locale changes
 * twice or the domain is closed, so use it right away (UI widgets copy it).
 */
const char* localisation_get(const struct L10nDomain* domain, const char* msgid);

/**
 * @brief Translates a text that has more than one meaning.
 * @param[in] msgctxt a short description of the meaning, which is not shown (e.g. "verb")
 * @see localisation_get()
 */
const char* localisation_get_ctx(const struct L10nDomain* domain, const char* msgctxt, const char* msgid);

/**
 * @brief Changes the locale, which reloads the catalogs of all domains. Setting the current locale again does nothing.
 * Reads files, so don't call it on the LVGL task.
 * @param[in] tag a language tag like "en-US" or "nl-NL" (at most 15 characters)
 * @retval ERROR_INVALID_ARGUMENT the tag is empty or too long
 * @retval ERROR_NONE on success
 */
error_t localisation_set_locale(const char* tag);

/**
 * @brief Copies the current locale tag, "en-US" until it was changed.
 * @param[out] out_tag buffer for the tag
 * @param[in] out_tag_size size of the buffer
 * @retval ERROR_BUFFER_OVERFLOW if the buffer is too small
 * @retval ERROR_NONE on success
 */
error_t localisation_get_locale(char* out_tag, size_t out_tag_size);

/**
 * @brief Makes a catalog available to every domain. The data must stay valid, e.g. const data in flash.
 * @param[in] locale a language tag like "nl-NL" (copied)
 * @param[in] data the content of a "<locale>.tlc" file
 * @param[in] size size of @a data
 * @retval ERROR_INVALID_ARGUMENT the data is not a catalog
 * @retval ERROR_OUT_OF_MEMORY
 * @retval ERROR_NONE on success
 */
error_t localisation_add_system_catalog(const char* locale, const void* data, size_t size);

/**
 * @brief Adds a listener that is called after the locale changed. It runs on the task that changed the locale.
 * @return ERROR_NONE, or ERROR_OUT_OF_MEMORY when the list is full or memory ran out
 */
error_t localisation_add_locale_listener(L10nLocaleListener listener, void* context);

/**
 * @brief Removes a listener. A listener that was removed is not called anymore, also not by a notification that is in progress.
 * When the listener is running on other tasks, this waits until all of those calls returned, so the context can be freed afterwards.
 * A listener can remove itself or another listener from inside a notification.
 */
void localisation_remove_locale_listener(L10nLocaleListener listener, void* context);

#ifdef __cplusplus
}
#endif
