#pragma once

namespace tt::bluetooth::settings {

void setEnableOnBoot(bool enable);
bool shouldEnableOnBoot();

void setSppAutoStart(bool enable);
bool shouldSppAutoStart();

void setMidiAutoStart(bool enable);
bool shouldMidiAutoStart();

} // namespace tt::bluetooth::settings
