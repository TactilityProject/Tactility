#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <drivers/t5s3_display.h>

DEFINE_DEVICETREE(t5s3_display, struct T5s3DisplayConfig)

#ifdef __cplusplus
}
#endif
