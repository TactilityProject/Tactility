#pragma once

bool tusbIsSupported();
bool tusbStartMassStorageWithSdmmc(bool fromBootMode = false);
bool tusbStartMassStorageWithFlash();
void tusbStop();
bool tusbCanStartMassStorageWithFlash();