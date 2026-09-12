#pragma once

bool tusbIsSupported();
bool tusbStartMassStorageWithSdmmc(bool fromBootMode = false);
bool tusbStartMassStorageWithFlash(bool fromBootMode = false);
void tusbStop();
bool tusbCanStartMassStorageWithFlash();

// Reason the most recent tusbStartMassStorageWith*() call failed, or "" if it succeeded.
const char* tusbGetLastError();