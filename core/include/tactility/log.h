#pragma once

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#ifndef ESP_PLATFORM

void log_generic(const char* tag, const char* format, ...);

#define LOG_E(x, ...) log(x, ##__VA_ARGS__)
#define LOG_W(x, ...) log(x, ##__VA_ARGS__)
#define LOG_I(x, ...) log(x, ##__VA_ARGS__)
#define LOG_D(x, ...) log(x, ##__VA_ARGS__)
#define LOG_V(x, ...) log(x, ##__VA_ARGS__)

#else

#define LOG_E(x, ...) ESP_LOGD(x, ##__VA_ARGS__)
#define LOG_W(x, ...) ESP_LOGW(x, ##__VA_ARGS__)
#define LOG_I(x, ...) ESP_LOGI(x, ##__VA_ARGS__)
#define LOG_D(x, ...) ESP_LOGD(x, ##__VA_ARGS__)
#define LOG_V(x, ...) ESP_LOGV(x, ##__VA_ARGS__)

#endif