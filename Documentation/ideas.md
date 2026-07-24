# TODOs

## Before release

- WebServer service shouldn't save webserver.properties at start, it slows the boot process
- Remove incubating flag from various devices
- Add `// SPDX-License-Identifier: GPL-3.0-only` and `// SPDX-License-Identifier: Apache-2.0` to individual files in the project
- Elecrow Basic & Advance 3.5" memory issue: not enough memory for App Hub
- App Hub crashes if you close it while an app is being installed
- Calculator bugs (see GitHub issue)
- Try out speed optimizations: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html
  (relates to CONFIG_ESP32S3_DATA_CACHE_LINE_64B that is in use for RGB displays via the `device.properties` fix/workaround)

## Higher Priority

- Remove and migrate `Include/Tactility/kernel/Kernel.h` into `tactility/delay.h`
- Drivers/audio-codec-module is not a module. Move it somewhere else. Or make it an actual module.
- LilyGO T-Dongle S3: 1 button control, stop auto-launching web server
- Core2: support power off via software
- Create `#define` for empty module (for modules that fully rely on device.properties and don't define drivers or have start/stop logic)
- Get rid of TactilityC in favour of TactilityKernel and kernel modules
- Improve SPI kernel driver (implement read, write, transactions)
- Add font design tokens such as "regular", "title" and "smaller". Perhaps via the LVGL kernel module.
- Kernel concepts for ELF loading (generic approach for GUI apps, console apps, libraries).
- Fix glitches when installing app via App Hub with 4.3" Waveshare
- TCA9534 keyboards should use interrupts
- External app loading: Check the version of Tactility and check ESP target hardware to check for compatibility
  Check during installation process, but also when starting (SD card might have old app install from before Tactility OS update)
- Make a URL handler. Use it for handling local files. Match file types with apps.
  Create some kind of "intent" handler like on Android.
  The intent can have an action (e.g. view), a URL and an optional bundle.
  The manifest can provide the intent handler
- Support direct installation of an `.app` file with `tactility.py install helloworld.app <ip>`
- Support `tactility.py target <ip>` to remember the device IP address.
- minitar/untarFile(): "entry->metadata.path" can escape its confined path (e.g. "../something")

## Medium Priority

- Implement a LED kernel driver (single colour and RGB, plain GPIO and PWM)
- Make USB host driver disabled by default, so it doesn't consume memory
- Filtering for apps in App Hub:
  - apps that only work on a specific device
- Diceware app has large "+" and "-' buttons on Cardputer. It should be smaller.
- Create PwmRgbLedDevice class and implement it for all CYD devices
- TactilityTool: Make API compatibility table (and check for compatibility in the tool itself)
- Improve EspLcdDisplay to contain all the standard configuration options, and implement a default init function. Add a configuration class.
- Make WiFi setup app that starts an access point and hosts a webpage to set up the device.
  This will be useful for devices without a screen, a small screen or a non-touch screen.
- Unify the way displays are dimmed. Some implementations turn off the display when it's fully dimmed. Make this a separate functionality.
- Bug: Crash handling app cannot be exited with an EncoderDevice. (current work-around is to manually reset the device)

## Lower Priority

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
- Show a warning screen if firmware encryption or secure boot are off when saving WiFi credentials.
- Remove flex_flow from app_container in Gui.cpp
- ElfAppManifest: change name (remove "manifest" as it's confusing), remove icon and title, publish snapshot SDK on CDN
- Bug: CYD 2432S032C screen rotation fails due to touch driver issue
- Calculator app should show regular text input field on non-touch devices that have a keyboard (Cardputer, T-Lora Pager)
- Allow for WSAD keys to navigate LVGL (this is extra nice for cardputer, but just handy in general)
- Create a "How to" app for a device. It could explain things like keyboard navigation on first start.

# Nice-to-haves

- Considering the lack of callstack debugging for external apps: allow for some debugging to be exposed during a device crash. Apps could report their state (e.g. an integer value) which can be stored during app operation and retrieve after crash. The same can be done for various OS apps and states. We can keep an array of these numbers to keep track of the last X states, to get an idea of what's going on.
- Audio player app
- Audio recording app
- OTA updates
- If present, use LED to show boot/wifi status
- On crash, try to save the current log to flash or SD card? (this is risky, though, so ask in Discord first)
- Support more than 1 hardware keyboard (see lvgl::hardware_keyboard_set_indev()). LVGL init currently calls keyboard init, but that part should probably be done from the KeyboardDevice base class.

# App Ideas

- Map widget:
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/application/ui/ui_geomap.cpp
  https://github.com/portapack-mayhem/mayhem-firmware/blob/b66d8b1aa178d8a9cd06436fea788d5d58cb4c8d/firmware/tools/generate_world_map.bin.py
  https://github.com/portapack-mayhem/mayhem-firmware/releases
- Weather app: https://lab.flipper.net/apps/flip_weather
- wget app: https://lab.flipper.net/apps/web_crawler (add profiles for known public APIs?)
- Chip 8 emulator
- BadUSB (in December 2024, TinyUSB has a bug where uninstalling and re-installing the driver fails)
- Discord bot
- IR transceiver app
- GPS app
- Investigate CSI https://stevenmhernandez.github.io/ESP32-CSI-Tool/
- Calendar
- RSS reader
