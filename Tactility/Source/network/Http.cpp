#include <Tactility/Tactility.h>
#include <Tactility/file/File.h>
#include <Tactility/network/Http.h>

#include "Tactility/service/gui/GuiService.h"

#include <tactility/log.h>

#ifdef ESP_PLATFORM
#include <Tactility/network/EspHttpClient.h>
#include <esp_http_client.h>
#endif

namespace tt::network::http {

constexpr auto* TAG = "HTTP";

void download(
    const std::string& url,
    const std::string& certFilePath,
    const std::string &downloadFilePath,
    const std::function<void()>& onSuccess,
    const std::function<void(const char* errorMessage)>& onError
) {
    service::gui::warnIfRunningOnGuiTask("HTTP");
    LOG_I(TAG, "Downloading from %s to %s", url.c_str(), downloadFilePath.c_str());
#ifdef ESP_PLATFORM
    getMainDispatcher().dispatch([url, certFilePath, downloadFilePath, onSuccess, onError] {
        LOG_I(TAG, "Loading certificate");
        auto certificate = file::readString(certFilePath);
        if (certificate == nullptr) {
            onError("Failed to read certificate");
            return;
        }

        auto certificate_length = strlen(reinterpret_cast<const char*>(certificate.get())) + 1;

        // TODO: Fix for missing initializer warnings
        auto config = std::make_unique<esp_http_client_config_t>();
        memset(config.get(), 0, sizeof(esp_http_client_config_t));
        config->url = url.c_str();
        config->auth_type = HTTP_AUTH_TYPE_NONE;
        config->cert_pem = reinterpret_cast<const char*>(certificate.get());
        config->cert_len = certificate_length;
        config->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
        config->method = HTTP_METHOD_GET;
        config->timeout_ms = 5000;
        config->transport_type = HTTP_TRANSPORT_OVER_SSL;

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
        LOG_I(TAG, "opening %s", downloadFilePath.c_str());
        auto* file = fopen(downloadFilePath.c_str(), "wb");
        if (file == nullptr) {
            onError("Failed to open file");
            return;
        }

        LOG_I(TAG, "Writing %d bytes to %s", bytes_left, downloadFilePath.c_str());
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
        LOG_I(TAG, "Downloaded %s to %s", url.c_str(), downloadFilePath.c_str());
        onSuccess();
    });
#else
    getMainDispatcher().dispatch([onError] {
        onError("Not implemented");
    });
#endif
}

}
