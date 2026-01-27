# DisplayIdle Service

The DisplayIdle service manages screen timeout, screensavers, and backlight control for Tactility devices.

## Features

### Screen Timeout
When enabled, the display will automatically dim after a configurable period of inactivity. Timeout options:
- 15 seconds
- 30 seconds
- 1 minute
- 2 minutes
- 5 minutes
- Never

### Screensavers
Four screensaver options are available:

| Type | Description |
|------|-------------|
| **None** | Black screen only, backlight turns off immediately |
| **Bouncing Balls** | Colored balls bouncing around the screen |
| **Mystify** | Classic Windows-style polygon trails with color-changing effects |
| **Matrix Rain** | Digital rain effect with terminal-style grid movement, 6-color gradient trails, glow effects, and random character flicker |

### Auto-Off Feature
After 5 minutes of screensaver activity, the screensaver animation stops and the backlight turns off completely to save power. Touching the screen restores normal operation.

## Public API

The service exposes a public header for external control:

```cpp
#include <Tactility/service/displayidle/DisplayIdleService.h>

// Get service instance
auto displayIdle = tt::service::displayidle::findService();

// Force start screensaver immediately
displayIdle->startScreensaver();

// Force stop screensaver and restore backlight
displayIdle->stopScreensaver();

// Check if screensaver is currently active
bool active = displayIdle->isScreensaverActive();

// Reload settings (call after external settings changes)
displayIdle->reloadSettings();
```

## Architecture

### Files

| File | Purpose |
|------|---------|
| `DisplayIdleService.h` | Public header with service interface |
| `DisplayIdle.cpp` | Service implementation |
| `Screensaver.h` | Base class for screensaver implementations |
| `BouncingBallsScreensaver.h/cpp` | Bouncing balls screensaver |
| `MystifyScreensaver.h/cpp` | Mystify polygon screensaver |
| `MatrixRainScreensaver.h/cpp` | Matrix digital rain screensaver |

### Screensaver Base Class

All screensavers inherit from the `Screensaver` base class:

```cpp
class Screensaver {
public:
    virtual void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) = 0;
    virtual void stop() = 0;
    virtual void update(lv_coord_t screenW, lv_coord_t screenH) = 0;
};
```

### Adding a New Screensaver

1. Create header and implementation files inheriting from `Screensaver`
2. Add enum value to `ScreensaverType` in `DisplaySettings.h` (before `Count` sentinel)
3. Add string conversion in `DisplaySettings.cpp` (`toString` and `fromString`)
4. Add dropdown option in `Display.cpp` (order must match enum order)
5. Add case in `DisplayIdle.cpp` `activateScreensaver()` switch
6. Include the new header in `DisplayIdle.cpp`

**Note:** The `ScreensaverType::Count` sentinel must always be the last enum value - it's used for bounds checking in the UI.

## Settings Integration

Settings are stored in `/data/settings/display.properties` and managed through `DisplaySettings.h`:

```cpp
struct DisplaySettings {
    Orientation orientation;
    uint8_t gammaCurve;
    uint8_t backlightDuty;
    bool backlightTimeoutEnabled;
    uint32_t backlightTimeoutMs;
    ScreensaverType screensaverType;
};
```

The Display app (`Display.cpp`) provides the UI for configuring these settings and notifies the DisplayIdle service when settings change via `reloadSettings()`.

## Timing

- Service tick interval: 50ms
- Wake activity threshold: 100ms
- Screensaver auto-off: 5 minutes (6000 ticks)
