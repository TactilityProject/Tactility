#pragma once

#include <cstdint>
#include <string>

namespace tt::service::webserver {

/**
 * @brief Asset version tracking for web server content synchronization
 * 
 * Manages version.json files in both Data partition and SD card to ensure
 * proper synchronization and recovery of web assets.
 */
struct AssetVersion {
    uint32_t version;  // Integer version number
    
    AssetVersion() : version(0) {}
    explicit AssetVersion(uint32_t v) : version(v) {}
};

/**
 * @brief Load asset version from Data partition
 * @param[out] version The version structure to populate
 * @return true if version was loaded successfully
 */
bool loadDataVersion(AssetVersion& version);

/**
 * @brief Load asset version from SD card
 * @param[out] version The version structure to populate  
 * @return true if version was loaded successfully
 */
bool loadSdVersion(AssetVersion& version);

/**
 * @brief Save asset version to Data partition
 * @param[in] version The version to save
 * @return true if version was saved successfully
 */
bool saveDataVersion(const AssetVersion& version);

/**
 * @brief Save asset version to SD card
 * @param[in] version The version to save
 * @return true if version was saved successfully
 */
bool saveSdVersion(const AssetVersion& version);

/**
 * @brief Check if Data partition has any web assets
 * @return true if Data partition contains web assets
 */
bool hasDataAssets();

/**
 * @brief Check if SD card has any web assets
 * @return true if SD card contains web assets backup
 */
bool hasSdAssets();

/**
 * @brief Synchronize assets between Data partition and SD card based on version
 * 
 * Logic:
 * - If Data is empty: Copy from SD card (recovery mode)
 * - If SD version > Data version: Copy SD -> Data (firmware update)
 * - If Data version > SD version: Copy Data -> SD (backup customizations)
 * 
 * @return true if sync completed successfully
 */
bool syncAssets();

} // namespace
