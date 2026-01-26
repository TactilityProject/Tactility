#include <Tactility/Tactility.h>
#include <Tactility/file/File.h>
#include <Tactility/Logger.h>
#include <Tactility/network/Http.h>

#include "Tactility/service/gui/GuiService.h"

#ifdef ESP_PLATFORM
#include <Tactility/network/EspHttpClient.h>
#include <esp_http_client.h>
#endif

namespace tt::network::http {

static const auto LOGGER = Logger("HTTP");

void download(
    const std::string& url,
    const std::string& certFilePath,
    const std::string &downloadFilePath,
    const std::function<void()>& onSuccess,
    const std::function<void(const char* errorMessage)>& onError
) {
    service::gui::warnIfRunningOnGuiTask("HTTP");
    LOGGER.info("Downloading from {} to {}", url, downloadFilePath);
#ifdef ESP_PLATFORM
    getMainDispatcher().dispatch([url, certFilePath, downloadFilePath, onSuccess, onError] {
        LOGGER.info("Loading certificate");
        auto certificate = file::readString(certFilePath);
        if (certificate == nullptr) {
            onError("Failed to read certificate");
            return;
        }

        auto certificate_length = strlen(reinterpret_cast<const char*>(certificate.get())) + 1;

        // TODO: Fix for missing initializer warnings
        auto config = std::make_unique<esp_http_client_config_t>(esp_http_client_config_t {
            .url = url.c_str(),
            .auth_type = HTTP_AUTH_TYPE_NONE,
            .cert_pem = reinterpret_cast<const char*>(certificate.get()),
            .cert_len = certificate_length,
            .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 5000,
            .transport_type = HTTP_TRANSPORT_OVER_SSL
        });

        auto client = std::make_unique<EspHttpClient>();
        if (!client->init(std::move(config))) {
            onError("Failed to initialize client");
            return;
        }

        if (!client->open()) {
            onError("Failed to open connection");
            return;
        }

        if (!client->fetchHeaders()) {
            onError("Failed to get request headers");
            return;
        }

        if (!client->isStatusCodeOk()) {
            onError("Server response is not OK");
            return;
        }

        auto bytes_left = client->getContentLength();

        auto lock = file::getLock(downloadFilePath)->asScopedLock();
        lock.lock();
        LOGGER.info("opening {}", downloadFilePath);
        auto* file = fopen(downloadFilePath.c_str(), "wb");
        if (file == nullptr) {
            onError("Failed to open file");
            return;
        }

        LOGGER.info("Writing {} bytes to {}", bytes_left, downloadFilePath);
        char buffer[512];
        while (bytes_left > 0) {
            int data_read = client->read(buffer, 512);
            if (data_read <= 0) {
                fclose(file);
                onError("Failed to read data");
                return;
            }
            bytes_left -= data_read;
            if (fwrite(buffer, 1, data_read, file) != data_read) {
                fclose(file);
                onError("Failed to write all bytes");
                return;
            }
            taskYIELD();
        }
        fclose(file);
        LOGGER.info("Downloaded {} to {}", url, downloadFilePath);
        onSuccess();
    });
#else
    getMainDispatcher().dispatch([onError] {
        onError("Not implemented");
    });
#endif
}

}
