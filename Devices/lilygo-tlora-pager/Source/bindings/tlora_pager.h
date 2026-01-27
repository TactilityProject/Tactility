#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/root.h>
#include <drivers/TloraPager.h>

DEFINE_DEVICETREE(tlora_pager, struct RootConfig)

#ifdef __cplusplus
}
#endif
