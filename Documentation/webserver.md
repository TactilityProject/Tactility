# WebServer Service

The WebServer service provides a built-in HTTP server for remote device management, file operations, and system monitoring through a web browser.

## Features

- **Dashboard**: Real-time system information, memory stats, and storage overview
- **File Browser**: Navigate, upload, download, rename, and delete files on internal storage and SD card
- **App Management**: List installed apps, run apps remotely, install/uninstall external apps
- **WiFi Status**: View current WiFi connection details
- **Screenshot Capture**: Capture the current display as a PNG
- **System Controls**: Sync assets, reboot device

## Enabling the WebServer

The WebServer is disabled by default to conserve memory. Enable it through:

1. **Settings App**: Navigate to Settings > WebServer Settings
2. **Programmatically**: Call `tt::service::webserver::setWebServerEnabled(true)`

When enabled, a statusbar icon appears indicating the server mode (AP or Station).

## Accessing the Dashboard

Once enabled, access the dashboard by navigating to the device's IP address in a web browser:

```text
http://<device-ip>/
```

**Access Point Mode:** When using AP mode, connect to the device's WiFi network (SSID shown in settings, default `Tactility-XXXX`) and navigate to `http://192.168.4.1/`

The root URL redirects to `/dashboard.html` which provides a tabbed interface for all features.

## API Endpoints

All API endpoints return JSON responses unless otherwise noted.

### System Information

#### GET /api/sysinfo

Returns comprehensive system information.

**Response:**
```json
{
  "firmware": {
    "version": "1.0.0",
    "idf_version": "5.3.0"
  },
  "chip": {
    "model": "ESP32-S3",
    "cores": 2,
    "revision": 0,
    "features": ["Embedded Flash", "WiFi 2.4GHz", "BLE"],
    "flash_size": 16777216
  },
  "heap": {
    "free": 123456,
    "total": 327680,
    "min_free": 100000,
    "largest_block": 65536
  },
  "psram": {
    "free": 4000000,
    "total": 8388608,
    "min_free": 3500000,
    "largest_block": 2000000
  },
  "storage": {
    "data": {
      "free": 1000000,
      "total": 3145728,
      "mounted": true
    },
    "sdcard": {
      "free": 15000000000,
      "total": 32000000000,
      "mounted": true
    }
  },
  "uptime": 3600,
  "task_count": 25
}
```

### WiFi Status

#### GET /api/wifi

Returns current WiFi connection status.

**Response:**
```json
{
  "state": "connected",
  "ip": "192.168.1.100",
  "ssid": "MyNetwork",
  "rssi": -45,
  "secure": true
}
```

**State values:**
- `off` - WiFi radio is off
- `turning_on` - WiFi is starting
- `turning_off` - WiFi is stopping
- `on` - WiFi is on but not connected
- `connecting` - Connection in progress
- `connected` - Connected to access point

### Screenshot

#### GET /api/screenshot

Captures the current display and returns a PNG. The screenshot is also saved to storage with an incrementing filename.

**Response:** PNG data (`image/png`)

**Save Location:**
- SD card root (if mounted): `/sdcard/webscreenshot1.png`, `/sdcard/webscreenshot2.png`, etc.
- Internal storage (fallback): `/data/webscreenshot1.png`, `/data/webscreenshot2.png`, etc.

**Requirements:** `TT_FEATURE_SCREENSHOT_ENABLED` must be defined in the build.

**Note:** Returns 501 Not Implemented if screenshot feature is disabled.

### App Management

#### GET /api/apps

Lists all installed applications.

**Response:**
```json
{
  "apps": [
    {
      "id": "com.example.myapp",
      "name": "My App",
      "version": "1.0.0",
      "category": "user",
      "isExternal": true,
      "hidden": false,
      "icon": "/data/app/com.example.myapp/icon.png"
    }
  ]
}
```

**Category values:** `user`, `system`, `settings`

#### POST /api/apps/run?id=xxx

Runs an application by its ID. If the app is already running, it will be stopped first.

**Parameters:**
- `id` (required): Application ID

**Response:** `ok` on success

#### POST /api/apps/uninstall?id=xxx

Uninstalls an external application. System apps cannot be uninstalled.

**Parameters:**
- `id` (required): Application ID

**Response:** `ok` on success

**Errors:**
- 403 Forbidden: Cannot uninstall system apps
- 500 Internal Server Error: Uninstall failed

#### PUT /api/apps/install

Installs an application from an uploaded `.app` file (tar archive).

**Content-Type:** `multipart/form-data`

**Form field:** `file` - The `.app` file to install

**Response:** `ok` on success

### File System Operations

#### GET /fs/list?path=/path

Lists directory contents.

**Parameters:**
- `path` (optional): Directory path. Defaults to `/` which shows mount points.

**Response:**
```json
{
  "path": "/data",
  "entries": [
    {"name": "app", "type": "dir", "size": 0},
    {"name": "settings.json", "type": "file", "size": 1234}
  ]
}
```

**Special paths:**
- `/` - Shows available mount points (data, sdcard if mounted)
- `/data` - Internal flash storage
- `/sdcard` - SD card (if mounted)

#### GET /fs/download?path=/path/to/file

Downloads a file.

**Parameters:**
- `path` (required): Full path to the file

**Response:** File contents with appropriate Content-Type header and Content-Disposition for download.

#### POST /fs/upload?path=/path/to/file

Uploads a file. The request body contains the raw file data.

**Parameters:**
- `path` (required): Full destination path including filename

**Content-Type:** Any (raw file data in body)

**Response:** `Uploaded X bytes`

**Limits:** Maximum file size is 10MB.

#### POST /fs/mkdir?path=/path/to/newdir

Creates a new directory.

**Parameters:**
- `path` (required): Full path of directory to create

**Response:** `ok` on success

#### POST /fs/delete?path=/path/to/item

Deletes a file or directory (recursive for directories).

**Parameters:**
- `path` (required): Full path to delete

**Response:** `ok` on success

**Restrictions:** Cannot delete mount points (`/data`, `/sdcard`).

#### POST /fs/rename?path=/path/to/old&newName=newname

Renames a file or directory.

**Parameters:**
- `path` (required): Full path to the item to rename
- `newName` (required): New name (filename only, not a path)

**Response:** `ok` on success

**Restrictions:**
- `newName` cannot contain path separators or `..`
- Cannot overwrite existing items

#### GET /fs/tree

Returns a tree structure of all mount points and their immediate contents.

**Response:**
```json
{
  "mounts": [
    {
      "name": "data",
      "path": "/data",
      "entries": [
        {"name": "app", "type": "dir"},
        {"name": "tmp", "type": "dir"}
      ]
    }
  ]
}
```

### Admin Operations

#### POST /admin/sync

Synchronizes web assets from the Data partition.

**Response:** `Assets synchronized successfully`

#### POST /admin/reboot

Reboots the device after a 1-second delay.

**Response:** `Rebooting...`

## Static Assets

The WebServer serves static files from:

1. **Primary**: `/data/webserver/` (internal flash)
2. **Fallback**: `/sdcard/tactility/webserver/` (SD card)

The dashboard HTML file is served from these locations. If `dashboard.html` doesn't exist, `default.html` is served as a fallback.

## Asset Synchronization

The WebServer includes an asset synchronization system that keeps web assets in sync between the Data partition and SD card. This enables recovery after firmware updates and backup of user customizations.

### Storage Locations

| Location | Path | Purpose |
|----------|------|---------|
| Data Partition | `/data/webserver/` | Primary storage, served by WebServer |
| SD Card | `/sdcard/tactility/webserver/` | Backup storage for recovery |

### Version Tracking

Each storage location maintains a `version.json` file:

```json
{
  "version": 1
}
```

The version is an integer that increments when assets are updated. This allows the sync system to determine which location has newer assets.

### Sync Scenarios

The `syncAssets()` function handles several scenarios:

#### First Boot (No SD Card Backup)
- **Condition**: Data partition has assets, SD card backup doesn't exist
- **Action**: Skip backup during boot to avoid watchdog timeout
- **Note**: SD backup is deferred to first settings save

#### No SD Card Available
- **Condition**: SD card not mounted or unavailable
- **Action**: Create default Data structure with version 0 if needed
- **Note**: System operates normally without SD backup

#### Post-Flash Recovery
- **Condition**: Data partition empty, SD card has backup
- **Action**: Copy entire SD backup to Data partition
- **Use Case**: Restoring assets after flashing new firmware that erased Data

#### Firmware Update (SD Newer)
- **Condition**: SD version > Data version
- **Action**: Copy SD assets to Data partition
- **Use Case**: SD card contains newer assets from a firmware update package

#### User Customization (Data Newer)
- **Condition**: Data version > SD version
- **Action**: Defer backup to avoid boot watchdog timeout
- **Note**: Backup occurs on next settings save or manual sync

#### Versions Match
- **Condition**: Data version == SD version
- **Action**: No synchronization needed

### Boot Watchdog Considerations

Some sync operations are intentionally deferred during boot to avoid triggering the ESP32 watchdog timer:

- **Deferred**: Copying from Data to SD (user customization backup)
- **Deferred**: Creating SD version.json
- **Immediate**: Copying from SD to Data (recovery and firmware update)

This ensures the device boots reliably even with slow or corrupted SD cards.

### Manual Synchronization

#### Settings App
Navigate to **Settings > Web Server** and tap **"Sync Assets Now"** to manually trigger synchronization.

#### API Endpoint
Send a POST request to `/admin/sync`:

```bash
curl -X POST http://<device-ip>/admin/sync
```

**Response:** `Assets synchronized successfully`

### Programmatic Access

```cpp
#include <Tactility/service/webserver/AssetVersion.h>

// Check asset status
bool hasData = tt::service::webserver::hasDataAssets();
bool hasSd = tt::service::webserver::hasSdAssets();

// Load versions
tt::service::webserver::AssetVersion dataVer, sdVer;
tt::service::webserver::loadDataVersion(dataVer);
tt::service::webserver::loadSdVersion(sdVer);

// Trigger sync
bool success = tt::service::webserver::syncAssets();
```

### Directory Structure

```text
/data/webserver/
├── version.json          # Version tracking
├── dashboard.html        # Main dashboard UI
└── ...                   # Other web assets

/sdcard/tactility/webserver/
├── version.json          # Version tracking (backup)
├── dashboard.html        # Dashboard backup
└── ...                   # Other web assets (backup)
```

### Updating Assets

To update web assets with a new version:

1. Place new assets in `/sdcard/tactility/webserver/`
2. Update `/sdcard/tactility/webserver/version.json` with a higher version number
3. Reboot the device or trigger manual sync
4. The sync system will detect the newer SD version and copy to Data

## Security Considerations

> **⚠️ Security Warning**: The WebServer is unauthenticated by default, allowing anyone on the network to:
> - Upload, download, and delete files
> - Install and uninstall applications
> - Reboot the device
> - Capture screenshots
>
> **Strongly recommended**: 
> - Enable HTTP Basic Authentication in Settings > Web Server before exposing the device to untrusted networks
> - Keep "AP Open Network" disabled (use WPA2 password protection) to prevent unauthorized network access

- **⚠️ Open Network Option**: The "AP Open Network" setting allows creating an unprotected access point without a password. **This is convenient for quick access but exposes the device to anyone within WiFi range**, potentially allowing unauthorized access to all WebServer functionality if HTTP authentication is also disabled.
- **Automatic credential generation**: Credentials are automatically generated when empty:
  - **AP Password**: Generated when empty (unless "AP Open Network" is enabled)
  - **HTTP Auth**: Generated when auth is enabled but username or password are empty
  - Generated credentials are 12 alphanumeric characters (~71 bits of entropy) and persisted immediately
  - User-set credentials are preserved (the system only replaces empty credentials, not weak user-chosen passwords)
  - Check Settings > Web Server to view the generated credentials
- File operations are restricted to `/data` and `/sdcard` paths
- Path traversal attacks are blocked (e.g., `../` is rejected)
- Mount points cannot be deleted
- System apps cannot be uninstalled via the API

## Configuration

Settings are stored in the WebServer settings file and can be configured via **Settings > Web Server**:

| Setting | Description | Default |
|---------|-------------|---------|
| WiFi Mode | Station (connect to existing network) or Access Point (create own network) | Station |
| AP Open Network | Create an open AP without password protection | Disabled |
| AP Password | Password for Access Point mode (WPA2, 8-63 chars). Disabled when Open Network is enabled. | Auto-generated |
| Web Server Enabled | Whether the HTTP server is running | Disabled |
| Require Authentication | Enable HTTP Basic Authentication | Disabled |
| Username | Authentication username (when auth enabled) | Auto-generated |
| Password | Authentication password (when auth enabled) | Auto-generated |

**Note:** The system automatically generates secure credentials when they are empty. Generated credentials are 12-character alphanumeric strings with ~71 bits of entropy. See **Security Considerations** for details.

**Note:** WiFi Station credentials are managed separately via the WiFi settings menu.

## Statusbar Icons

When the WebServer is running, a statusbar icon indicates the WiFi mode:
- `webserver_ap_white.png` - Access Point mode
- `webserver_station_white.png` - Station mode

## Events

The WebServer publishes events:
- `WebServerStarted` - Fired when the HTTP server starts
- `WebServerStopped` - Fired when the HTTP server stops
- `WebServerSettingsChanged` - Fired when settings are modified

## Troubleshooting

### "No slots left for registering handler"

The ESP-IDF HTTP server has a limit on URI handlers. The WebServer configures this dynamically based on the number of handlers needed, but if you see this error, check `CONFIG_HTTPD_MAX_URI_HANDLERS` in sdkconfig.

### 404 for dashboard.html

Ensure the `dashboard.html` file exists in `/data/webserver/`. Run the asset sync operation or copy files manually.

### Screenshot fails

- Verify `TT_FEATURE_SCREENSHOT_ENABLED` is defined
- Check available heap memory (screenshot requires ~width*height*3 bytes)
- Ensure the save location (SD card or `/data`) is writable
- Screenshots are saved as `webscreenshot1.png`, `webscreenshot2.png`, etc. up to 9999

### File upload fails

- Check file size is under 10MB limit
- Verify the destination path is writable
- Ensure the parent directory exists

### Asset sync fails

- Check SD card is properly mounted and writable
- Verify sufficient space on destination (Data or SD card)
- Check logs for specific file copy errors
- Maximum directory depth is 16 levels
- If sync hangs during boot, the SD card may be slow or corrupted
