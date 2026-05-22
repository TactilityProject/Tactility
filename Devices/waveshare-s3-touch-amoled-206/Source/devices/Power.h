#pragma once

#include <memory>
#include <Axp2101.h>

bool initAxp();
std::shared_ptr<Axp2101> getAxp2101();