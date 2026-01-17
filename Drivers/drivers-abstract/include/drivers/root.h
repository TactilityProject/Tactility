#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct root_config {
    const char* model;
};

struct root_api {
};

#define root_init nullptr
#define root_deinit nullptr

#ifdef __cplusplus
}
#endif
