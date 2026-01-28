#pragma once

#include <esp_err.h>

#include <tactility/error.h>

/** Convert an esp_err_t to an error_t */
error_t esp_err_to_error(esp_err_t error);
