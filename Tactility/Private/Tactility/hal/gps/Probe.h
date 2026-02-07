#pragma once

struct Device;

namespace tt::hal::gps {

GpsModel probe(::Device* uart);

}
