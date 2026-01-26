#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Tactility/bindings/bindings.h>
#include <Tactility/drivers/Root.h>
#include <drivers/TloraPager.h>

DEFINE_DEVICETREE(tlora_pager, struct RootConfig)

#ifdef __cplusplus
}
#endif
