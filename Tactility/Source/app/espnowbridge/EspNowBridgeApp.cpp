#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/Tactility.h>
#include <Tactility/app/App.h>
#include <Tactility/app/fileselection/FileSelection.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/espnow/EspNowHostedTransport.h>
#include <Tactility/PubSub.h>
#include <Tactility/Thread.h>

#include <esp_system.h>

#include <esp_hosted.h>
extern "C" {
#include <esp_hosted_ota.h>
}
#include <esp_hosted_api_types.h>
#include <esp_app_format.h>
#include <esp_app_desc.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <lvgl.h>

#include <tactility/log.h>
#include <tactility/lvgl_icon_shared.h>

namespace tt::app::espnowbridge {

constexpr auto* TAG = "EspNowBridgeApp";
constexpr size_t CHUNK_SIZE = 1500;
constexpr uint32_t TRANSPORT_WAIT_TIMEOUT_MS = 5000;
constexpr uint32_t UPDATE_TASK_STACK_SIZE = 8192;
/** esp_hosted_slave_ota_activate() reboots the co-processor ~2s later (per its own log message).
 *  Surviving that reboot without a host restart turned out to be unreliable on real hardware:
 *  pausing WiFi's auto-scan (still done below, belt-and-suspenders) isn't enough because
 *  esp_hosted's own internal transport traffic (independent of anything Tactility triggers) can
 *  still hit the SDIO bus while the co-processor is briefly gone and exhaust esp_hosted's thin
 *  write-retry budget ("Unrecoverable host sdio state" -> forced host restart) - reproduced even
 *  during a plain delay with zero app-level activity. Actively reconnecting
 *  (esp_hosted_connect_to_slave()/waitForHostedTransport()) is worse: it performs a hard GPIO
 *  reset of the co-processor when esp_hosted's transport_state believes the link is down, which
 *  stacks a second, uncoordinated hardware reset on top of the co-processor's own OTA reboot and
 *  reliably fails the immediate post-reset register read.
 *
 *  A confirm-dialog ("press Restart Now") was tried and reverted: if the user didn't react fast
 *  enough, the co-processor's own background traffic still crashed the host before they pressed
 *  it. Given none of this is reliably fixable from the app layer, this instead matches M5Stack's
 *  own ESP-Hosted firmware upgrade tool: after a successful activate, restart the whole host
 *  automatically (after a brief moment for the status message to be visible) rather than trying
 *  to keep it running across the co-processor's reboot or waiting on human reaction time. */

/** RAII guard: pauses WifiService's background auto-connect scan for the guard's lifetime.
 *  Kept as a belt-and-suspenders measure even though it wasn't sufficient on its own (see
 *  REBOOT_PROMPT_TITLE comment above) - it still avoids one known contributor. */
class AutoScanPauseGuard {
public:
    AutoScanPauseGuard() { service::wifi::setAutoScanPaused(true); }
    ~AutoScanPauseGuard() { service::wifi::setAutoScanPaused(false); }
    AutoScanPauseGuard(const AutoScanPauseGuard&) = delete;
    AutoScanPauseGuard& operator=(const AutoScanPauseGuard&) = delete;
};

// Binary partition table format (gen_esp32part.py STRUCT_FORMAT '<2sBBLL16sL'): a flat array of
// 32-byte little-endian records starting at flash offset PARTITION_TABLE_OFFSET, terminated by
// an all-0xFF entry or an MD5-checksum record (magic 0xEBEB). Not exposed as a C header by
// ESP-IDF (only the Python generator knows the format) - this is a hand-ported minimal reader,
// just enough to locate the app partition inside a merged/factory bin.
constexpr size_t PARTITION_TABLE_OFFSET = 0x8000;
constexpr size_t PARTITION_TABLE_MAX_ENTRIES = 128; // covers the largest partition table IDF supports (0x1000 / 32)
constexpr uint16_t PARTITION_ENTRY_MAGIC = 0x50AA; // little-endian bytes 0xAA, 0x50
constexpr uint16_t PARTITION_MD5_MAGIC = 0xEBEB;
constexpr uint8_t PARTITION_TYPE_APP = 0x00;
constexpr uint8_t PARTITION_SUBTYPE_FACTORY = 0x00;
constexpr uint8_t PARTITION_SUBTYPE_OTA_0 = 0x10;

struct __attribute__((packed)) PartitionEntry {
    uint16_t magic;
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
    char name[16];
    uint32_t flags;
};
static_assert(sizeof(PartitionEntry) == 32, "partition table entry must be 32 bytes");

/**
 * Scans the partition table embedded in a merged/factory bin (at PARTITION_TABLE_OFFSET) for
 * the app partition to flash: prefers "factory" if present, otherwise the first OTA slot
 * (ota_0) - matches what a real M5Stack ESP-Hosted factory image contains (verified against a
 * real example: nvs/otadata/phy_init/ota_0/ota_1, no factory partition).
 * @return true if an app partition was found, with appOffset/appSize set to its location
 * within the file (these are the same as the absolute flash offsets the merged bin preserves).
 */
static bool findAppPartitionInMergedBin(FILE* file, size_t& appOffset, size_t& appSize) {
    if (fseek(file, static_cast<long>(PARTITION_TABLE_OFFSET), SEEK_SET) != 0) {
        return false;
    }

    bool foundFactory = false;
    bool foundOta0 = false;
    size_t factoryOffset = 0, factorySize = 0;
    size_t ota0Offset = 0, ota0Size = 0;

    for (size_t i = 0; i < PARTITION_TABLE_MAX_ENTRIES; i++) {
        PartitionEntry entry;
        if (fread(&entry, 1, sizeof(entry), file) != sizeof(entry)) {
            break;
        }
        if (entry.magic == PARTITION_MD5_MAGIC) {
            break;
        }
        if (entry.magic != PARTITION_ENTRY_MAGIC) {
            break;
        }
        if (entry.type == PARTITION_TYPE_APP) {
            if (entry.subtype == PARTITION_SUBTYPE_FACTORY) {
                foundFactory = true;
                factoryOffset = entry.offset;
                factorySize = entry.size;
            } else if (entry.subtype == PARTITION_SUBTYPE_OTA_0 && !foundOta0) {
                foundOta0 = true;
                ota0Offset = entry.offset;
                ota0Size = entry.size;
            }
        }
    }

    if (foundFactory) {
        appOffset = factoryOffset;
        appSize = factorySize;
        return true;
    }
    if (foundOta0) {
        appOffset = ota0Offset;
        appSize = ota0Size;
        return true;
    }
    return false;
}

/**
 * Validates the app image at the given file offset and extracts its version string. The actual
 * transfer size used for the OTA loop is just the real remaining file size from appOffset (see
 * performUpdate) - hand-computing the image's "logical" size from segment headers + checksum/
 * hash padding is fiddly and was found to drift a bit short of the real length (a previous
 * version of this function did that and caused the last chunk's fread() to come up short,
 * corrupting the transfer). We already have the real length for free, so just use it.
 */
static bool parseImageHeader(FILE* file, size_t appOffset, char* versionOut, size_t versionOutLen, std::string* errorOut = nullptr) {
    esp_image_header_t imageHeader;
    if (fseek(file, static_cast<long>(appOffset), SEEK_SET) != 0 ||
        fread(&imageHeader, 1, sizeof(imageHeader), file) != sizeof(imageHeader)) {
        LOG_E(TAG, "Failed to read image header");
        if (errorOut != nullptr) {
            *errorOut = "Failed to read image header";
        }
        return false;
    }

    if (imageHeader.magic != ESP_IMAGE_HEADER_MAGIC) {
        LOG_E(TAG, "Selected file is not a valid firmware image (bad magic)");
        if (errorOut != nullptr) {
            *errorOut = "Selected file is not a valid firmware image (bad magic)";
        }
        return false;
    }

    // Fail fast on a wrong-chip image (e.g. an ESP32 or S3 binary picked by mistake) before
    // streaming the whole file over the paced, slow bridge link - esp_hosted_slave_ota_end()
    // would eventually catch this too, but only after the entire transfer already completed.
    if (imageHeader.chip_id != ESP_CHIP_ID_ESP32C6) {
        LOG_E(TAG, "Selected file targets chip id %u, expected ESP32-C6 (%u)",
            (unsigned)imageHeader.chip_id, (unsigned)ESP_CHIP_ID_ESP32C6);
        if (errorOut != nullptr) {
            *errorOut = std::format("Wrong chip: image targets chip id {}, expected ESP32-C6",
                (unsigned)imageHeader.chip_id);
        }
        return false;
    }

    esp_image_segment_header_t segmentHeader;
    size_t firstSegmentOffset = appOffset + sizeof(imageHeader);
    if (fseek(file, static_cast<long>(firstSegmentOffset), SEEK_SET) != 0 ||
        fread(&segmentHeader, 1, sizeof(segmentHeader), file) != sizeof(segmentHeader)) {
        LOG_E(TAG, "Failed to read first segment header");
        if (errorOut != nullptr) {
            *errorOut = "Failed to read first segment header";
        }
        return false;
    }

    esp_app_desc_t appDesc;
    size_t appDescOffset = appOffset + sizeof(imageHeader) + sizeof(segmentHeader);
    if (fseek(file, static_cast<long>(appDescOffset), SEEK_SET) == 0 && fread(&appDesc, 1, sizeof(appDesc), file) == sizeof(appDesc)) {
        strncpy(versionOut, appDesc.version, versionOutLen - 1);
        versionOut[versionOutLen - 1] = '\0';
    } else {
        strncpy(versionOut, "unknown", versionOutLen - 1);
        versionOut[versionOutLen - 1] = '\0';
    }

    return true;
}

static bool getCurrentVersionString(char* versionOut, size_t versionOutLen) {
    esp_hosted_coprocessor_fwver_t currentVersion = {};
    if (esp_hosted_get_coprocessor_fwversion(&currentVersion) != ESP_OK) {
        return false;
    }
    snprintf(versionOut, versionOutLen, "%u.%u.%u",
        (unsigned)currentVersion.major1, (unsigned)currentVersion.minor1, (unsigned)currentVersion.patch1);
    return true;
}

extern const AppManifest manifest;

class EspNowBridgeApp;

static std::shared_ptr<EspNowBridgeApp> optApp() {
    auto appContext = getCurrentAppContext();
    if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
        return std::static_pointer_cast<EspNowBridgeApp>(appContext->getApp());
    }
    return nullptr;
}

class EspNowBridgeApp final : public App, public std::enable_shared_from_this<EspNowBridgeApp> {
    LaunchId pickFileLaunchId = 0;
    std::string pendingUpdateFilePath;
    PubSub<service::wifi::WifiEvent>::SubscriptionHandle wifiSubscription = nullptr;
    Thread updateThread;
    bool isShown = false;
    // Outlives performUpdate() deliberately, so auto-scan stays paused across the async gap
    // between performUpdate() returning and the user actually pressing the restart prompt's
    // button - see promptForHostRestart(). The local-variable version of this guard (previous
    // iteration) was destroyed the instant performUpdate() returned, i.e. before the dialog was
    // even shown, which defeated its whole purpose. setAutoScanPaused() itself is just a
    // mutex-guarded flag set, safe to call from any task.
    std::optional<AutoScanPauseGuard> heldAutoScanPauseGuard;

    lv_obj_t* currentVersionLabel = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* progressBar = nullptr;
    lv_obj_t* updateButton = nullptr;
    lv_obj_t* enableWifiButton = nullptr;

    void refreshCurrentVersion() {
        char versionStr[32];
        if (getCurrentVersionString(versionStr, sizeof(versionStr))) {
            lv_label_set_text_fmt(currentVersionLabel, "Co-processor firmware: %s", versionStr);
        } else {
            lv_label_set_text(currentVersionLabel, "Co-processor firmware: unknown (link not up)");
        }
    }

    /** ESP-NOW bridge RPC calls need the esp_hosted transport up, which in turn needs the WiFi
     *  radio enabled (esp_hosted's transport doesn't come up automatically at boot - it starts
     *  as a side effect of the WiFi/esp_wifi_remote stack being turned on). */
    bool isWifiRadioOn() {
        auto state = service::wifi::getRadioState();
        return state == service::wifi::RadioState::On ||
            state == service::wifi::RadioState::ConnectionPending ||
            state == service::wifi::RadioState::ConnectionActive;
    }

    void refreshWifiPrompt() {
        if (isWifiRadioOn()) {
            lv_obj_add_flag(enableWifiButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(updateButton, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_flag(enableWifiButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(updateButton, LV_STATE_DISABLED);
        }
    }

    void setStatus(const std::string& text) {
        lv_label_set_text(statusLabel, text.c_str());
    }

    void setProgress(int percent) {
        lv_bar_set_value(progressBar, percent, LV_ANIM_OFF);
    }

    /** Dispatches a UI-touching closure onto the LVGL task. The worker thread that runs
     *  performUpdate() must never touch lv_obj_t* directly - LVGL isn't thread-safe against
     *  arbitrary tasks. A shared_ptr keeps this app instance alive until the callback runs, in
     *  case the app is torn down while the update is still in flight; isShown additionally
     *  guards against touching lv_obj_t* members that onHide() already tore down (this app's
     *  widget tree is destroyed on hide/app-switch, but the App object itself may outlive that
     *  via the shared_ptr). */
    void dispatchToUi(std::function<void(EspNowBridgeApp&)> work) {
        using Payload = std::pair<std::shared_ptr<EspNowBridgeApp>, std::function<void(EspNowBridgeApp&)>>;
        auto* payload = new Payload(shared_from_this(), std::move(work));
        lv_result_t result = lv_async_call([](void* userData) {
            auto* payload = static_cast<Payload*>(userData);
            if (payload->first->isShown) {
                payload->second(*payload->first);
            }
            delete payload;
        }, payload);
        if (result != LV_RESULT_OK) {
            LOG_E(TAG, "lv_async_call() failed to schedule UI update");
            delete payload;
        }
    }

    /** Runs on a dedicated worker thread (see onUpdateButtonClicked) rather than the LVGL/gui
     *  task - the gui task's stack is only 4096 bytes with little headroom (see GuiService.cpp),
     *  and this function's local chunk buffer plus the esp_hosted/esp_now call chain underneath
     *  esp_hosted_slave_ota_write() was deep enough to risk a stack overflow there. All lv_obj_t*
     *  touches are marshaled back to the LVGL task via dispatchToUi(). */
    void performUpdate(const std::string& filePath) {
        dispatchToUi([](EspNowBridgeApp& app) {
            lv_obj_add_state(app.updateButton, LV_STATE_DISABLED);
            app.setProgress(0);
            app.setStatus("Waiting for co-processor link...");
        });

        if (!service::espnow::backend::waitForHostedTransport(TRANSPORT_WAIT_TIMEOUT_MS)) {
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Co-processor link not available - update cancelled");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        FILE* file = fopen(filePath.c_str(), "rb");
        if (file == nullptr) {
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Failed to open selected file");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        fseek(file, 0, SEEK_END);
        long fileSizeSigned = ftell(file);
        if (fileSizeSigned <= 0) {
            fclose(file);
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Failed to determine file size");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }
        size_t fileSize = static_cast<size_t>(fileSizeSigned);

        // Support both a plain app image (e.g. this bridge's own network_adapter.bin, starting
        // with the app image header at offset 0) and a merged/factory bin (e.g. M5Stack's
        // official ESP-Hosted factory image, which bundles bootloader + partition table + app
        // at their real flash offsets) - useful for downgrading to factory firmware or similar.
        // Detected by whether a valid partition table is found at PARTITION_TABLE_OFFSET; a
        // plain app image doesn't have one there (that offset is just app segment data).
        size_t appOffset = 0;
        size_t partitionSize = 0;
        bool isMergedBin = findAppPartitionInMergedBin(file, appOffset, partitionSize);
        if (isMergedBin && appOffset >= fileSize) {
            fclose(file);
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Merged bin's app partition is outside the file - selected file looks truncated");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        char newVersion[32];
        std::string parseError;
        if (!parseImageHeader(file, appOffset, newVersion, sizeof(newVersion), &parseError)) {
            fclose(file);
            dispatchToUi([parseError](EspNowBridgeApp& app) {
                app.setStatus(parseError);
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        // Merged bins pad the app partition to its declared size; a plain app image is exactly
        // as long as the app itself. Transfer whichever is smaller so a merged bin's transfer
        // doesn't include trailing 0xFF padding, and a plain image isn't truncated.
        size_t remainingInFile = fileSize - appOffset;
        size_t firmwareSize = isMergedBin ? std::min(partitionSize, remainingInFile) : remainingInFile;

        LOG_I(TAG, "Pushing bridge firmware %s to co-processor (%zu bytes%s)...", newVersion, firmwareSize,
            isMergedBin ? ", extracted from merged bin" : "");
        std::string versionStr(newVersion);
        dispatchToUi([versionStr](EspNowBridgeApp& app) {
            app.setStatus(std::format("Pushing firmware {}...", versionStr));
        });

        // Held on the app instance (not a local variable) so it outlives this function - see
        // heldAutoScanPauseGuard's declaration for why. Released when the host actually restarts
        // (moot, since esp_restart() doesn't return) or if the update fails early below.
        heldAutoScanPauseGuard.emplace();

        if (esp_hosted_slave_ota_begin() != ESP_OK) {
            fclose(file);
            heldAutoScanPauseGuard.reset();
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Failed to start OTA on co-processor");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        if (fseek(file, static_cast<long>(appOffset), SEEK_SET) != 0) {
            fclose(file);
            esp_hosted_slave_ota_end();
            heldAutoScanPauseGuard.reset();
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Failed to seek to firmware start");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        uint8_t chunk[CHUNK_SIZE];
        size_t sent = 0;
        bool writeFailed = false;
        int lastReportedPercent = -1;

        while (sent < firmwareSize) {
            size_t toRead = (firmwareSize - sent > CHUNK_SIZE) ? CHUNK_SIZE : (firmwareSize - sent);
            size_t actuallyRead = fread(chunk, 1, toRead, file);
            if (actuallyRead != toRead) {
                LOG_E(TAG, "Failed to read file at offset %zu", sent);
                writeFailed = true;
                break;
            }

            if (esp_hosted_slave_ota_write(chunk, actuallyRead) != ESP_OK) {
                LOG_E(TAG, "esp_hosted_slave_ota_write() failed at offset %zu", sent);
                writeFailed = true;
                break;
            }

            // Pace the transfer - esp_hosted's SDIO driver only retries a write twice with no
            // backoff (MAX_SDIO_WRITE_RETRY in sdio_drv.c) before giving up and restarting the
            // host. Back-to-back chunk writes with zero gap were observed to saturate the bus
            // enough to trigger a genuine sdmmc_send_cmd timeout (0x107) mid-transfer, not just
            // around the post-activate reboot. A small per-chunk delay gives the bus/co-processor
            // room to drain instead of relying on esp_hosted's thin retry budget to absorb it.
            vTaskDelay(pdMS_TO_TICKS(5));

            sent += actuallyRead;

            // Only touch LVGL every couple of percent, not every 1500-byte chunk - frequent
            // display-bus activity during the transfer was implicated in SDIO transport crashes
            // ("Unrecoverable host sdio state") under sustained OTA write load.
            int percent = (int)((sent * 100) / firmwareSize);
            if (percent != lastReportedPercent) {
                dispatchToUi([percent](EspNowBridgeApp& app) {
                    app.setProgress(percent);
                });
                lastReportedPercent = percent;
            }
        }

        fclose(file);

        if (writeFailed) {
            esp_hosted_slave_ota_end();
            heldAutoScanPauseGuard.reset();
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Update failed while transferring firmware");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        if (esp_hosted_slave_ota_end() != ESP_OK) {
            heldAutoScanPauseGuard.reset();
            dispatchToUi([](EspNowBridgeApp& app) {
                app.setStatus("Failed to finalize OTA on co-processor");
                lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
            });
            return;
        }

        // esp_hosted_slave_ota_activate() is only implemented by slave firmware >= v2.6.0 (older
        // slaves reject/lack the RPC entirely). Check the *currently running* (pre-update) slave
        // version - the new image isn't running yet - and skip straight to the required host
        // restart for older slaves, same as upstream's host_performs_slave_ota example.
        esp_hosted_coprocessor_fwver_t runningVersion = {};
        bool activateSupported = esp_hosted_get_coprocessor_fwversion(&runningVersion) == ESP_OK &&
            ((runningVersion.major1 > 2) || (runningVersion.major1 == 2 && runningVersion.minor1 > 5));

        if (activateSupported) {
            if (esp_hosted_slave_ota_activate() != ESP_OK) {
                heldAutoScanPauseGuard.reset();
                dispatchToUi([](EspNowBridgeApp& app) {
                    app.setStatus("Failed to activate new firmware - co-processor still running old firmware");
                    lv_obj_clear_state(app.updateButton, LV_STATE_DISABLED);
                });
                return;
            }
            LOG_I(TAG, "Firmware %s activated - co-processor is rebooting", versionStr.c_str());
        } else {
            LOG_I(TAG, "Firmware %s written - activate API not supported by running slave (<v2.6.0), "
                "restarting host to resync", versionStr.c_str());
        }

        // heldAutoScanPauseGuard is deliberately left held (never explicitly released) - the
        // host restarts itself immediately below, and there's no safe window to resume normal
        // WiFi activity before that (see the comment on REBOOT_PROMPT_TITLE above: waiting on a
        // user-dismissed dialog instead of restarting immediately was tried and still let the
        // co-processor's own background traffic crash the host if the user didn't react fast
        // enough - restarting automatically removes human reaction time from the equation).
        dispatchToUi([versionStr](EspNowBridgeApp& app) {
            app.setStatus(std::format("Firmware {} activated - restarting Tactility...", versionStr));
        });

        // Give the status message above a moment to actually be seen before the restart cuts
        // the display, then restart. No dialog/button - see comment above for why.
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }

    static void onUpdateButtonClicked(lv_event_t* event) {
        auto app = optApp();
        if (app == nullptr || !app->isWifiRadioOn()) {
            return;
        }
        app->pickFileLaunchId = fileselection::startForExistingFile();
    }

    static void onEnableWifiButtonClicked(lv_event_t* event) {
        service::wifi::setEnabled(true);
    }

    void onWifiEvent() {
        refreshWifiPrompt();
        refreshCurrentVersion();
    }

    void onShow(AppContext& appContext, lv_obj_t* parent) override {
        isShown = true;

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lvgl::toolbar_create(parent, appContext);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(wrapper, 8, LV_STATE_DEFAULT);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);

        auto* title = lv_label_create(wrapper);
        lv_label_set_text(title, "ESP-NOW Bridge Co-Processor");
        lv_obj_set_style_pad_bottom(title, 8, LV_STATE_DEFAULT);

        currentVersionLabel = lv_label_create(wrapper);
        lv_obj_set_style_pad_bottom(currentVersionLabel, 12, LV_STATE_DEFAULT);

        enableWifiButton = lv_button_create(wrapper);
        lv_obj_add_event_cb(enableWifiButton, onEnableWifiButtonClicked, LV_EVENT_CLICKED, nullptr);
        auto* enableWifiButtonLabel = lv_label_create(enableWifiButton);
        lv_label_set_text(enableWifiButtonLabel, "Enable WiFi (required for co-processor link)");
        lv_obj_set_style_pad_bottom(enableWifiButton, 12, LV_STATE_DEFAULT);

        updateButton = lv_button_create(wrapper);
        lv_obj_add_event_cb(updateButton, onUpdateButtonClicked, LV_EVENT_CLICKED, nullptr);
        auto* updateButtonLabel = lv_label_create(updateButton);
        lv_label_set_text(updateButtonLabel, "Update from SD card...");
        lv_obj_set_style_pad_bottom(updateButton, 12, LV_STATE_DEFAULT);

        progressBar = lv_bar_create(wrapper);
        lv_obj_set_width(progressBar, LV_PCT(100));
        lv_bar_set_range(progressBar, 0, 100);
        lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
        lv_obj_set_style_pad_bottom(progressBar, 8, LV_STATE_DEFAULT);

        statusLabel = lv_label_create(wrapper);
        lv_label_set_text(statusLabel, "Ready");

        refreshCurrentVersion();
        refreshWifiPrompt();

        // Subscribed only now that all widgets referenced by onWifiEvent() (via
        // refreshWifiPrompt()/refreshCurrentVersion()) exist - a weak reference is captured (not
        // `this`) so a WiFi event racing app teardown can't touch a freed EspNowBridgeApp, and
        // dispatchToUi() marshals onto the LVGL task and re-checks isShown before touching any
        // lv_obj_t* (onHide() destroys this app's whole widget tree on hide/app-switch).
        std::weak_ptr<EspNowBridgeApp> weakSelf = weak_from_this();
        wifiSubscription = service::wifi::getPubsub()->subscribe([weakSelf](auto /*event*/) {
            auto self = weakSelf.lock();
            if (self == nullptr) {
                return;
            }
            self->dispatchToUi([](EspNowBridgeApp& app) {
                app.onWifiEvent();
            });
        });

        // If a file was picked before this onShow() ran (FileSelection tears down and rebuilds
        // this app's whole widget tree), perform the update now that widgets are valid again -
        // doing it directly from onResult() would touch lv_obj_t* members mid app-switch/teardown.
        if (!pendingUpdateFilePath.empty()) {
            std::string path = std::move(pendingUpdateFilePath);
            pendingUpdateFilePath.clear();
            startUpdateThread(path);
        }
    }

    void startUpdateThread(const std::string& filePath) {
        if (updateThread.getState() != Thread::State::Stopped) {
            return;
        }
        // Previous run (if any) has State::Stopped but may not have been joined yet - join()
        // returns immediately once the task handle is cleared, which happens right after the
        // Stopped transition, so this is effectively non-blocking in practice.
        updateThread.join();
        updateThread.setName("espnow_bridge_ota");
        updateThread.setStackSize(UPDATE_TASK_STACK_SIZE);
        // Captures a shared_ptr (not raw `this`) so this app instance stays alive for the whole
        // OTA worker run even if it's torn down (app switched away and dropped) while the update
        // is in flight - performUpdate() itself already marshals all lv_obj_t* touches through
        // dispatchToUi(), which separately re-checks isShown/liveness.
        auto self = shared_from_this();
        updateThread.setMainFunction([self, filePath]() -> int32_t {
            self->performUpdate(filePath);
            return 0;
        });
        updateThread.start();
    }

    void onHide(AppContext& /*appContext*/) override {
        isShown = false;
        if (wifiSubscription != nullptr) {
            service::wifi::getPubsub()->unsubscribe(wifiSubscription);
            wifiSubscription = nullptr;
        }
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> resultData) override {
        if (launchId != pickFileLaunchId) {
            return;
        }
        pickFileLaunchId = 0;

        if (result == Result::Ok && resultData != nullptr) {
            auto path = fileselection::getResultPath(*resultData);
            if (!path.empty()) {
                pendingUpdateFilePath = path;
            }
        }
    }
};

extern const AppManifest manifest = {
    .appId = "EspNowBridge",
    .appName = "ESP-NOW Bridge",
    .appIcon = LVGL_ICON_SHARED_DEVICES,
    .appCategory = Category::Settings,
    .createApp = create<EspNowBridgeApp>
};

} // namespace

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
