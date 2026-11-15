// SPDX-License-Identifier: Apache-2.0
#include <localisation/catalog.h>
#include <localisation/localisation.h>
#include <localisation/module.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/task.h>
#include <tactility/module.h>
#include <tactility/memory.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr size_t LOCALE_MAX_LENGTH = 15;
constexpr size_t MAX_SYSTEM_CATALOGS = 8;
constexpr size_t MAX_LISTENERS = 8;
constexpr size_t MAX_RUNNING_CALLS = 16;
constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

struct Catalog {
    /** The memory that holds the catalog, or nullptr when the catalog is not owned */
    void* memory = nullptr;
    const void* data = nullptr;
    size_t size = 0;
};

struct SystemCatalog {
    char locale[LOCALE_MAX_LENGTH + 1];
    Catalog catalog;
};

struct Listener {
    L10nLocaleListener function;
    void* context;
};

/** A listener that is being called, and on which task */
struct RunningCall {
    Listener listener;
    TaskHandle_t task;
};

}

struct L10nDomain {
    /** NULL when the domain only has added catalogs */
    char* directory;
    Catalog current;
    SystemCatalog added[MAX_SYSTEM_CATALOGS];
    size_t added_count;
    /** The added catalog for the current locale, or empty */
    Catalog active_added;
    /** The catalog of the previous locale stays for one more change, so strings that were just returned stay valid */
    Catalog retired;
    L10nDomain* next;
};

namespace {

struct State {
    Mutex mutex;
    char locale[LOCALE_MAX_LENGTH + 1] = "en-US";
    L10nDomain* domains = nullptr;
    SystemCatalog system_catalogs[MAX_SYSTEM_CATALOGS] = {};
    size_t system_catalog_count = 0;
    /** The system catalog for the current locale, or empty */
    Catalog active_system;
    Listener listeners[MAX_LISTENERS] = {};
    size_t listener_count = 0;
    /** The listener calls that are in progress, also the nested ones and those of different tasks */
    RunningCall running_calls[MAX_RUNNING_CALLS] = {};
    size_t running_call_count = 0;
};

State* state = nullptr;

// region Helpers

bool is_valid_tag(const char* tag) {
    const size_t length = tag == nullptr ? 0 : strlen(tag);
    if (length == 0 || length > LOCALE_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        const char c = tag[i];
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

/** Copies the language part of a tag: "nl" for "nl-NL". @return false when the tag has no region */
bool get_language(const char* tag, char* out_language) {
    const size_t length = strcspn(tag, "-_");
    if (tag[length] == '\0') {
        return false;
    }
    memcpy(out_language, tag, length);
    out_language[length] = '\0';
    return true;
}

void free_catalog(Catalog& catalog) {
    if (catalog.memory != nullptr) {
        memory_free(catalog.memory);
    }
    catalog = Catalog {};
}

bool read_catalog_file(const char* directory, const char* tag, Catalog& out_catalog) {
    char path[256];
    if (snprintf(path, sizeof(path), "%s/%s.tlc", directory, tag) >= static_cast<int>(sizeof(path))) {
        return false;
    }
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    bool success = false;
    if (fseek(file, 0, SEEK_END) == 0) {
        const long length = ftell(file);
        if (length > 0 && fseek(file, 0, SEEK_SET) == 0) {
            void* memory = memory_alloc_with_policy(static_cast<size_t>(length), &MEMORY_POLICY);
            if (memory != nullptr) {
                if (fread(memory, 1, static_cast<size_t>(length), file) == static_cast<size_t>(length) && catalog_validate(memory, static_cast<size_t>(length))) {
                    out_catalog = { memory, memory, static_cast<size_t>(length) };
                    success = true;
                } else {
                    memory_free(memory);
                }
            }
        }
    }
    fclose(file);
    return success;
}

/** Loads the catalog for the current locale, or the one of its language. Leaves @a out_catalog empty when there is none. */
void load_catalog(const L10nDomain* domain, Catalog& out_catalog) {
    out_catalog = Catalog {};
    if (domain->directory == nullptr) {
        return;
    }
    if (read_catalog_file(domain->directory, state->locale, out_catalog)) {
        return;
    }
    char language[LOCALE_MAX_LENGTH + 1];
    if (get_language(state->locale, language)) {
        read_catalog_file(domain->directory, language, out_catalog);
    }
}

/** The catalog for the current locale, or for its language. Empty when there is none. */
Catalog select_catalog(const SystemCatalog* catalogs, size_t count) {
    char language[LOCALE_MAX_LENGTH + 1];
    const bool has_language = get_language(state->locale, language);
    const SystemCatalog* language_match = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(catalogs[i].locale, state->locale) == 0) {
            return catalogs[i].catalog;
        }
        if (has_language && strcmp(catalogs[i].locale, language) == 0) {
            language_match = &catalogs[i];
        }
    }
    return language_match != nullptr ? language_match->catalog : Catalog {};
}

void update_active_system_catalog() {
    state->active_system = select_catalog(state->system_catalogs, state->system_catalog_count);
}

// endregion

}

// region Module

static error_t start() {
    if (state != nullptr) {
        return ERROR_NONE;
    }
    void* memory = memory_alloc_with_policy(sizeof(State), &MEMORY_POLICY);
    if (memory == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    state = new (memory) State();
    mutex_construct(&state->mutex);
    return ERROR_NONE;
}

static error_t stop() {
    if (state == nullptr) {
        return ERROR_NONE;
    }
    State* old_state = state;
    state = nullptr;
    while (old_state->domains != nullptr) {
        L10nDomain* domain = old_state->domains;
        old_state->domains = domain->next;
        free_catalog(domain->current);
        free_catalog(domain->retired);
        memory_free(domain->directory);
        memory_free(domain);
    }
    mutex_destruct(&old_state->mutex);
    memory_free(old_state);
    return ERROR_NONE;
}

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    DEFINE_MODULE_SYMBOL(localisation_domain_open),
    DEFINE_MODULE_SYMBOL(localisation_domain_close),
    DEFINE_MODULE_SYMBOL(localisation_domain_add_catalog),
    DEFINE_MODULE_SYMBOL(localisation_get),
    DEFINE_MODULE_SYMBOL(localisation_get_ctx),
    DEFINE_MODULE_SYMBOL(localisation_set_locale),
    DEFINE_MODULE_SYMBOL(localisation_get_locale),
    DEFINE_MODULE_SYMBOL(localisation_add_system_catalog),
    DEFINE_MODULE_SYMBOL(localisation_add_locale_listener),
    DEFINE_MODULE_SYMBOL(localisation_remove_locale_listener),
    MODULE_SYMBOL_TERMINATOR,
};

struct Module localisation_module = {
    .name = "localisation",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

// endregion

error_t localisation_domain_open(const char* directory, struct L10nDomain** out_domain) {
    *out_domain = nullptr;
    auto* domain = static_cast<L10nDomain*>(memory_calloc_with_policy(1, sizeof(L10nDomain), &MEMORY_POLICY));
    const size_t directory_length = directory == nullptr ? 0 : strlen(directory);
    char* directory_copy = directory == nullptr ? nullptr : static_cast<char*>(memory_alloc_with_policy(directory_length + 1, &MEMORY_POLICY));
    if (domain == nullptr || (directory != nullptr && directory_copy == nullptr) || state == nullptr) {
        memory_free(domain);
        memory_free(directory_copy);
        return ERROR_OUT_OF_MEMORY;
    }
    if (directory != nullptr) {
        memcpy(directory_copy, directory, directory_length + 1);
    }
    domain->directory = directory_copy;

    mutex_lock(&state->mutex);
    load_catalog(domain, domain->current);
    domain->next = state->domains;
    state->domains = domain;
    mutex_unlock(&state->mutex);

    *out_domain = domain;
    return ERROR_NONE;
}

void localisation_domain_close(struct L10nDomain* domain) {
    if (domain == nullptr || state == nullptr) {
        return;
    }
    mutex_lock(&state->mutex);
    L10nDomain** link = &state->domains;
    while (*link != nullptr && *link != domain) {
        link = &(*link)->next;
    }
    if (*link == domain) {
        *link = domain->next;
    }
    mutex_unlock(&state->mutex);

    free_catalog(domain->current);
    free_catalog(domain->retired);
    memory_free(domain->directory);
    memory_free(domain);
}

error_t localisation_domain_add_catalog(struct L10nDomain* domain, const char* locale, const void* data, size_t size) {
    if (!is_valid_tag(locale) || !catalog_validate(data, size)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (state == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    error_t result = ERROR_NONE;
    mutex_lock(&state->mutex);
    if (domain->added_count >= MAX_SYSTEM_CATALOGS) {
        result = ERROR_OUT_OF_MEMORY;
    } else {
        SystemCatalog& entry = domain->added[domain->added_count++];
        strcpy(entry.locale, locale);
        entry.catalog = { nullptr, data, size };
        domain->active_added = select_catalog(domain->added, domain->added_count);
    }
    mutex_unlock(&state->mutex);
    return result;
}

const char* localisation_get_ctx(const struct L10nDomain* domain, const char* msgctxt, const char* msgid) {
    if (state == nullptr) {
        return msgid;
    }
    const char* result = nullptr;
    mutex_lock(&state->mutex);
    if (domain != nullptr && domain->current.data != nullptr) {
        result = catalog_find(domain->current.data, msgctxt, msgid);
    }
    if (result == nullptr && domain != nullptr && domain->active_added.data != nullptr) {
        result = catalog_find(domain->active_added.data, msgctxt, msgid);
    }
    if (result == nullptr && state->active_system.data != nullptr) {
        result = catalog_find(state->active_system.data, msgctxt, msgid);
    }
    mutex_unlock(&state->mutex);
    return result != nullptr ? result : msgid;
}

const char* localisation_get(const struct L10nDomain* domain, const char* msgid) {
    return localisation_get_ctx(domain, nullptr, msgid);
}

error_t localisation_set_locale(const char* tag) {
    if (!is_valid_tag(tag)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (state == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    Listener listeners[MAX_LISTENERS];
    size_t listener_count;

    mutex_lock(&state->mutex);
    // Nothing changes for the same locale, and rotating the catalogs would free strings that were just returned
    if (strcmp(state->locale, tag) == 0) {
        mutex_unlock(&state->mutex);
        return ERROR_NONE;
    }
    strcpy(state->locale, tag);
    update_active_system_catalog();
    for (L10nDomain* domain = state->domains; domain != nullptr; domain = domain->next) {
        free_catalog(domain->retired);
        domain->retired = domain->current;
        load_catalog(domain, domain->current);
        domain->active_added = select_catalog(domain->added, domain->added_count);
    }
    listener_count = state->listener_count;
    memcpy(listeners, state->listeners, sizeof(listeners));
    mutex_unlock(&state->mutex);

    for (size_t i = 0; i < listener_count; i++) {
        // A listener that was removed since the copy was made is not called anymore
        bool registered = false;
        bool tracked = false;
        while (!tracked) {
            mutex_lock(&state->mutex);
            registered = false;
            for (size_t j = 0; j < state->listener_count; j++) {
                registered = registered || (state->listeners[j].function == listeners[i].function && state->listeners[j].context == listeners[i].context);
            }
            if (registered && state->running_call_count < MAX_RUNNING_CALLS) {
                state->running_calls[state->running_call_count++] = { listeners[i], xTaskGetCurrentTaskHandle() };
                tracked = true;
            }
            mutex_unlock(&state->mutex);
            if (!registered) {
                break;
            }
            if (!tracked) {
                // Too many calls are in progress: wait for one to finish
                vTaskDelay(1);
            }
        }
        if (!registered) {
            continue;
        }

        listeners[i].function(listeners[i].context);

        mutex_lock(&state->mutex);
        const auto task = xTaskGetCurrentTaskHandle();
        for (size_t j = 0; j < state->running_call_count; j++) {
            if (state->running_calls[j].task == task && state->running_calls[j].listener.function == listeners[i].function && state->running_calls[j].listener.context == listeners[i].context) {
                state->running_calls[j] = state->running_calls[--state->running_call_count];
                break;
            }
        }
        mutex_unlock(&state->mutex);
    }
    return ERROR_NONE;
}

error_t localisation_get_locale(char* out_tag, size_t out_tag_size) {
    if (state == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    error_t result = ERROR_NONE;
    mutex_lock(&state->mutex);
    if (strlen(state->locale) + 1 > out_tag_size) {
        result = ERROR_BUFFER_OVERFLOW;
    } else {
        strcpy(out_tag, state->locale);
    }
    mutex_unlock(&state->mutex);
    return result;
}

error_t localisation_add_system_catalog(const char* locale, const void* data, size_t size) {
    if (!is_valid_tag(locale) || !catalog_validate(data, size)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (state == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    error_t result = ERROR_NONE;
    mutex_lock(&state->mutex);
    if (state->system_catalog_count >= MAX_SYSTEM_CATALOGS) {
        result = ERROR_OUT_OF_MEMORY;
    } else {
        SystemCatalog& entry = state->system_catalogs[state->system_catalog_count++];
        strcpy(entry.locale, locale);
        entry.catalog = { nullptr, data, size };
        update_active_system_catalog();
    }
    mutex_unlock(&state->mutex);
    return result;
}

error_t localisation_add_locale_listener(L10nLocaleListener listener, void* context) {
    if (state == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    error_t result = ERROR_NONE;
    mutex_lock(&state->mutex);
    if (state->listener_count >= MAX_LISTENERS) {
        result = ERROR_OUT_OF_MEMORY;
    } else {
        state->listeners[state->listener_count++] = { listener, context };
    }
    mutex_unlock(&state->mutex);
    return result;
}

void localisation_remove_locale_listener(L10nLocaleListener listener, void* context) {
    if (state == nullptr) {
        return;
    }
    mutex_lock(&state->mutex);
    for (size_t i = 0; i < state->listener_count; i++) {
        if (state->listeners[i].function == listener && state->listeners[i].context == context) {
            state->listeners[i] = state->listeners[--state->listener_count];
            break;
        }
    }
    // Returns after the calls of the listener on other tasks finished, so its context can be freed
    const auto current_task = xTaskGetCurrentTaskHandle();
    while (true) {
        bool running_elsewhere = false;
        for (size_t i = 0; i < state->running_call_count; i++) {
            const auto& call = state->running_calls[i];
            running_elsewhere = running_elsewhere || (call.listener.function == listener && call.listener.context == context && call.task != current_task);
        }
        if (!running_elsewhere) {
            break;
        }
        mutex_unlock(&state->mutex);
        vTaskDelay(1);
        mutex_lock(&state->mutex);
    }
    mutex_unlock(&state->mutex);
}

}
