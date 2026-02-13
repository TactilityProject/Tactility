# TODOs

## Before release

- Remove incubating flag from various devices
- Add `// SPDX-License-Identifier: GPL-3.0-only` and `// SPDX-License-Identifier: Apache-2.0` to individual files in the project
- Elecrow Basic & Advance 3.5" memory issue: not enough memory for App Hub
- App Hub crashes if you close it while an app is being installed
- Calculator bugs (see GitHub issue)
- Try out speed optimizations: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
  (relates to CONFIG_ESP32S3_DATA_CACHE_LINE_64B that is in use for RGB displays via the `device.properties` fix/workaround)

## Higher Priority

- Make a root device type so it can be discovered more easily.
- When device.py selects a new device, it should automatically delete the build dirs (build/, cmake-*/) when it detects that the platform has changed.
- Add font design tokens such as "regular", "title" and "smaller". Perhaps via the LVGL kernel module.
- Add kernel listening mechanism so that the root device init can be notified when a device becomes available: 
  Callback for device/start stop with filtering on device type:
    - on_before_start: e.g. to do the CS pin hack for SD card on a shared bus
    - on_after_start: e.g. to initialize an I2S device via its I2C connection, once I2C becomes available
    - on_before_stop: e.g. to stop using a device that was started before
    - on_after_stop: ?
- DTS: support for #defines
- DTS: support for aliases
- SPI kernel driver
- iomux kernel driver
- Kernel concepts for ELF loading (generic approach for GUI apps, console apps, libraries).
- Fix glitches when installing app via App Hub with 4.3" Waveshare
- TCA9534 keyboards should use interrupts
- GT911 drivers should use interrupts if it's stable
- Change ButtonControl to work with interrupts and xQueue
- Fix Cardputer (original): use LV_KEY_NEXT and _PREV in keyboard mapping instead of encoder driver hack (and check GPIO app if it then hangs too)
- Logging with a function that uses std::format
- Expose http::download() and main dispatcher to TactiltyC.
- External app loading: Check the version of Tactility and check ESP target hardware to check for compatibility
  Check during installation process, but also when starting (SD card might have old app install from before Tactility OS update)
- Make a URL handler. Use it for handling local files. Match file types with apps.
  Create some kind of "intent" handler like on Android.
  The intent can have an action (e.g. view), a URL and an optional bundle.
  The manifest can provide the intent handler
- When an SD card is detected, check if it has been initialized and assigned as data partition.
  If the user choses to select it, then copy files from /data over to it.
  Write the user choice to a file on the card.
  File contains 3 statuses: ignore, data, .. initdata?
  The latter is used for auto-selecting it as data partition.
- Support direct installation of an `.app` file with `tactility.py install helloworld.app <ip>`
- Support `tactility.py target <ip>` to remember the device IP address.
- minitar/untarFile(): "entry->metadata.path" can escape its confined path (e.g. "../something")
- Refactor elf loader code to make it multi-platform and to support multiple types of executables

## Medium Priority

- Filtering for apps in App Hub:
  - apps that only work on a specific device
- Diceware app has large "+" and "-' buttons on Cardputer. It should be smaller.
- Create PwmRgbLedDevice class and implement it for all CYD devices
- TactilityTool: Make API compatibility table (and check for compatibility in the tool itself)
- Improve EspLcdDisplay to contain all the standard configuration options, and implement a default init function. Add a configuration class.
- Make WiFi setup app that starts an access point and hosts a webpage to set up the device.
  This will be useful for devices without a screen, a small screen or a non-touch screen.
- Unify the way displays are dimmed. Some implementations turn off the display when it's fully dimmed. Make this a separate functionality.
- Try out ILI9342 https://github.com/jbrilha/esp_lcd_ili9342
- All drivers (e.g. display, touch, etc.) should call stop() in their destructor, or at least assert that they should not be running.
- Bug: Turn on WiFi (when testing it wasn't connected/connecting - just active). Open chat. Observe crash.
- Bug: Crash handling app cannot be exited with an EncoderDevice. (current work-around is to manually reset the device)
- I2C app should show error when I2C port is disabled when the scan button was manually pressed

## Lower Priority

- The boot button on some devices can be used as GPIO_NUM_0 at runtime
- Localize all apps
- Support hot-plugging SD card (note: this is not possible if they require the CS pin hack)
- Explore LVGL9's FreeRTOS functionality
- CrashHandler: use "corrupted" flag
- CrashHandler: process other types of crashes (WDT?)
- Use GPS time to set/update the current time
- Fix bug in T-Deck/etc: esp_lvgl_port settings has a large stack size (~9kB) to fix an issue where the T-Deck would get a stackoverflow. This sometimes happens when WiFi is auto-enabled and you open the app while it is still connecting.
- Consider using non_null (either via MS GSL, or custom)
- Fix system time to not be 1980 (use build year as a minimum). Consider keeping track of the last known time.
- Use std::span or string_view in StringUtils https://youtu.be/FRkJCvHWdwQ?t=2754 
- Mutex: Implement give/take from ISR support (works only for non-recursive ones)
- Extend unPhone power driver: add charging status, usb connection status, etc.
- Clear screen before turning on blacklight (e.g. T-Deck, CYD 2432S028R, etc.)
- T-Deck: Use trackball as input device (with optional mouse functionality for LVGL)
- Show a warning screen if firmware encryption or secure boot are off when saving WiFi credentials.
- Remove flex_flow from app_container in Gui.cpp
- Files app: copy/cut/paste actions
- ElfAppManifest: change name (remove "manifest" as it's confusing), remove icon and title, publish snapshot SDK on CDN
- `UiScale` implementation for devices like the CYD 2432S032C
- Bug: CYD 2432S032C screen rotation fails due to touch driver issue
- Calculator app should show regular text input field on non-touch devices that have a keyboard (Cardputer, T-Lora Pager)
- Allow for WSAD keys to navigate LVGL (this is extra nice for cardputer, but just handy in general)
- Create a "How to" app for a device. It could explain things like keyboard navigation on first start.

# Nice-to-haves

- Considering the lack of callstack debugging for external apps: allow for some debugging to be exposed during a device crash. Apps could report their state (e.g. an integer value) which can be stored during app operation and retrieve after crash. The same can be done for various OS apps and states. We can keep an array of these numbers to keep track of the last X states, to get an idea of what's going on.
- Audio player app
- Audio recording app
- OTA updates
- T-Deck Plus: Create separate device config?
- Support for displays with different DPI. Consider the layer-based system like on Android.
- If present, use LED to show boot/wifi status
- Capacity based on voltage: estimation for various devices uses a linear voltage curve, but it should use a battery discharge curve.
- Wrapper for lvgl slider widget that shows "+" and "-" buttons, and also the value in a label.
  Note: consider Spinbox
- On crash, try to save the current log to flash or SD card? (this is risky, though, so ask in Discord first)
- Support more than 1 hardware keyboard (see lvgl::hardware_keyboard_set_indev()). LVGL init currently calls keyboard init, but that part should probably be done from the KeyboardDevice base class.

# App Ideas

- Revisit TinyUSB mouse idea: the bugs related to cleanup seem to be fixed in the library.
- Map widget:
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/application/ui/ui_geomap.cpp
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/tools/generate_world_map.bin.py
  https://github.com/portapack-mayhem/mayhem-firmware/releases
- Weather app: https://lab.flipper.net/apps/flip_weather
- wget app: https://lab.flipper.net/apps/web_crawler (add profiles for known public APIs?)
- BlueTooth keyboard app
- Chip 8 emulator
- BadUSB (in December 2024, TinyUSB has a bug where uninstalling and re-installing the driver fails)
- Discord bot
- IR transceiver app
- GPS app
- Investigate CSI https://stevenmhernandez.github.io/ESP32-CSI-Tool/
- Compile unix tools to ELF apps?
- Todo list
- Calendar
- Display touch calibration
- RSS reader
- Static file web server (with option to specify path and port)
- Port TamaFi https://github.com/cifertech/TamaFi

# Notes on firmware size

- adding esp_http_client (with esp_event) added about 100kB