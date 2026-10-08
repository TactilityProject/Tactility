#ifdef ESP_PLATFORM

#include <Tactility/app/crashdiagnostics/CrashDiagnostics.h>

#include <Tactility/PanicHandler.h>
#include <Tactility/app/boot/BootScreen.h>
#include <Tactility/app/crashdiagnostics/QrHelpers.h>
#include <Tactility/app/crashdiagnostics/QrUrl.h>
#include <Tactility/file/File.h>

#include <qrcode.h>
#include <tactility/delay.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include <esp_cpu_utils.h>
#else
#include <esp_cpu.h>
#endif

#include <sdkconfig.h>

#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace tt::app::crashdiagnostics {

constexpr auto* TAG = "CrashDiagnostics";

namespace {

const char* crashCauseToString(CrashCause cause) {
    switch (cause) {
        case CrashCause::Debug: return "Debug";
        case CrashCause::WatchdogInterrupt: return "Watchdog (interrupt)";
        case CrashCause::WatchdogTask: return "Watchdog (task)";
        case CrashCause::Abort: return "Abort";
        case CrashCause::Fault: return "Fault";
        case CrashCause::Unknown:
        default: return "Unknown";
    }
}

std::string formatCrashData(const CrashData& crashData) {
    std::stringstream stream;

    stream << "Cause: " << crashCauseToString(crashData.cause) << "\n";

    stream << "Reason: ";
    if (crashData.reason[0] != '\0') {
         stream << crashData.reason;
    } else {
        stream << "unknown";
    }
    stream << "\n";

    stream << "Fault address: " << std::hex << std::setw(8) << std::setfill('0') << crashData.faultAddress << std::dec << "\n";

    stream << "Callstack" << (crashData.callstackCorrupted ? " (corrupted)" : "") << ":";
    if (crashData.callstackLength > 0) {
        stream << "\n";
        for (uint8_t i = 0; i < crashData.callstackLength; i++) {
#if CONFIG_IDF_TARGET_ARCH_XTENSA
            uint32_t pc = esp_cpu_process_stack_pc(crashData.callstack[i].pc);
#else
            uint32_t pc = crashData.callstack[i].pc; // No processing needed on RISC-V
#endif
            stream << std::hex << std::setw(8) << std::setfill('0') << pc << std::dec << " ";
        }
    } else {
        stream << " empty" << "\n";
    }

    return stream.str();
}

// Best-effort: crash.txt is a convenience for offline inspection, not required for the app to work.
void writeCrashLogFile(const CrashData& crashData) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve data path for crash.txt");
        return;
    }

    std::string path = std::string(root) + "/crash.txt";
    if (!file::writeString(path, formatCrashData(crashData))) {
        LOG_E(TAG, "Failed to write %s", path.c_str());
    }
}

void waitForInputOrForever(bool hasInput) {
    if (hasInput) {
        boot::waitForInput();
    } else {
        // Without input to continue with, the device has to be restarted
        while (true) {
            delay_millis(1000);
        }
    }
}

} // namespace

void showCrashScreen(boot::BootScreen& screen) {
    const auto& crash_data = getRtcCrashData();
    writeCrashLogFile(crash_data);

    std::string prompt = boot::getInputPrompt("continue");
    const bool has_input = !prompt.empty();
    if (!has_input) {
        prompt = "Restart device";
    }
    // Without a callstack, there's nothing worth reporting through the QR code
    if (crash_data.callstackLength == 0) {
        std::vector<std::string> lines = { "Oops! We've crashed ..."};
        if (crash_data.cause != CrashCause::Unknown) {
            lines.push_back(std::string("Only the cause is known: ") + crashCauseToString(crash_data.cause));
        }
        lines.push_back(prompt);
        screen.show("", lines);
        waitForInputOrForever(has_input);
        return;
    }

    const std::vector<std::string> lines = { "Oops! We've crashed ...", prompt };

    // The QR code links to a page that shows the crash details
    const std::string url = getUrlFromCrashData(crash_data);
    LOG_I(TAG, "%s", url.c_str());
    int qr_version;
    std::unique_ptr<uint8_t[]> qr_buffer;
    QRCode qr_code;
    bool has_qr_code = false;
    if (!getQrVersionForBinaryDataLength(url.length(), qr_version)) {
        LOG_E(TAG, "QR is too large");
    } else {
        qr_buffer.reset(new (std::nothrow) uint8_t[qrcode_getBufferSize(qr_version)]);
        if (qr_buffer == nullptr) {
            LOG_E(TAG, "Failed to allocate QR buffer");
        } else if (qrcode_initText(&qr_code, qr_buffer.get(), qr_version, ECC_LOW, url.c_str()) != 0) {
            LOG_E(TAG, "QR init text failed");
        } else {
            has_qr_code = true;
        }
    }

    if (has_qr_code) {
        screen.showQrCode(qr_code.size, [&qr_code](int x, int y) {
            return qrcode_getModule(&qr_code, static_cast<uint8_t>(x), static_cast<uint8_t>(y));
        }, lines);
    } else {
        screen.show("", lines);
    }

    waitForInputOrForever(has_input);
}

}

#endif
