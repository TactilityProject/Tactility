// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

// Registers the GPS service manifest with the kernel service manager (auto-started). Called once
// from module.cpp's Module::start().
error_t gps_service_register();
