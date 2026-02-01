#ifdef ESP_PLATFORM

#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/ElfApp.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/Logger.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/StringUtils.h>

#include <esp_elf.h>
#include <string>
#include <utility>

namespace tt::app {

static auto LOGGER = Logger("ElfApp");

static std::string getErrorCodeString(int error_code) {
    switch (error_code) {
        case ENOMEM:
            return "out of memory";
        case ENOSYS:
            return "missing symbol";
        case EINVAL:
            return "invalid argument or main() missing";
        default:
            return std::format("code {}", error_code);
    }
}

class ElfApp final : public App {

public:

    struct Parameters {
        CreateData createData = nullptr;
        DestroyData destroyData = nullptr;
        OnCreate onCreate = nullptr;
        OnDestroy onDestroy = nullptr;
        OnShow onShow = nullptr;
        OnHide onHide = nullptr;
        OnResult onResult = nullptr;
    };

    static void setParameters(const Parameters& parameters) {
        staticParameters = parameters;
        staticParametersSetCount++;
    }

private:

    static Parameters staticParameters;
    static size_t staticParametersSetCount;
    static std::shared_ptr<Lock> staticParametersLock;

    const std::string appPath;
    std::unique_ptr<uint8_t[]> elfFileData;
    esp_elf_t elf {
        .psegment = nullptr,
        .svaddr = 0,
        .ptext = nullptr,
        .pdata = nullptr,
        .sec = { },
        .entry = nullptr
    };
    bool shouldCleanupElf = false; // Whether we have to clean up the above "elf" object
    std::unique_ptr<Parameters> manifest;
    void* data = nullptr;
    std::string lastError = "";

    bool startElf() {
        const std::string elf_path = std::format("{}/elf/{}.elf", appPath, CONFIG_IDF_TARGET);
        LOGGER.info("Starting ELF {}", elf_path);
        assert(elfFileData == nullptr);

        size_t size = 0;
        file::getLock(elf_path)->withLock([this, &elf_path, &size]{
            elfFileData = file::readBinary(elf_path, size);
        });

        if (elfFileData == nullptr) {
            return false;
        }

        if (esp_elf_init(&elf) != ESP_OK) {
            lastError = "Failed to initialize";
            LOGGER.error("{}", lastError);
            elfFileData  = nullptr;
            return false;
        }

        auto relocate_result = esp_elf_relocate(&elf, elfFileData.get());
        if (relocate_result != 0) {
            // Note: the result code maps to values from cstdlib's errno.h
            lastError = getErrorCodeString(-relocate_result);
            LOGGER.error("Application failed to load: {}", lastError);
            elfFileData  = nullptr;
            return false;
        }

        int argc = 0;
        char* argv[] = {};

        if (esp_elf_request(&elf, 0, argc, argv) != ESP_OK) {
            lastError = "Executable returned error code";
            LOGGER.error("{}", lastError);
            esp_elf_deinit(&elf);
            elfFileData  = nullptr;
            return false;
        }

        shouldCleanupElf = true;
        return true;
    }

    void stopElf() {
        LOGGER.info("Cleaning up ELF");

        if (shouldCleanupElf) {
            esp_elf_deinit(&elf);
        }

        if (elfFileData != nullptr) {
            elfFileData = nullptr;
        }
    }

public:

    explicit ElfApp(std::string appPath) : appPath(std::move(appPath)) {}

    void onCreate(AppContext& appContext) override {
        // Because we use global variables, we have to ensure that we are not starting 2 apps in parallel
        // We use a ScopedLock so we don't have to safeguard all branches
        auto lock = staticParametersLock->asScopedLock();
        lock.lock();

        staticParametersSetCount = 0;
        if (!startElf()) {
            stop();
            auto message = lastError.empty() ? "Application failed to start." : std::format("Application failed to start: {}", lastError);
            alertdialog::start("Error", message);
            return;
        }

        if (staticParametersSetCount == 0) {
            stop();
            alertdialog::start("Error", "Application failed to start: application failed to register itself");
            return;
        }

        manifest = std::make_unique<Parameters>(staticParameters);
        lock.unlock();

        if (manifest->createData != nullptr) {
            data = manifest->createData();
        }

        if (manifest->onCreate != nullptr) {
            manifest->onCreate(&appContext, data);
        }
    }

    void onDestroy(AppContext& appContext) override {
        LOGGER.info("Cleaning up app");
        if (manifest != nullptr) {
            if (manifest->onDestroy != nullptr) {
                manifest->onDestroy(&appContext, data);
            }

            if (manifest->destroyData != nullptr && data != nullptr) {
                manifest->destroyData(data);
            }

            this->manifest = nullptr;
        }
        stopElf();
    }

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        if (manifest != nullptr && manifest->onShow != nullptr) {
            manifest->onShow(&appContext, data, parent);
        }
    }

    void onHide(AppContext& appContext) override {
        if (manifest != nullptr && manifest->onHide != nullptr) {
            manifest->onHide(&appContext, data);
        }
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> resultBundle) override {
        if (manifest != nullptr && manifest->onResult != nullptr) {
            manifest->onResult(&appContext, data, launchId, result, resultBundle.get());
        }
    }
};

ElfApp::Parameters ElfApp::staticParameters;
size_t ElfApp::staticParametersSetCount = 0;
std::shared_ptr<Lock> ElfApp::staticParametersLock = std::make_shared<Mutex>();

void setElfAppParameters(
    CreateData createData,
    DestroyData destroyData,
    OnCreate onCreate,
    OnDestroy onDestroy,
    OnShow onShow,
    OnHide onHide,
    OnResult onResult
) {
    ElfApp::setParameters({
        .createData = createData,
        .destroyData = destroyData,
        .onCreate = onCreate,
        .onDestroy = onDestroy,
        .onShow = onShow,
        .onHide = onHide,
        .onResult = onResult
    });
}

std::shared_ptr<App> createElfApp(const std::shared_ptr<AppManifest>& manifest) {
    LOGGER.info("createElfApp");
    assert(manifest != nullptr);
    assert(manifest->appLocation.isExternal());
    return std::make_shared<ElfApp>(manifest->appLocation.getPath());
}

} // namespace

#endif // ESP_PLATFORM
