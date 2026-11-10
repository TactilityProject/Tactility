#pragma once

bool tusbIsSupported();
bool tusbStartMassStorageWithSdmmc(bool fromBootMode = false);
bool tusbStartMassStorageWithInternalStorage(bool fromBootMode = false);
void tusbStop();
bool tusbCanStartMassStorageWithInternalStorage();

// Reason the most recent tusbStartMassStorageWith*() call failed, or "" if it succeeded.
const char* tusbGetLastError();