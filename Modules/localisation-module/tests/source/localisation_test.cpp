#include "doctest.h"

#include "FreeRTOS.h"
#include "task.h"

#include <localisation/catalog.h>
#include <localisation/localisation.h>
#include <localisation/module.h>
#include <localisation/tr.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct Item {
    std::string context;
    std::string text;
    std::string translation;
};

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

/** Builds a catalog the way Buildscripts/l10n/po2tlc.py does */
std::vector<uint8_t> buildCatalog(const std::vector<Item>& items) {
    struct Row { uint32_t hash; std::string key; std::string value; };
    std::vector<Row> rows;
    for (const auto& item : items) {
        const char* context = item.context.empty() ? nullptr : item.context.c_str();
        std::string key = item.context.empty() ? item.text : item.context + CATALOG_CONTEXT_SEPARATOR + item.text;
        rows.push_back({ catalog_hash(context, item.text.c_str()), key, item.translation });
    }
    std::ranges::sort(rows, [](const Row& a, const Row& b) { return a.hash != b.hash ? a.hash < b.hash : a.key < b.key; });

    std::vector<uint8_t> blob;
    std::vector<uint8_t> table;
    for (const auto& row : rows) {
        const auto keyOffset = static_cast<uint32_t>(blob.size());
        blob.insert(blob.end(), row.key.begin(), row.key.end());
        blob.push_back(0);
        const auto valueOffset = static_cast<uint32_t>(blob.size());
        blob.insert(blob.end(), row.value.begin(), row.value.end());
        blob.push_back(0);
        appendU32(table, row.hash);
        appendU32(table, keyOffset);
        appendU32(table, valueOffset);
    }

    std::vector<uint8_t> out = { 'T', 'L', 'C', '1' };
    appendU32(out, CATALOG_VERSION);
    appendU32(out, static_cast<uint32_t>(rows.size()));
    appendU32(out, 0);
    out.insert(out.end(), table.begin(), table.end());
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* file = fopen(path.c_str(), "wb");
    REQUIRE(file != nullptr);
    fwrite(data.data(), 1, data.size(), file);
    fclose(file);
}

const std::vector<Item> DUTCH = {
    { "", "OK", "OK" },
    { "", "Cancel", "Annuleren" },
    { "verb", "Open", "Openen" },
    { "adjective", "Open", "Geopend" },
};

struct ModuleScope {
    ModuleScope() { REQUIRE(localisation_module.start() == ERROR_NONE); }
    ~ModuleScope() { localisation_module.stop(); }
};

std::string makeTempDirectory() {
    char path[] = "/tmp/l10n_test_XXXXXX";
    REQUIRE(mkdtemp(path) != nullptr);
    return path;
}

void removeDirectory(const std::string& directory) {
    std::string command = "rm -rf " + directory;
    (void)system(command.c_str());
}

}

TEST_CASE("hash matches the catalog tool") {
    // Same values as Buildscripts/l10n/po2tlc.py fnv1a()
    CHECK(catalog_hash(nullptr, "OK") == 0x85e4b82f);
    CHECK(catalog_hash("verb", "Open") != catalog_hash(nullptr, "Open"));
}

TEST_CASE("catalog validation rejects malformed data") {
    auto catalog = buildCatalog(DUTCH);
    CHECK(catalog_validate(catalog.data(), catalog.size()));
    CHECK_FALSE(catalog_validate(catalog.data(), 8));
    CHECK_FALSE(catalog_validate(catalog.data(), catalog.size() - 1));
    auto badMagic = catalog;
    badMagic[0] = 'X';
    CHECK_FALSE(catalog_validate(badMagic.data(), badMagic.size()));
    auto badOffset = catalog;
    badOffset[CATALOG_HEADER_SIZE + 4] = 0xFF;
    badOffset[CATALOG_HEADER_SIZE + 5] = 0xFF;
    CHECK_FALSE(catalog_validate(badOffset.data(), badOffset.size()));
    CHECK_FALSE(catalog_validate(nullptr, 0));
}

TEST_CASE("catalog lookup finds texts and contexts") {
    auto catalog = buildCatalog(DUTCH);
    CHECK(std::string(catalog_find(catalog.data(), nullptr, "Cancel")) == "Annuleren");
    CHECK(std::string(catalog_find(catalog.data(), "verb", "Open")) == "Openen");
    CHECK(std::string(catalog_find(catalog.data(), "adjective", "Open")) == "Geopend");
    CHECK(catalog_find(catalog.data(), nullptr, "Open") == nullptr);
    CHECK(catalog_find(catalog.data(), nullptr, "Missing") == nullptr);
}

TEST_CASE("catalog lookup handles hash collisions") {
    // Two keys with the same hash, by setting the hash by hand
    std::vector<uint8_t> out = { 'T', 'L', 'C', '1' };
    appendU32(out, CATALOG_VERSION);
    appendU32(out, 2);
    appendU32(out, 0);
    const uint32_t hash = catalog_hash(nullptr, "B");
    appendU32(out, hash); appendU32(out, 0); appendU32(out, 2);
    appendU32(out, hash); appendU32(out, 4); appendU32(out, 6);
    const char blob[] = "A\0a\0B\0b";
    out.insert(out.end(), blob, blob + sizeof(blob));
    REQUIRE(catalog_validate(out.data(), out.size()));
    CHECK(std::string(catalog_find(out.data(), nullptr, "B")) == "b");
}

TEST_CASE("without a translation the text is returned") {
    ModuleScope scope;
    const char* text = "Untranslated";
    CHECK(localisation_get(nullptr, text) == text);
    CHECK(localisation_get_ctx(nullptr, "ctx", text) == text);
}

TEST_CASE("system catalog translates for every domain") {
    ModuleScope scope;
    auto dutch = buildCatalog(DUTCH);
    REQUIRE(localisation_add_system_catalog("nl-BE", dutch.data(), dutch.size()) == ERROR_NONE);
    CHECK(std::string(tr("Cancel")) == "Cancel");
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    CHECK(std::string(tr("Cancel")) == "Annuleren");
    CHECK(std::string(trc("verb", "Open")) == "Openen");
    REQUIRE(localisation_set_locale("fr-FR") == ERROR_NONE);
    CHECK(std::string(tr("Cancel")) == "Cancel");
}

TEST_CASE("a language catalog is used for a region") {
    ModuleScope scope;
    auto dutch = buildCatalog(DUTCH);
    REQUIRE(localisation_add_system_catalog("nl", dutch.data(), dutch.size()) == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    CHECK(std::string(tr("Cancel")) == "Annuleren");
}

TEST_CASE("domain catalog wins over the system catalog") {
    ModuleScope scope;
    const auto directory = makeTempDirectory();
    auto system = buildCatalog({ { "", "Cancel", "Annuleren" }, { "", "Delete", "Verwijderen" } });
    auto app = buildCatalog({ { "", "Delete", "Wissen" } });
    writeFile(directory + "/nl-BE.tlc", app);
    REQUIRE(localisation_add_system_catalog("nl-BE", system.data(), system.size()) == ERROR_NONE);

    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(directory.c_str(), &domain) == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Delete")) == "Wissen");
    CHECK(std::string(localisation_get(domain, "Cancel")) == "Annuleren");
    CHECK(std::string(localisation_get(nullptr, "Delete")) == "Verwijderen");
    localisation_domain_close(domain);
    removeDirectory(directory);
}

TEST_CASE("domain loads the catalog of the current locale when opened and when changed") {
    ModuleScope scope;
    const auto directory = makeTempDirectory();
    writeFile(directory + "/nl-BE.tlc", buildCatalog({ { "", "Hello", "Hallo" } }));
    writeFile(directory + "/fr.tlc", buildCatalog({ { "", "Hello", "Bonjour" } }));

    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(directory.c_str(), &domain) == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Hallo");

    REQUIRE(localisation_set_locale("fr-FR") == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Bonjour");

    REQUIRE(localisation_set_locale("de-DE") == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Hello");
    localisation_domain_close(domain);
    removeDirectory(directory);
}

TEST_CASE("a string stays valid for one locale change") {
    ModuleScope scope;
    const auto directory = makeTempDirectory();
    writeFile(directory + "/nl-BE.tlc", buildCatalog({ { "", "Hello", "Hallo" } }));
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(directory.c_str(), &domain) == ERROR_NONE);
    const char* hello = localisation_get(domain, "Hello");
    REQUIRE(localisation_set_locale("en-US") == ERROR_NONE);
    CHECK(std::string(hello) == "Hallo");
    localisation_domain_close(domain);
    removeDirectory(directory);
}

TEST_CASE("a corrupt catalog file is ignored") {
    ModuleScope scope;
    const auto directory = makeTempDirectory();
    writeFile(directory + "/nl-BE.tlc", { 'n', 'o', 'p', 'e' });
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(directory.c_str(), &domain) == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Hello");
    localisation_domain_close(domain);
    removeDirectory(directory);
}

TEST_CASE("locale validation") {
    ModuleScope scope;
    CHECK(localisation_set_locale("") == ERROR_INVALID_ARGUMENT);
    CHECK(localisation_set_locale("../etc") == ERROR_INVALID_ARGUMENT);
    CHECK(localisation_set_locale("a-very-long-locale-tag") == ERROR_INVALID_ARGUMENT);
    char tag[16];
    REQUIRE(localisation_get_locale(tag, sizeof(tag)) == ERROR_NONE);
    CHECK(std::string(tag) == "en-US");
    char small[3];
    CHECK(localisation_get_locale(small, sizeof(small)) == ERROR_BUFFER_OVERFLOW);
}

namespace {
int listenerCalls = 0;
void onLocaleChanged(void* context) { listenerCalls += *static_cast<int*>(context); }
}

TEST_CASE("locale listeners are called") {
    ModuleScope scope;
    int increment = 2;
    listenerCalls = 0;
    REQUIRE(localisation_add_locale_listener(onLocaleChanged, &increment) == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-BE") == ERROR_NONE);
    CHECK(listenerCalls == 2);
    localisation_remove_locale_listener(onLocaleChanged, &increment);
    REQUIRE(localisation_set_locale("fr-FR") == ERROR_NONE);
    CHECK(listenerCalls == 2);
}

TEST_CASE("domain with added catalogs") {
    ModuleScope scope;
    auto dutch = buildCatalog({ { "", "Hello", "Hallo" }, { "", "Cancel", "Nope" } });
    auto system = buildCatalog({ { "", "Cancel", "Annuleren" }, { "", "Close", "Sluiten" } });
    REQUIRE(localisation_add_system_catalog("nl-NL", system.data(), system.size()) == ERROR_NONE);

    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(nullptr, &domain) == ERROR_NONE);
    REQUIRE(localisation_domain_add_catalog(domain, "nl-NL", dutch.data(), dutch.size()) == ERROR_NONE);
    CHECK(localisation_domain_add_catalog(domain, "nl-NL", "nope", 4) == ERROR_INVALID_ARGUMENT);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Hello");

    REQUIRE(localisation_set_locale("nl-NL") == ERROR_NONE);
    CHECK(std::string(localisation_get(domain, "Hello")) == "Hallo");
    CHECK(std::string(localisation_get(domain, "Cancel")) == "Nope");
    CHECK(std::string(localisation_get(domain, "Close")) == "Sluiten");
    // Another domain or no domain doesn't see them
    CHECK(std::string(localisation_get(nullptr, "Hello")) == "Hello");
    localisation_domain_close(domain);
}

TEST_CASE("setting the same locale again keeps the strings valid") {
    ModuleScope scope;
    const auto directory = makeTempDirectory();
    writeFile(directory + "/nl-NL.tlc", buildCatalog({ { "", "Hello", "Hallo" } }));
    REQUIRE(localisation_set_locale("nl-NL") == ERROR_NONE);
    L10nDomain* domain = nullptr;
    REQUIRE(localisation_domain_open(directory.c_str(), &domain) == ERROR_NONE);
    const char* hello = localisation_get(domain, "Hello");

    REQUIRE(localisation_set_locale("en-US") == ERROR_NONE);
    REQUIRE(localisation_set_locale("en-US") == ERROR_NONE);
    CHECK(std::string(hello) == "Hallo");

    localisation_domain_close(domain);
    removeDirectory(directory);
}

TEST_CASE("setting the same locale again doesn't call listeners") {
    ModuleScope scope;
    int increment = 1;
    listenerCalls = 0;
    REQUIRE(localisation_add_locale_listener(onLocaleChanged, &increment) == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-NL") == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-NL") == ERROR_NONE);
    CHECK(listenerCalls == 1);
    localisation_remove_locale_listener(onLocaleChanged, &increment);
}

namespace {
int firstCalls = 0;
int secondCalls = 0;
int secondContext = 0;
void removableSecond(void*) { secondCalls++; }
void removingFirst(void*) {
    firstCalls++;
    localisation_remove_locale_listener(removableSecond, &secondContext);
}
}

TEST_CASE("a listener that is removed during a notification is not called") {
    ModuleScope scope;
    firstCalls = 0;
    secondCalls = 0;
    REQUIRE(localisation_add_locale_listener(removingFirst, nullptr) == ERROR_NONE);
    REQUIRE(localisation_add_locale_listener(removableSecond, &secondContext) == ERROR_NONE);
    REQUIRE(localisation_set_locale("nl-NL") == ERROR_NONE);
    CHECK(firstCalls == 1);
    CHECK(secondCalls == 0);
    localisation_remove_locale_listener(removingFirst, nullptr);
}

namespace {
std::atomic<bool> insideListener = false;
std::atomic<bool> releaseListener = false;
std::atomic<bool> removeReturned = false;
std::atomic<bool> notifierDone = false;

void blockingListener(void*) {
    insideListener = true;
    while (!releaseListener) {
        vTaskDelay(1);
    }
}

void notifierTask(void*) {
    localisation_set_locale("de-DE");
    notifierDone = true;
    vTaskDelete(nullptr);
}

void removerTask(void*) {
    localisation_remove_locale_listener(blockingListener, nullptr);
    removeReturned = true;
    vTaskDelete(nullptr);
}
}

TEST_CASE("removing a listener waits for a call on another task") {
    ModuleScope scope;
    insideListener = false;
    releaseListener = false;
    removeReturned = false;
    notifierDone = false;
    REQUIRE(localisation_add_locale_listener(blockingListener, nullptr) == ERROR_NONE);

    REQUIRE(xTaskCreate(notifierTask, "notifier", 4096, nullptr, 1, nullptr) == pdPASS);
    while (!insideListener) {
        vTaskDelay(1);
    }
    REQUIRE(xTaskCreate(removerTask, "remover", 4096, nullptr, 1, nullptr) == pdPASS);
    vTaskDelay(20);
    CHECK_FALSE(removeReturned);
    releaseListener = true;
    while (!removeReturned || !notifierDone) {
        vTaskDelay(1);
    }
    CHECK(removeReturned);
}

namespace {
std::atomic<int> nestedDepth = 0;
std::atomic<int> maxNestedDepth = 0;
void nestingListener(void*) {
    const int depth = ++nestedDepth;
    maxNestedDepth = std::max<int>(maxNestedDepth, depth);
    if (depth < 40) {
        // Another locale each time, as the same locale does nothing
        localisation_set_locale(depth % 2 == 0 ? "nl-NL" : "fr-FR");
    }
    nestedDepth--;
}
}

TEST_CASE("nested notifications are not limited by a fixed number of slots") {
    ModuleScope scope;
    nestedDepth = 0;
    maxNestedDepth = 0;
    REQUIRE(localisation_add_locale_listener(nestingListener, nullptr) == ERROR_NONE);
    REQUIRE(localisation_set_locale("de-DE") == ERROR_NONE);
    CHECK(maxNestedDepth == 40);
    localisation_remove_locale_listener(nestingListener, nullptr);
}

namespace {
std::atomic<int> insideSelfRemoving = 0;
std::atomic<int> selfRemovedReturned = 0;
std::atomic<bool> releaseSelfRemoving = false;
int selfRemovingContext = 0;
std::atomic<int> selfRemovingDone = 0;
void selfRemovingListener(void*) {
    insideSelfRemoving++;
    while (!releaseSelfRemoving) {
        vTaskDelay(1);
    }
    // Both tasks remove the listener from inside it: neither may wait for the other
    localisation_remove_locale_listener(selfRemovingListener, nullptr);
    selfRemovedReturned++;
}
void selfRemovingTask(void* locale) {
    localisation_set_locale(static_cast<const char*>(locale));
    selfRemovingDone++;
    vTaskDelete(nullptr);
}
}

TEST_CASE("listeners that remove themselves on two tasks do not wait for each other") {
    ModuleScope scope;
    insideSelfRemoving = 0;
    selfRemovedReturned = 0;
    selfRemovingDone = 0;
    releaseSelfRemoving = false;
    REQUIRE(localisation_add_locale_listener(selfRemovingListener, nullptr) == ERROR_NONE);
    REQUIRE(xTaskCreate(selfRemovingTask, "a", 4096, const_cast<char*>("de-DE"), 1, nullptr) == pdPASS);
    // The second notification only calls the listener once it is registered and not removed: wait for the first to be inside
    while (insideSelfRemoving < 1) {
        vTaskDelay(1);
    }
    REQUIRE(xTaskCreate(selfRemovingTask, "b", 4096, const_cast<char*>("fr-FR"), 1, nullptr) == pdPASS);
    while (insideSelfRemoving < 2) {
        vTaskDelay(1);
    }
    releaseSelfRemoving = true;
    for (int i = 0; i < 500 && selfRemovingDone < 2; i++) {
        vTaskDelay(1);
    }
    CHECK(selfRemovingDone == 2);
    CHECK(selfRemovedReturned == 2);
}
