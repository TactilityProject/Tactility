#pragma once

bool tusbIsSupported();
bool tusbStartMassStorageWithSdmmc(bool fromBootMode = false);
bool tusbStartMassStorageWithFlash(bool fromBootMode = false);
void tusbStop();
bool tusbCanStartMassStorageWithFlash();